/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/dijkstra-routing-protocol.h"
#include "ns3/double.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-fidelity-model.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-link-layer-service.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-routing-metric.h"
#include "ns3/sliced-dijkstra-routing-protocol.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace ns3;

namespace {

LinkMetrics
MakeLinkMetrics (double initialFidelity,
                 double successRate = 1.0,
                 double latencyMs = 0.0,
                 double quantumSetupTimeMs = 0.0,
                 double classicalControlDelayMs = 0.0,
                 bool isAvailable = true,
                 BellPairNoiseFamily noiseFamily = BellPairNoiseFamily::WERNER)
{
  LinkMetrics metrics;
  metrics.fidelity = initialFidelity;
  metrics.initialFidelity = initialFidelity;
  metrics.noiseFamily = noiseFamily;
  metrics.successRate = successRate;
  metrics.latency = latencyMs;
  metrics.quantumSetupTimeMs = quantumSetupTimeMs;
  metrics.classicalControlDelayMs = classicalControlDelayMs;
  metrics.isAvailable = isAvailable;
  return metrics;
}

double
GetScalar (const QuantumRoutingLabel& label, const std::string& key, double fallback = 0.0)
{
  auto it = label.scalars.find (key);
  if (it == label.scalars.end ())
    {
      return fallback;
    }
  return it->second;
}

void
NoopEntanglementCallback (EntanglementId, EntanglementState)
{
}

void
SampleFidelity (Ptr<QuantumPhyEntity> qphyent,
                const std::string& leftQubit,
                const std::string& rightQubit,
                double* output)
{
  double fidelity = 0.0;
  qphyent->CalculateFidelity ({leftQubit, rightQubit}, fidelity);
  *output = fidelity;
}

void
CapturePathSnapshot (Ptr<QuantumNetworkLayer> netLayer,
                     PathId pathId,
                     PathState* state,
                     bool* ready,
                     double* actualFidelity)
{
  PathInfo info = netLayer->GetPathInfo (pathId);
  *state = info.state;
  *ready = netLayer->IsPathReady (pathId);
  *actualFidelity = info.actualFidelity;
}

void
DisposeLinearNetwork (std::vector<Ptr<QuantumNetworkLayer>>& netLayers,
                      Ptr<QuantumLinkLayerService>& linkLayer,
                      Ptr<QuantumPhyEntity>& qphyent)
{
  for (auto& netLayer : netLayers)
    {
      if (netLayer)
        {
          netLayer->Dispose ();
          netLayer = nullptr;
        }
    }
  netLayers.clear ();

  if (linkLayer)
    {
      linkLayer->Dispose ();
      linkLayer = nullptr;
    }

  if (qphyent)
    {
      qphyent->Dispose ();
      qphyent = nullptr;
    }
}

struct LinearNetworkContext
{
  Ptr<QuantumPhyEntity> qphyent;
  Ptr<QuantumLinkLayerService> linkLayer;
  std::vector<Ptr<QuantumNetworkLayer>> netLayers;
};

struct RoutingMetricContext
{
  Ptr<QuantumPhyEntity> qphyent;
  Ptr<QuantumNetworkLayer> netLayer;
};

LinearNetworkContext
BuildLinearNetwork (double linkFidelity,
                    double quantumSetupTimeMs,
                    double classicalControlDelayMs,
                    uint32_t maxRetries)
{
  LinearNetworkContext ctx;
  std::vector<std::string> owners = {"A", "B", "C"};

  ctx.qphyent = CreateObject<QuantumPhyEntity> (owners);
  for (const auto& owner : owners)
    {
      ctx.qphyent->SetTimeModel (owner, 1000.0);
    }

  ctx.linkLayer = CreateObject<QuantumLinkLayerService> ();
  ctx.linkLayer->SetPhyEntity (ctx.qphyent);

  for (const auto& owner : owners)
    {
      Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer> ();
      netLayer->SetAttribute ("MaxRetries", UintegerValue (maxRetries));
      netLayer->SetOwner (owner);
      netLayer->SetPhyEntity (ctx.qphyent);
      netLayer->SetLinkLayer (ctx.linkLayer);
      netLayer->SetRoutingProtocol (CreateObject<DijkstraRoutingProtocol> ());
      ctx.netLayers.push_back (netLayer);
    }

  auto addBidirectionalLink = [&] (uint32_t left, uint32_t right) {
    const std::string src = owners[left];
    const std::string dst = owners[right];

    Ptr<QuantumChannel> forward = CreateObject<QuantumChannel> (src, dst);
    Ptr<QuantumChannel> reverse = CreateObject<QuantumChannel> (dst, src);

    ctx.netLayers[left]->AddNeighbor (dst,
                                      forward,
                                      linkFidelity,
                                      1.0,
                                      quantumSetupTimeMs,
                                      classicalControlDelayMs);
    ctx.netLayers[right]->AddNeighbor (src,
                                       reverse,
                                       linkFidelity,
                                       1.0,
                                       quantumSetupTimeMs,
                                       classicalControlDelayMs);
  };

  addBidirectionalLink (0, 1);
  addBidirectionalLink (1, 2);

  for (auto& netLayer : ctx.netLayers)
    {
      netLayer->Initialize ();
    }

  return ctx;
}

RoutingMetricContext
BuildRoutingMetricContext (const std::vector<std::string>& owners,
                           const std::map<std::string, double>& coherenceTimeSeconds)
{
  RoutingMetricContext ctx;
  ctx.qphyent = CreateObject<QuantumPhyEntity> (owners);

  for (const auto& owner : owners)
    {
      auto tauIt = coherenceTimeSeconds.find (owner);
      const double tauSeconds = tauIt == coherenceTimeSeconds.end () ? 1000.0 : tauIt->second;
      ctx.qphyent->SetTimeModel (owner, tauSeconds);
    }

  ctx.netLayer = CreateObject<QuantumNetworkLayer> ();
  ctx.netLayer->SetOwner (owners.front ());
  ctx.netLayer->SetPhyEntity (ctx.qphyent);
  return ctx;
}

double
ResolveTheoreticalSetupTimeMs (const LinkMetrics& metrics)
{
  if (metrics.quantumSetupTimeMs > 0.0)
    {
      return metrics.quantumSetupTimeMs;
    }
  if (metrics.latency > 0.0 && metrics.classicalControlDelayMs <= 0.0)
    {
      return metrics.latency;
    }
  if (metrics.classicalControlDelayMs > 0.0)
    {
      return metrics.classicalControlDelayMs;
    }
  return 0.0;
}

const LinkMetrics&
LookupLinkMetrics (const std::map<std::string, std::map<std::string, LinkMetrics>>& topology,
                   const std::string& left,
                   const std::string& right)
{
  return topology.at (left).at (right);
}

double
SimulateRouteActualFidelity (const std::vector<std::string>& route,
                             const std::map<std::string, std::map<std::string, LinkMetrics>>& topology,
                             const std::map<std::string, double>& coherenceTimeSeconds)
{
  if (route.size () < 2)
    {
      return 0.0;
    }

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (route);
  for (const auto& owner : route)
    {
      auto tauIt = coherenceTimeSeconds.find (owner);
      const double tauSeconds = tauIt == coherenceTimeSeconds.end () ? 1000.0 : tauIt->second;
      qphyent->SetTimeModel (owner, tauSeconds);
    }

  std::vector<std::string> leftQubits;
  std::vector<std::string> rightQubits;
  leftQubits.reserve (route.size () - 1);
  rightQubits.reserve (route.size () - 1);

  double tMaxMs = 0.0;
  for (uint32_t i = 0; i + 1 < route.size (); ++i)
    {
      const LinkMetrics& metrics = LookupLinkMetrics (topology, route[i], route[i + 1]);
      const double setupMs = ResolveTheoreticalSetupTimeMs (metrics);
      tMaxMs = std::max (tMaxMs, setupMs);

      const std::string leftQubit = "seg_" + std::to_string (i) + "_left";
      const std::string rightQubit = "seg_" + std::to_string (i) + "_right";
      leftQubits.push_back (leftQubit);
      rightQubits.push_back (rightQubit);

      Simulator::Schedule (
          MilliSeconds (setupMs),
          [qphyent, metrics, leftOwner = route[i], rightOwner = route[i + 1], leftQubit, rightQubit]() {
            qphyent->GenerateQubitsMixed (
                leftOwner,
                GetEPRwithNoiseFamily (
                    metrics.noiseFamily,
                    metrics.initialFidelity > 0.0 ? metrics.initialFidelity : metrics.fidelity),
                {leftQubit, rightQubit});
            qphyent->TransferQubit (leftOwner, rightOwner, rightQubit);
          });
    }

  double measuredFidelity = 0.0;
  Simulator::Schedule (
      MilliSeconds (tMaxMs),
      [qphyent, route, leftQubits, rightQubits, &measuredFidelity]() {
        std::string leftEndpoint = leftQubits.front ();
        std::string carriedRightEndpoint = rightQubits.front ();

        for (uint32_t segIndex = 1; segIndex < leftQubits.size (); ++segIndex)
          {
            const std::string& owner = route[segIndex];
            const std::string& rightOwner = route[segIndex + 1];
            const std::string& localRightSegmentQubit = leftQubits[segIndex];
            const std::string& farRightQubit = rightQubits[segIndex];

            qphyent->ApplyGate (owner,
                                QNS_GATE_PREFIX + "CNOT",
                                std::vector<std::complex<double>>{},
                                std::vector<std::string>{localRightSegmentQubit, carriedRightEndpoint});
            qphyent->ApplyGate (owner,
                                QNS_GATE_PREFIX + "H",
                                std::vector<std::complex<double>>{},
                                std::vector<std::string>{carriedRightEndpoint});

            auto outcomeLeft = qphyent->Measure (owner, {carriedRightEndpoint});
            auto outcomeRight = qphyent->Measure (owner, {localRightSegmentQubit});

            if (outcomeRight.first == 1)
              {
                qphyent->ApplyGate (rightOwner,
                                    QNS_GATE_PREFIX + "PX",
                                    std::vector<std::complex<double>>{},
                                    std::vector<std::string>{farRightQubit});
              }
            if (outcomeLeft.first == 1)
              {
                qphyent->ApplyGate (rightOwner,
                                    QNS_GATE_PREFIX + "PZ",
                                    std::vector<std::complex<double>>{},
                                    std::vector<std::string>{farRightQubit});
              }

            qphyent->PartialTrace ({carriedRightEndpoint, localRightSegmentQubit});
            carriedRightEndpoint = farRightQubit;
          }

        qphyent->CalculateFidelity ({leftEndpoint, carriedRightEndpoint}, measuredFidelity);
      });

  Simulator::Run ();

  qphyent->Dispose ();
  Simulator::Destroy ();
  return measuredFidelity;
}

class LatencyRoutingMetric : public QuantumRoutingMetric
{
public:
  static TypeId GetTypeId ();

  LatencyRoutingMetric () = default;
  ~LatencyRoutingMetric () override = default;

  QuantumRoutingLabel CreateInitialLabel (const std::string& srcNode) const override;

  bool ExtendLabel (QuantumNetworkLayer* networkLayer,
                    const QuantumRoutingLabel& current,
                    const std::string& nextNode,
                    const LinkMetrics& linkAttributes,
                    QuantumRoutingLabel& extended) const override;

  bool IsBetter (const QuantumRoutingLabel& lhs,
                 const QuantumRoutingLabel& rhs) const override;

  bool Dominates (const QuantumRoutingLabel& lhs,
                  const QuantumRoutingLabel& rhs) const override;

  double GetScore (const QuantumRoutingLabel& label) const override;
};

NS_OBJECT_ENSURE_REGISTERED (LatencyRoutingMetric);

TypeId
LatencyRoutingMetric::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::LatencyRoutingMetric")
          .SetParent<QuantumRoutingMetric> ()
          .SetGroupName ("Quantum")
          .AddConstructor<LatencyRoutingMetric> ();
  return tid;
}

QuantumRoutingLabel
LatencyRoutingMetric::CreateInitialLabel (const std::string& srcNode) const
{
  QuantumRoutingLabel label = QuantumRoutingMetric::CreateInitialLabel (srcNode);
  label.scalars["neg_total_latency"] = 0.0;
  return label;
}

bool
LatencyRoutingMetric::ExtendLabel (QuantumNetworkLayer*,
                                   const QuantumRoutingLabel& current,
                                   const std::string& nextNode,
                                   const LinkMetrics& linkAttributes,
                                   QuantumRoutingLabel& extended) const
{
  if (!linkAttributes.isAvailable || current.path.empty ())
    {
      return false;
    }

  if (std::find (current.path.begin (), current.path.end (), nextNode) != current.path.end ())
    {
      return false;
    }

  extended = current;
  extended.path.push_back (nextNode);

  double totalLatency = -GetScore (current) + std::max (0.0, linkAttributes.latency);
  extended.scalars["neg_total_latency"] = -totalLatency;
  return true;
}

bool
LatencyRoutingMetric::IsBetter (const QuantumRoutingLabel& lhs,
                                const QuantumRoutingLabel& rhs) const
{
  double lhsScore = GetScore (lhs);
  double rhsScore = GetScore (rhs);

  if (lhsScore > rhsScore + 1e-12)
    {
      return true;
    }
  if (rhsScore > lhsScore + 1e-12)
    {
      return false;
    }

  return lhs.path.size () < rhs.path.size ();
}

bool
LatencyRoutingMetric::Dominates (const QuantumRoutingLabel& lhs,
                                 const QuantumRoutingLabel& rhs) const
{
  double lhsScore = GetScore (lhs);
  double rhsScore = GetScore (rhs);
  return lhsScore + 1e-12 >= rhsScore &&
         (lhsScore > rhsScore + 1e-12 || lhs.path.size () <= rhs.path.size ());
}

double
LatencyRoutingMetric::GetScore (const QuantumRoutingLabel& label) const
{
  return GetScalar (label, "neg_total_latency", 0.0);
}

} // namespace

class QuantumBasisSmokeTestCase : public TestCase
{
public:
  QuantumBasisSmokeTestCase ()
      : TestCase ("Quantum module smoke test")
  {
  }

private:
  void DoRun () override
  {
    NS_TEST_ASSERT_MSG_EQ (true, true, "Smoke test should always pass");
    NS_TEST_ASSERT_MSG_EQ_TOL (0.01, 0.01, 0.001, "Floating-point comparison sanity check");
  }
};

class AutoDecoherenceFidelityQueryTestCase : public TestCase
{
public:
  AutoDecoherenceFidelityQueryTestCase ()
      : TestCase ("CalculateFidelity should flush pending decoherence automatically")
  {
  }

private:
  void DoRun () override
  {
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->SetTimeModel ("God", 0.05);

    bool generated =
        qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (1.0), {"qa", "qb"});
    NS_TEST_ASSERT_MSG_EQ (generated, true, "Failed to generate an EPR pair for the decoherence test");

    double initialFidelity = 0.0;
    qphyent->CalculateFidelity ({"qa", "qb"}, initialFidelity);

    double delayedFidelity = 0.0;
    Simulator::Schedule (MilliSeconds (100),
                         &SampleFidelity,
                         qphyent,
                         std::string ("qa"),
                         std::string ("qb"),
                         &delayedFidelity);

    Simulator::Run ();

    NS_TEST_ASSERT_MSG_GT (initialFidelity,
                           delayedFidelity,
                           "Fidelity query after waiting should reflect storage decoherence");
    NS_TEST_ASSERT_MSG_LT (delayedFidelity,
                           0.999,
                           "Delayed fidelity unexpectedly stayed near the initial value");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class CustomMetricRoutingTestCase : public TestCase
{
public:
  CustomMetricRoutingTestCase ()
      : TestCase ("Dijkstra routing should accept a custom metric model")
  {
  }

private:
  void DoRun () override
  {
    Ptr<DijkstraRoutingProtocol> routing = CreateObject<DijkstraRoutingProtocol> ();
    routing->SetMetricModel (CreateObject<LatencyRoutingMetric> ());
    routing->Initialize ();

    routing->AddNeighbor ("S", "A", MakeLinkMetrics (0.80, 1.0, 10.0));
    routing->AddNeighbor ("A", "D", MakeLinkMetrics (0.80, 1.0, 10.0));
    routing->AddNeighbor ("S", "B", MakeLinkMetrics (0.60, 1.0, 1.0));
    routing->AddNeighbor ("B", "D", MakeLinkMetrics (0.60, 1.0, 1.0));

    std::vector<std::string> route = routing->CalculateRoute ("S", "D");
    NS_TEST_ASSERT_MSG_EQ (route.size (), 3u, "Expected a two-hop route from S to D");
    NS_TEST_ASSERT_MSG_EQ (route[0], std::string ("S"), "Route should start at the source");
    NS_TEST_ASSERT_MSG_EQ (route[1],
                           std::string ("B"),
                           "Custom latency metric should prefer the lower-latency branch");
    NS_TEST_ASSERT_MSG_EQ (route[2], std::string ("D"), "Route should end at the destination");

    double metric = routing->GetRouteMetric ("S", "D");
    NS_TEST_ASSERT_MSG_EQ_TOL (metric,
                               -2.0,
                               1e-9,
                               "Custom metric score should reflect accumulated link latency");

    routing->Dispose ();
  }
};

class SlicedCustomMetricRoutingTestCase : public TestCase
{
public:
  SlicedCustomMetricRoutingTestCase ()
      : TestCase ("Sliced Dijkstra routing should accept a custom metric model")
  {
  }

private:
  void DoRun () override
  {
    Ptr<SlicedDijkstraRoutingProtocol> routing = CreateObject<SlicedDijkstraRoutingProtocol> ();
    routing->SetMetricModel (CreateObject<LatencyRoutingMetric> ());
    routing->SetAttribute ("K", UintegerValue (3));
    routing->SetAttribute ("UseBuckets", BooleanValue (false));
    routing->Initialize ();

    routing->AddNeighbor ("S", "A", MakeLinkMetrics (0.80, 1.0, 10.0));
    routing->AddNeighbor ("A", "D", MakeLinkMetrics (0.80, 1.0, 10.0));
    routing->AddNeighbor ("S", "B", MakeLinkMetrics (0.60, 1.0, 1.0));
    routing->AddNeighbor ("B", "D", MakeLinkMetrics (0.60, 1.0, 1.0));

    std::vector<std::string> route = routing->CalculateRoute ("S", "D");
    NS_TEST_ASSERT_MSG_EQ (route.size (), 3u, "Expected a two-hop route from S to D");
    NS_TEST_ASSERT_MSG_EQ (route[0], std::string ("S"), "Route should start at the source");
    NS_TEST_ASSERT_MSG_EQ (route[1],
                           std::string ("B"),
                           "Custom latency metric should prefer the lower-latency branch");
    NS_TEST_ASSERT_MSG_EQ (route[2], std::string ("D"), "Route should end at the destination");

    double metric = routing->GetRouteMetric ("S", "D");
    NS_TEST_ASSERT_MSG_EQ_TOL (metric,
                               -2.0,
                               1e-9,
                               "Custom metric score should reflect accumulated link latency");

    routing->Dispose ();
  }
};

class WernerPairDephasingFormulaTestCase : public TestCase
{
public:
  WernerPairDephasingFormulaTestCase ()
      : TestCase ("Bell-pair fidelity after waiting should follow the phase-flip Bell-diagonal formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double coherenceTimeSeconds = 0.1;
    const double waitSeconds = 0.01;
    const double initialFidelity = 0.9;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->SetTimeModel ("God", coherenceTimeSeconds);
    qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (initialFidelity), {"qa", "qb"});

    Simulator::Schedule (Seconds (waitSeconds),
                         &SampleFidelity,
                         qphyent,
                         std::string ("qa"),
                         std::string ("qb"),
                         &measuredFidelity);

    Simulator::Run ();

    BellDiagonalState expectedState =
        ApplyPhaseFlipMemoryWait (MakeWernerState (initialFidelity),
                                  waitSeconds * 1000.0,
                                  coherenceTimeSeconds * 1000.0,
                                  waitSeconds * 1000.0,
                                  coherenceTimeSeconds * 1000.0);

    NS_TEST_ASSERT_MSG_EQ_TOL (measuredFidelity,
                               GetBellFidelity (expectedState),
                               1e-9,
                               "Single-pair fidelity no longer matches the Bell-diagonal wait formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class WernerSwapFormulaTestCase : public TestCase
{
public:
  WernerSwapFormulaTestCase ()
      : TestCase ("Entanglement swapping should follow the Bell-diagonal Werner-state formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double leftFidelity = 0.93;
    const double rightFidelity = 0.88;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (leftFidelity), {"qa", "qb"});
    qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (rightFidelity), {"qc", "qd"});

    qphyent->ApplyGate ("God",
                        QNS_GATE_PREFIX + "CNOT",
                        std::vector<std::complex<double>>{},
                        std::vector<std::string>{"qc", "qb"});
    qphyent->ApplyGate ("God",
                        QNS_GATE_PREFIX + "H",
                        std::vector<std::complex<double>>{},
                        std::vector<std::string>{"qb"});

    auto outcomeQb = qphyent->Measure ("God", {"qb"});
    auto outcomeQc = qphyent->Measure ("God", {"qc"});

    if (outcomeQc.first == 1)
      {
        qphyent->ApplyGate ("God",
                            QNS_GATE_PREFIX + "PX",
                            std::vector<std::complex<double>>{},
                            std::vector<std::string>{"qd"});
      }
    if (outcomeQb.first == 1)
      {
        qphyent->ApplyGate ("God",
                            QNS_GATE_PREFIX + "PZ",
                            std::vector<std::complex<double>>{},
                            std::vector<std::string>{"qd"});
      }

    qphyent->PartialTrace ({"qb", "qc"});
    qphyent->CalculateFidelity ({"qa", "qd"}, measuredFidelity);

    BellDiagonalState expectedState =
        EntanglementSwapBellDiagonal (MakeWernerState (leftFidelity), MakeWernerState (rightFidelity));

    NS_TEST_ASSERT_MSG_EQ_TOL (measuredFidelity,
                               GetBellFidelity (expectedState),
                               1e-9,
                               "Swapped-pair fidelity no longer matches the Bell-diagonal swap formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class WernerWaitThenSwapFormulaTestCase : public TestCase
{
public:
  WernerWaitThenSwapFormulaTestCase ()
      : TestCase ("Waiting and then swapping should follow the Bell-diagonal wait-plus-swap formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double coherenceTimeSeconds = 0.1;
    const double waitSeconds = 0.01;
    const double leftFidelity = 0.93;
    const double rightFidelity = 0.88;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->SetTimeModel ("God", coherenceTimeSeconds);
    qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (leftFidelity), {"qa", "qb"});
    qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (rightFidelity), {"qc", "qd"});

    Simulator::Schedule (
        Seconds (waitSeconds),
        [qphyent, &measuredFidelity]() {
          qphyent->ApplyGate ("God",
                              QNS_GATE_PREFIX + "CNOT",
                              std::vector<std::complex<double>>{},
                              std::vector<std::string>{"qc", "qb"});
          qphyent->ApplyGate ("God",
                              QNS_GATE_PREFIX + "H",
                              std::vector<std::complex<double>>{},
                              std::vector<std::string>{"qb"});

          auto outcomeQb = qphyent->Measure ("God", {"qb"});
          auto outcomeQc = qphyent->Measure ("God", {"qc"});

          if (outcomeQc.first == 1)
            {
              qphyent->ApplyGate ("God",
                                  QNS_GATE_PREFIX + "PX",
                                  std::vector<std::complex<double>>{},
                                  std::vector<std::string>{"qd"});
            }
          if (outcomeQb.first == 1)
            {
              qphyent->ApplyGate ("God",
                                  QNS_GATE_PREFIX + "PZ",
                                  std::vector<std::complex<double>>{},
                                  std::vector<std::string>{"qd"});
            }

          qphyent->PartialTrace ({"qb", "qc"});
          qphyent->CalculateFidelity ({"qa", "qd"}, measuredFidelity);
        });

    Simulator::Run ();

    const double waitMs = waitSeconds * 1000.0;
    const double tauMs = coherenceTimeSeconds * 1000.0;
    BellDiagonalState waitedLeft =
        ApplyPhaseFlipMemoryWait (MakeWernerState (leftFidelity), waitMs, tauMs, waitMs, tauMs);
    BellDiagonalState waitedRight =
        ApplyPhaseFlipMemoryWait (MakeWernerState (rightFidelity), waitMs, tauMs, waitMs, tauMs);
    BellDiagonalState expectedState = EntanglementSwapBellDiagonal (waitedLeft, waitedRight);

    NS_TEST_ASSERT_MSG_EQ_TOL (
        measuredFidelity,
        GetBellFidelity (expectedState),
        1e-9,
        "Wait-then-swap fidelity no longer matches the Bell-diagonal wait-plus-swap formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class PhaseFlipPairDephasingFormulaTestCase : public TestCase
{
public:
  PhaseFlipPairDephasingFormulaTestCase ()
      : TestCase ("Phase-flip Bell pairs after waiting should follow the phase-flip Bell-diagonal formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double coherenceTimeSeconds = 0.1;
    const double waitSeconds = 0.01;
    const double initialFidelity = 0.9;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->SetTimeModel ("God", coherenceTimeSeconds);
    qphyent->GenerateQubitsMixed (
        "God",
        GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, initialFidelity),
        {"qa", "qb"});

    Simulator::Schedule (Seconds (waitSeconds),
                         &SampleFidelity,
                         qphyent,
                         std::string ("qa"),
                         std::string ("qb"),
                         &measuredFidelity);

    Simulator::Run ();

    BellDiagonalState expectedState =
        ApplyPhaseFlipMemoryWait (MakePhaseFlipState (initialFidelity),
                                  waitSeconds * 1000.0,
                                  coherenceTimeSeconds * 1000.0,
                                  waitSeconds * 1000.0,
                                  coherenceTimeSeconds * 1000.0);

    NS_TEST_ASSERT_MSG_EQ_TOL (
        measuredFidelity,
        GetBellFidelity (expectedState),
        1e-9,
        "Phase-flip single-pair fidelity no longer matches the Bell-diagonal wait formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class PhaseFlipSwapFormulaTestCase : public TestCase
{
public:
  PhaseFlipSwapFormulaTestCase ()
      : TestCase ("Phase-flip entanglement swapping should follow the Bell-diagonal swap formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double leftFidelity = 0.93;
    const double rightFidelity = 0.88;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->GenerateQubitsMixed (
        "God",
        GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, leftFidelity),
        {"qa", "qb"});
    qphyent->GenerateQubitsMixed (
        "God",
        GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, rightFidelity),
        {"qc", "qd"});

    qphyent->ApplyGate ("God",
                        QNS_GATE_PREFIX + "CNOT",
                        std::vector<std::complex<double>>{},
                        std::vector<std::string>{"qc", "qb"});
    qphyent->ApplyGate ("God",
                        QNS_GATE_PREFIX + "H",
                        std::vector<std::complex<double>>{},
                        std::vector<std::string>{"qb"});

    auto outcomeQb = qphyent->Measure ("God", {"qb"});
    auto outcomeQc = qphyent->Measure ("God", {"qc"});

    if (outcomeQc.first == 1)
      {
        qphyent->ApplyGate ("God",
                            QNS_GATE_PREFIX + "PX",
                            std::vector<std::complex<double>>{},
                            std::vector<std::string>{"qd"});
      }
    if (outcomeQb.first == 1)
      {
        qphyent->ApplyGate ("God",
                            QNS_GATE_PREFIX + "PZ",
                            std::vector<std::complex<double>>{},
                            std::vector<std::string>{"qd"});
      }

    qphyent->PartialTrace ({"qb", "qc"});
    qphyent->CalculateFidelity ({"qa", "qd"}, measuredFidelity);

    BellDiagonalState expectedState =
        EntanglementSwapBellDiagonal (MakePhaseFlipState (leftFidelity), MakePhaseFlipState (rightFidelity));

    NS_TEST_ASSERT_MSG_EQ_TOL (measuredFidelity,
                               GetBellFidelity (expectedState),
                               1e-9,
                               "Phase-flip swapped-pair fidelity no longer matches the Bell-diagonal swap formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class PhaseFlipWaitThenSwapFormulaTestCase : public TestCase
{
public:
  PhaseFlipWaitThenSwapFormulaTestCase ()
      : TestCase ("Phase-flip waiting and then swapping should follow the Bell-diagonal wait-plus-swap formula")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    const double coherenceTimeSeconds = 0.1;
    const double waitSeconds = 0.01;
    const double leftFidelity = 0.93;
    const double rightFidelity = 0.88;
    double measuredFidelity = 0.0;

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    qphyent->SetTimeModel ("God", coherenceTimeSeconds);
    qphyent->GenerateQubitsMixed (
        "God",
        GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, leftFidelity),
        {"qa", "qb"});
    qphyent->GenerateQubitsMixed (
        "God",
        GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, rightFidelity),
        {"qc", "qd"});

    Simulator::Schedule (
        Seconds (waitSeconds),
        [qphyent, &measuredFidelity]() {
          qphyent->ApplyGate ("God",
                              QNS_GATE_PREFIX + "CNOT",
                              std::vector<std::complex<double>>{},
                              std::vector<std::string>{"qc", "qb"});
          qphyent->ApplyGate ("God",
                              QNS_GATE_PREFIX + "H",
                              std::vector<std::complex<double>>{},
                              std::vector<std::string>{"qb"});

          auto outcomeQb = qphyent->Measure ("God", {"qb"});
          auto outcomeQc = qphyent->Measure ("God", {"qc"});

          if (outcomeQc.first == 1)
            {
              qphyent->ApplyGate ("God",
                                  QNS_GATE_PREFIX + "PX",
                                  std::vector<std::complex<double>>{},
                                  std::vector<std::string>{"qd"});
            }
          if (outcomeQb.first == 1)
            {
              qphyent->ApplyGate ("God",
                                  QNS_GATE_PREFIX + "PZ",
                                  std::vector<std::complex<double>>{},
                                  std::vector<std::string>{"qd"});
            }

          qphyent->PartialTrace ({"qb", "qc"});
          qphyent->CalculateFidelity ({"qa", "qd"}, measuredFidelity);
        });

    Simulator::Run ();

    const double waitMs = waitSeconds * 1000.0;
    const double tauMs = coherenceTimeSeconds * 1000.0;
    BellDiagonalState waitedLeft =
        ApplyPhaseFlipMemoryWait (MakePhaseFlipState (leftFidelity), waitMs, tauMs, waitMs, tauMs);
    BellDiagonalState waitedRight =
        ApplyPhaseFlipMemoryWait (MakePhaseFlipState (rightFidelity), waitMs, tauMs, waitMs, tauMs);
    BellDiagonalState expectedState = EntanglementSwapBellDiagonal (waitedLeft, waitedRight);

    NS_TEST_ASSERT_MSG_EQ_TOL (
        measuredFidelity,
        GetBellFidelity (expectedState),
        1e-9,
        "Phase-flip wait-then-swap fidelity no longer matches the Bell-diagonal wait-plus-swap formula");

    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class PhaseFlipLinkLayerStateTestCase : public TestCase
{
public:
  PhaseFlipLinkLayerStateTestCase ()
      : TestCase ("Link layer should create phase-flip Bell-diagonal pairs when configured")
  {
  }

private:
  void DoRun () override
  {
    Simulator::Destroy ();

    Ptr<QuantumPhyEntity> qphyent =
        CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God", "A", "B"});
    Ptr<QuantumLinkLayerService> linkLayer = CreateObject<QuantumLinkLayerService> ();
    linkLayer->SetPhyEntity (qphyent);

    linkLayer->AddOrUpdateLink (
        "A",
        "B",
        MakeLinkMetrics (0.8, 1.0, 0.0, 0.0, 0.0, true, BellPairNoiseFamily::PHASE_FLIP));

    EntanglementId entId =
        linkLayer->RequestEntanglement ("A",
                                        "B",
                                        0.5,
                                        MakeCallback (&NoopEntanglementCallback));

    Simulator::Run ();

    NS_TEST_ASSERT_MSG_EQ (linkLayer->IsEntanglementReady (entId),
                           true,
                           "Configured phase-flip link should establish entanglement");

    EntanglementInfo info = linkLayer->GetEntanglementInfo (entId);
    std::vector<std::complex<double>> dm;
    qphyent->PeekDM ("God", {info.localQubit, info.remoteQubit}, dm);

    NS_TEST_ASSERT_MSG_EQ (dm.size (), 16u, "Expected a 4x4 density matrix");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[0].real (), 0.5, 1e-12, "Phase-flip state should keep |00><00| at 1/2");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[15].real (), 0.5, 1e-12, "Phase-flip state should keep |11><11| at 1/2");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[3].real (), 0.3, 1e-12, "Phase-flip coherence should match 2F-1 over 2");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[12].real (), 0.3, 1e-12, "Phase-flip coherence should be symmetric");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[5].real (), 0.0, 1e-12, "Phase-flip model should not populate |01>");
    NS_TEST_ASSERT_MSG_EQ_TOL (dm[10].real (), 0.0, 1e-12, "Phase-flip model should not populate |10>");

    linkLayer->Dispose ();
    qphyent->Dispose ();
    Simulator::Destroy ();
  }
};

class BellDiagonalRoutingMetricTestCase : public TestCase
{
public:
  BellDiagonalRoutingMetricTestCase ()
      : TestCase ("Bottleneck fidelity routing metric should use Bell-diagonal wait and swap formulas")
  {
  }

private:
  void DoRun () override
  {
    Ptr<QuantumPhyEntity> qphyent =
        CreateObject<QuantumPhyEntity> (std::vector<std::string>{"S", "A", "D"});
    qphyent->SetTimeModel ("S", 0.1);
    qphyent->SetTimeModel ("A", 0.1);
    qphyent->SetTimeModel ("D", 0.1);

    Ptr<QuantumNetworkLayer> networkLayer = CreateObject<QuantumNetworkLayer> ();
    networkLayer->SetPhyEntity (qphyent);

    Ptr<BottleneckFidelityRoutingMetric> metric = CreateObject<BottleneckFidelityRoutingMetric> ();
    QuantumRoutingLabel start = metric->CreateInitialLabel ("S");

    const double leftFidelity = 0.9;
    const double rightFidelity = 0.88;

    QuantumRoutingLabel prefix;
    const bool addedPrefix =
        metric->ExtendLabel (PeekPointer (networkLayer),
                             start,
                             "A",
                             MakeLinkMetrics (leftFidelity, 1.0, 0.0, 5.0, 0.0),
                             prefix);
    NS_TEST_ASSERT_MSG_EQ (addedPrefix, true, "Failed to add the first routing hop");

    QuantumRoutingLabel extended;
    const bool addedSecond =
        metric->ExtendLabel (PeekPointer (networkLayer),
                             prefix,
                             "D",
                             MakeLinkMetrics (rightFidelity, 1.0, 0.0, 15.0, 0.0),
                             extended);
    NS_TEST_ASSERT_MSG_EQ (addedSecond, true, "Failed to add the second routing hop");

    BellDiagonalState waitedPrefix =
        ApplyPhaseFlipMemoryWait (MakeWernerState (leftFidelity), 10.0, 100.0, 10.0, 100.0);
    BellDiagonalState expectedState =
        EntanglementSwapBellDiagonal (waitedPrefix, MakeWernerState (rightFidelity));

    NS_TEST_ASSERT_MSG_EQ_TOL (metric->GetScore (extended),
                               GetBellFidelity (expectedState),
                               1e-12,
                               "Routing metric score should follow the Bell-diagonal wait-plus-swap formula");

    networkLayer->Dispose ();
    qphyent->Dispose ();
  }
};

class PhaseFlipRoutingMetricTestCase : public TestCase
{
public:
  PhaseFlipRoutingMetricTestCase ()
      : TestCase ("Routing metric should switch to phase-flip Bell-diagonal modeling when requested")
  {
  }

private:
  void DoRun () override
  {
    Ptr<QuantumPhyEntity> qphyent =
        CreateObject<QuantumPhyEntity> (std::vector<std::string>{"S", "A", "D"});

    Ptr<QuantumNetworkLayer> networkLayer = CreateObject<QuantumNetworkLayer> ();
    networkLayer->SetPhyEntity (qphyent);

    Ptr<BottleneckFidelityRoutingMetric> metric = CreateObject<BottleneckFidelityRoutingMetric> ();
    QuantumRoutingLabel start = metric->CreateInitialLabel ("S");

    const double leftFidelity = 0.8;
    const double rightFidelity = 0.9;

    QuantumRoutingLabel prefix;
    const bool addedPrefix =
        metric->ExtendLabel (PeekPointer (networkLayer),
                             start,
                             "A",
                             MakeLinkMetrics (leftFidelity,
                                              1.0,
                                              0.0,
                                              0.0,
                                              0.0,
                                              true,
                                              BellPairNoiseFamily::PHASE_FLIP),
                             prefix);
    NS_TEST_ASSERT_MSG_EQ (addedPrefix, true, "Failed to add the first phase-flip hop");

    QuantumRoutingLabel extended;
    const bool addedSecond =
        metric->ExtendLabel (PeekPointer (networkLayer),
                             prefix,
                             "D",
                             MakeLinkMetrics (rightFidelity,
                                              1.0,
                                              0.0,
                                              0.0,
                                              0.0,
                                              true,
                                              BellPairNoiseFamily::PHASE_FLIP),
                             extended);
    NS_TEST_ASSERT_MSG_EQ (addedSecond, true, "Failed to add the second phase-flip hop");

    BellDiagonalState expectedState =
        EntanglementSwapBellDiagonal (MakePhaseFlipState (leftFidelity),
                                      MakePhaseFlipState (rightFidelity));
    const double expectedFidelity = GetBellFidelity (expectedState);
    const double wernerBaseline = GetBellFidelity (
        EntanglementSwapBellDiagonal (MakeWernerState (leftFidelity), MakeWernerState (rightFidelity)));

    NS_TEST_ASSERT_MSG_EQ_TOL (metric->GetScore (extended),
                               expectedFidelity,
                               1e-12,
                               "Phase-flip metric score should follow the phase-flip swap rule");
    NS_TEST_ASSERT_MSG_GT (expectedFidelity,
                           wernerBaseline,
                           "Phase-flip and Werner modeling should not collapse to the same score");

    networkLayer->Dispose ();
    qphyent->Dispose ();
  }
};

class SlicedDijkstraLabelRetentionTestCase : public TestCase
{
public:
  SlicedDijkstraLabelRetentionTestCase ()
      : TestCase ("Sliced Dijkstra should keep multiple labels for different Tmax slices")
  {
  }

private:
  void DoRun () override
  {
    Ptr<DijkstraRoutingProtocol> singleLabel = CreateObject<DijkstraRoutingProtocol> ();
    singleLabel->Initialize ();

    Ptr<SlicedDijkstraRoutingProtocol> sliced = CreateObject<SlicedDijkstraRoutingProtocol> ();
    sliced->SetAttribute ("K", UintegerValue (4));
    sliced->SetAttribute ("BucketWidthMs", DoubleValue (10.0));
    sliced->SetAttribute ("UseBuckets", BooleanValue (true));
    sliced->Initialize ();

    const LinkMetrics fastModerate1 = MakeLinkMetrics (0.94, 1.0, 0.0, 5.0, 0.0);
    const LinkMetrics fastModerate2 = MakeLinkMetrics (0.94, 1.0, 0.0, 5.0, 0.0);
    const LinkMetrics slowStrong1 = MakeLinkMetrics (0.96, 1.0, 0.0, 50.0, 0.0);
    const LinkMetrics slowStrong2 = MakeLinkMetrics (0.96, 1.0, 0.0, 50.0, 0.0);
    const LinkMetrics tail = MakeLinkMetrics (0.99, 1.0, 0.0, 5.0, 0.0);

    for (Ptr<QuantumRoutingProtocol> protocol : {Ptr<QuantumRoutingProtocol> (singleLabel),
                                                 Ptr<QuantumRoutingProtocol> (sliced)})
      {
        protocol->AddNeighbor ("S", "A", slowStrong1);
        protocol->AddNeighbor ("A", "X", slowStrong2);
        protocol->AddNeighbor ("S", "B", fastModerate1);
        protocol->AddNeighbor ("B", "X", fastModerate2);
        protocol->AddNeighbor ("X", "D", tail);
      }

    std::vector<std::string> bestSingle = singleLabel->CalculateRoute ("S", "D");
    NS_TEST_ASSERT_MSG_EQ (bestSingle.size (), 4u, "Single-label Dijkstra should find a valid path");
    NS_TEST_ASSERT_MSG_EQ (bestSingle[1],
                           std::string ("A"),
                           "Single-label Dijkstra should keep only the stronger prefix");

    sliced->CalculateRoute ("S", "D");
    std::vector<QuantumRoutingLabel> labelsAtX = sliced->GetNodeLabels ("X");
    NS_TEST_ASSERT_MSG_EQ (labelsAtX.size (),
                           2u,
                           "Sliced Dijkstra should retain two labels at X for distinct Tmax buckets");

    std::vector<int64_t> observedBuckets;
    for (const auto& label : labelsAtX)
      {
        observedBuckets.push_back (static_cast<int64_t> (GetScalar (label, "t_max_ms")));
      }
    std::sort (observedBuckets.begin (), observedBuckets.end ());

    NS_TEST_ASSERT_MSG_EQ (observedBuckets[0], 5, "Fast branch should retain a low-Tmax label");
    NS_TEST_ASSERT_MSG_EQ (observedBuckets[1], 50, "Slow branch should retain a high-Tmax label");

    singleLabel->Dispose ();
    sliced->Dispose ();
  }
};

class SlicedDijkstraOutperformsSingleLabelTestCase : public TestCase
{
public:
  SlicedDijkstraOutperformsSingleLabelTestCase ()
      : TestCase ("Sliced Dijkstra should beat single-label Dijkstra on a Tmax non-monotonic topology")
  {
  }

private:
  void DoRun () override
  {
    const std::vector<std::string> owners = {"S", "A", "B", "X", "D"};
    const std::map<std::string, double> coherenceTimeSeconds = {
        {"S", 1000.0}, {"A", 1000.0}, {"B", 1000.0}, {"X", 0.01}, {"D", 1000.0}};
    RoutingMetricContext ctx = BuildRoutingMetricContext (owners, coherenceTimeSeconds);

    Ptr<DijkstraRoutingProtocol> singleLabel = CreateObject<DijkstraRoutingProtocol> ();
    singleLabel->SetNetworkLayer (PeekPointer (ctx.netLayer));
    singleLabel->Initialize ();

    Ptr<SlicedDijkstraRoutingProtocol> sliced = CreateObject<SlicedDijkstraRoutingProtocol> ();
    sliced->SetNetworkLayer (PeekPointer (ctx.netLayer));
    sliced->SetAttribute ("K", UintegerValue (4));
    sliced->SetAttribute ("BucketWidthMs", DoubleValue (10.0));
    sliced->SetAttribute ("UseBuckets", BooleanValue (true));
    sliced->Initialize ();

    std::map<std::string, std::map<std::string, LinkMetrics>> topology;
    auto addDirectedLink = [&] (const std::string& left,
                                const std::string& right,
                                const LinkMetrics& metrics) {
      topology[left][right] = metrics;
      singleLabel->AddNeighbor (left, right, metrics);
      sliced->AddNeighbor (left, right, metrics);
    };

    const LinkMetrics slowStrong = MakeLinkMetrics (0.97, 1.0, 0.0, 50.0, 0.0);
    const LinkMetrics fastModerate = MakeLinkMetrics (0.94, 1.0, 0.0, 5.0, 0.0);
    const LinkMetrics tail = MakeLinkMetrics (0.99, 1.0, 0.0, 5.0, 0.0);

    addDirectedLink ("S", "A", slowStrong);
    addDirectedLink ("A", "X", slowStrong);
    addDirectedLink ("S", "B", fastModerate);
    addDirectedLink ("B", "X", fastModerate);
    addDirectedLink ("X", "D", tail);

    std::vector<std::string> singleRoute = singleLabel->CalculateRoute ("S", "D");
    std::vector<std::string> slicedRoute = sliced->CalculateRoute ("S", "D");

    NS_TEST_ASSERT_MSG_EQ (singleRoute.size (), 4u, "Single-label Dijkstra should find a 3-hop route");
    NS_TEST_ASSERT_MSG_EQ (slicedRoute.size (), 4u, "Sliced Dijkstra should find a 3-hop route");
    NS_TEST_ASSERT_MSG_EQ (singleRoute[1],
                           std::string ("A"),
                           "Single-label Dijkstra should keep the stronger but slower prefix");
    NS_TEST_ASSERT_MSG_EQ (slicedRoute[1],
                           std::string ("B"),
                           "Sliced Dijkstra should recover the faster prefix in a different Tmax slice");

    const double singlePredicted = singleLabel->GetRouteMetric ("S", "D");
    const double slicedPredicted = sliced->GetRouteMetric ("S", "D");
    NS_TEST_ASSERT_MSG_GT (slicedPredicted,
                           singlePredicted,
                           "Sliced Dijkstra should predict a better final fidelity than the single-label baseline");

    const double singleActual = SimulateRouteActualFidelity (singleRoute, topology, coherenceTimeSeconds);
    const double slicedActual = SimulateRouteActualFidelity (slicedRoute, topology, coherenceTimeSeconds);
    NS_TEST_ASSERT_MSG_GT (slicedActual,
                           singleActual,
                           "Sliced Dijkstra should realize a higher simulated end-to-end fidelity");

    singleLabel->Dispose ();
    sliced->Dispose ();
    ctx.netLayer->Dispose ();
    ctx.qphyent->Dispose ();
  }
};

class NetworkLayerReadyAfterFinalSwapTestCase : public TestCase
{
public:
  NetworkLayerReadyAfterFinalSwapTestCase ()
      : TestCase ("Network layer should become READY only after swap signaling and fidelity check"),
        m_callbackSeen (false),
        m_callbackSuccess (false),
        m_callbackId (INVALID_PATH_ID),
        m_callbackTime (Seconds (0))
  {
  }

private:
  void OnPathReady (PathId pathId, bool success)
  {
    m_callbackSeen = true;
    m_callbackSuccess = success;
    m_callbackId = pathId;
    m_callbackTime = Simulator::Now ();
  }

  void DoRun () override
  {
    LinearNetworkContext ctx = BuildLinearNetwork (1.0, 5.0, 5.0, 0);

    PathId pathId = ctx.netLayers[0]->SetupPath (
        "A",
        "C",
        0.5,
        MakeCallback (&NetworkLayerReadyAfterFinalSwapTestCase::OnPathReady, this));

    PathState stateBeforeReady = PathState::FAILED;
    bool readyBeforeReady = true;
    double fidelityBeforeReady = -1.0;
    Simulator::Schedule (MilliSeconds (12),
                         &CapturePathSnapshot,
                         ctx.netLayers[0],
                         pathId,
                         &stateBeforeReady,
                         &readyBeforeReady,
                         &fidelityBeforeReady);

    Simulator::Stop (MilliSeconds (50));
    Simulator::Run ();

    NS_TEST_ASSERT_MSG_EQ (readyBeforeReady,
                           false,
                           "Path should not be READY before swap results arrive");
    NS_TEST_ASSERT_MSG_EQ (static_cast<int> (stateBeforeReady),
                           static_cast<int> (PathState::PENDING),
                           "Path should remain pending until final swap completion");
    NS_TEST_ASSERT_MSG_EQ (m_callbackSeen, true, "Path callback should have been invoked");
    NS_TEST_ASSERT_MSG_EQ (m_callbackSuccess, true, "Path setup should succeed for perfect links");
    NS_TEST_ASSERT_MSG_EQ (m_callbackId, pathId, "Callback should report the created path id");
    const bool callbackDelayedEnough = (m_callbackTime >= MilliSeconds (15));
    NS_TEST_ASSERT_MSG_EQ (callbackDelayedEnough,
                           true,
                           "READY callback fired before entanglement setup and signaling delay elapsed");

    PathInfo info = ctx.netLayers[0]->GetPathInfo (pathId);
    NS_TEST_ASSERT_MSG_EQ (static_cast<int> (info.state),
                           static_cast<int> (PathState::READY),
                           "Successful path should be marked READY");
    NS_TEST_ASSERT_MSG_EQ (ctx.netLayers[0]->IsPathReady (pathId), true, "READY path must be queryable");
    const bool fidelityAccepted = (info.actualFidelity + 1e-12 >= info.minFidelity);
    NS_TEST_ASSERT_MSG_EQ (fidelityAccepted,
                           true,
                           "READY path should satisfy the requested fidelity threshold");

    DisposeLinearNetwork (ctx.netLayers, ctx.linkLayer, ctx.qphyent);
    Simulator::Destroy ();
  }

  bool m_callbackSeen;
  bool m_callbackSuccess;
  PathId m_callbackId;
  Time m_callbackTime;
};

class NetworkLayerMinFidelityFailureTestCase : public TestCase
{
public:
  NetworkLayerMinFidelityFailureTestCase ()
      : TestCase ("Network layer should fail a path whose final fidelity misses minFidelity"),
        m_callbackSeen (false),
        m_callbackSuccess (false)
  {
  }

private:
  void OnPathReady (PathId, bool success)
  {
    m_callbackSeen = true;
    m_callbackSuccess = success;
  }

  void DoRun () override
  {
    LinearNetworkContext ctx = BuildLinearNetwork (0.6, 5.0, 5.0, 0);

    PathId pathId = ctx.netLayers[0]->SetupPath (
        "A",
        "C",
        0.9,
        MakeCallback (&NetworkLayerMinFidelityFailureTestCase::OnPathReady, this));

    Simulator::Stop (MilliSeconds (50));
    Simulator::Run ();

    NS_TEST_ASSERT_MSG_EQ (m_callbackSeen, true, "Failure path should still notify the application");
    NS_TEST_ASSERT_MSG_EQ (m_callbackSuccess,
                           false,
                           "Path should fail when final fidelity is below minFidelity");

    PathInfo info = ctx.netLayers[0]->GetPathInfo (pathId);
    NS_TEST_ASSERT_MSG_EQ (static_cast<int> (info.state),
                           static_cast<int> (PathState::FAILED),
                           "Path should finish in FAILED state");
    NS_TEST_ASSERT_MSG_EQ (ctx.netLayers[0]->IsPathReady (pathId), false, "Failed path cannot be READY");
    const bool measuredFinalFidelity = (info.actualFidelity > 0.0);
    NS_TEST_ASSERT_MSG_EQ (measuredFinalFidelity,
                           true,
                           "Failure should happen after computing a real end-to-end fidelity");
    const bool fidelityExplainsFailure = (info.actualFidelity + 1e-12 < info.minFidelity);
    NS_TEST_ASSERT_MSG_EQ (fidelityExplainsFailure,
                           true,
                           "Recorded final fidelity should explain the failure");

    DisposeLinearNetwork (ctx.netLayers, ctx.linkLayer, ctx.qphyent);
    Simulator::Destroy ();
  }

  bool m_callbackSeen;
  bool m_callbackSuccess;
};

class QuantumTestSuite : public TestSuite
{
public:
  QuantumTestSuite ()
      : TestSuite ("quantum", Type::UNIT)
  {
    AddTestCase (new QuantumBasisSmokeTestCase, Duration::QUICK);
    AddTestCase (new AutoDecoherenceFidelityQueryTestCase, Duration::QUICK);
    AddTestCase (new WernerPairDephasingFormulaTestCase, Duration::QUICK);
    AddTestCase (new WernerSwapFormulaTestCase, Duration::QUICK);
    AddTestCase (new WernerWaitThenSwapFormulaTestCase, Duration::QUICK);
    AddTestCase (new PhaseFlipPairDephasingFormulaTestCase, Duration::QUICK);
    AddTestCase (new PhaseFlipSwapFormulaTestCase, Duration::QUICK);
    AddTestCase (new PhaseFlipWaitThenSwapFormulaTestCase, Duration::QUICK);
    AddTestCase (new PhaseFlipLinkLayerStateTestCase, Duration::QUICK);
    AddTestCase (new BellDiagonalRoutingMetricTestCase, Duration::QUICK);
    AddTestCase (new PhaseFlipRoutingMetricTestCase, Duration::QUICK);
    AddTestCase (new CustomMetricRoutingTestCase, Duration::QUICK);
    AddTestCase (new SlicedCustomMetricRoutingTestCase, Duration::QUICK);
    AddTestCase (new SlicedDijkstraLabelRetentionTestCase, Duration::QUICK);
    AddTestCase (new SlicedDijkstraOutperformsSingleLabelTestCase, Duration::QUICK);
    AddTestCase (new NetworkLayerReadyAfterFinalSwapTestCase, Duration::QUICK);
    AddTestCase (new NetworkLayerMinFidelityFailureTestCase, Duration::QUICK);
  }
};

static QuantumTestSuite s_quantumTestSuite;
