/*
 * dijkstra-vs-sliced-dijkstra.cc
 *
 * Fixed prefix-trap topology for comparing:
 *   - DijkstraRoutingProtocol            (single-label, K=1 baseline)
 *   - SlicedDijkstraRoutingProtocol      (multi-label with Tmax buckets)
 *
 * Scenario requested by the user:
 *   - Conceptual nodes: A, B, C
 *   - A->B has two candidate prefixes:
 *       1. Direct link: fidelity 0.98, setup 60 ms
 *       2. Two-hop path A->X1->B: each link fidelity 0.97, setup 50 ms
 *   - This example also includes a slower/stronger comparison branch
 *       A->X2->B: each link fidelity 0.995, setup 70 ms
 *   - B->C has one three-hop suffix:
 *       B->Y1->Y2->C: each link fidelity 0.99, setup 50 ms
 *   - All node coherence times: 100 ms
 *
 * Under the current metric semantics, all links start generating EPR pairs
 * simultaneously, fast links wait until the path-wide Tmax, and swapping is
 * ideal/instantaneous after all links are ready. This creates a prefix trap:
 * the direct A->B prefix looks better locally at B, but after appending the
 * shared B->C suffix it becomes worse end-to-end than the faster A->X1->B
 * prefix. Single-label Dijkstra drops the faster prefix at B; Sliced Dijkstra
 * keeps both Tmax slices and recovers the better final route.
 *
 * Usage:
 *   ./ns3 run dijkstra-vs-sliced-dijkstra
 *   ./ns3 run 'dijkstra-vs-sliced-dijkstra --coherenceMs=100 --bucketWidthMs=10 --k=4'
 *   ./ns3 run 'dijkstra-vs-sliced-dijkstra --swapSignalDelayMs=1'
 */

#include "ns3/boolean.h"
#include "ns3/core-module.h"
#include "ns3/dijkstra-routing-protocol.h"
#include "ns3/double.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-routing-metric.h"
#include "ns3/sliced-dijkstra-routing-protocol.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <complex>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

NS_LOG_COMPONENT_DEFINE("DijkstraVsSlicedDijkstra");

using namespace ns3;

namespace
{

using Topology = std::map<std::string, std::map<std::string, LinkMetrics>>;

LinkMetrics
MakeLinkMetrics(double initialFidelity,
                double quantumSetupTimeMs,
                double successRate = 1.0,
                BellPairNoiseFamily noiseFamily = BellPairNoiseFamily::WERNER)
{
    LinkMetrics metrics;
    metrics.fidelity = initialFidelity;
    metrics.initialFidelity = initialFidelity;
    metrics.noiseFamily = noiseFamily;
    metrics.successRate = successRate;
    metrics.latency = quantumSetupTimeMs;
    metrics.quantumSetupTimeMs = quantumSetupTimeMs;
    metrics.classicalControlDelayMs = 0.0;
    metrics.isAvailable = true;
    return metrics;
}

double
GetScalar(const QuantumRoutingLabel& label, const std::string& key, double fallback = 0.0)
{
    auto it = label.scalars.find(key);
    if (it == label.scalars.end())
    {
        return fallback;
    }
    return it->second;
}

std::string
RouteToString(const std::vector<std::string>& route)
{
    std::string result;
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (i > 0)
        {
            result += " -> ";
        }
        result += route[i];
    }
    return result;
}

void
AddBidirectionalLink(Topology& topology,
                     const std::string& left,
                     const std::string& right,
                     const LinkMetrics& metrics)
{
    topology[left][right] = metrics;
    topology[right][left] = metrics;
}

double
ResolveSetupTimeMs(const LinkMetrics& metrics)
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

bool
BuildLabelForRoute(Ptr<QuantumRoutingMetric> metric,
                   QuantumNetworkLayer* networkLayer,
                   const Topology& topology,
                   const std::vector<std::string>& route,
                   QuantumRoutingLabel& label)
{
    if (metric == nullptr || route.empty())
    {
        return false;
    }

    label = metric->CreateInitialLabel(route.front());
    for (size_t i = 0; i + 1 < route.size(); ++i)
    {
        auto srcIt = topology.find(route[i]);
        if (srcIt == topology.end())
        {
            return false;
        }

        auto dstIt = srcIt->second.find(route[i + 1]);
        if (dstIt == srcIt->second.end())
        {
            return false;
        }

        QuantumRoutingLabel extended;
        if (!metric->ExtendLabel(networkLayer, label, route[i + 1], dstIt->second, extended))
        {
            return false;
        }
        label = extended;
    }

    return true;
}

struct SwapChainState
{
    Ptr<QuantumPhyEntity> qphyent;
    std::vector<std::string> route;
    std::vector<std::string> leftQubits;
    std::vector<std::string> rightQubits;
    std::string leftEndpoint;
    double swapSignalDelayMs;
    double* measuredFidelity;
};

void
ExecuteSwapStep(const std::shared_ptr<SwapChainState>& state,
                uint32_t segIndex,
                const std::string& carriedRightEndpoint)
{
    const std::string owner = state->route.at(segIndex);
    const std::string rightOwner = state->route.at(segIndex + 1);
    const std::string localRightSegmentQubit = state->leftQubits.at(segIndex);
    const std::string farRightQubit = state->rightQubits.at(segIndex);

    state->qphyent->ApplyGate(owner,
                              QNS_GATE_PREFIX + "CNOT",
                              std::vector<std::complex<double>>{},
                              {localRightSegmentQubit, carriedRightEndpoint});
    state->qphyent->ApplyGate(owner,
                              QNS_GATE_PREFIX + "H",
                              std::vector<std::complex<double>>{},
                              {carriedRightEndpoint});

    auto outcomeLeft = state->qphyent->Measure(owner, {carriedRightEndpoint});
    auto outcomeRight = state->qphyent->Measure(owner, {localRightSegmentQubit});
    state->qphyent->PartialTrace({carriedRightEndpoint, localRightSegmentQubit});

    // PartialTrace invalidates these measured qubits in the tensor state, but the
    // current memory bookkeeping keeps their names until they are removed explicitly.
    // Without this cleanup, later EnsureAllDecoherence() calls may try to decohere
    // already-traced qubits once simulated time advances between swap stages.
    state->qphyent->GetNode(owner)->RemoveQubit(carriedRightEndpoint);
    state->qphyent->GetNode(owner)->RemoveQubit(localRightSegmentQubit);

    auto continueAfterSignal = [state, rightOwner, farRightQubit, segIndex, outcomeLeft, outcomeRight]() {
        if (outcomeRight.first == 1)
        {
            state->qphyent->ApplyGate(rightOwner,
                                      QNS_GATE_PREFIX + "PX",
                                      std::vector<std::complex<double>>{},
                                      {farRightQubit});
        }
        if (outcomeLeft.first == 1)
        {
            state->qphyent->ApplyGate(rightOwner,
                                      QNS_GATE_PREFIX + "PZ",
                                      std::vector<std::complex<double>>{},
                                      {farRightQubit});
        }

        if (segIndex + 1 < state->leftQubits.size())
        {
            ExecuteSwapStep(state, segIndex + 1, farRightQubit);
        }
        else
        {
            state->qphyent->CalculateFidelity({state->leftEndpoint, farRightQubit},
                                              *state->measuredFidelity);
        }
    };

    if (state->swapSignalDelayMs > 0.0)
    {
        Simulator::Schedule(MilliSeconds(state->swapSignalDelayMs), continueAfterSignal);
    }
    else
    {
        continueAfterSignal();
    }
}

double
SimulateRouteActualFidelity(const std::vector<std::string>& route,
                            const Topology& topology,
                            double coherenceMs,
                            double swapSignalDelayMs)
{
    if (route.size() < 2)
    {
        return 0.0;
    }

    Simulator::Destroy();

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(route);
    for (const auto& owner : route)
    {
        qphyent->SetTimeModel(owner, coherenceMs / 1000.0);
    }

    std::vector<std::string> leftQubits;
    std::vector<std::string> rightQubits;
    leftQubits.reserve(route.size() - 1);
    rightQubits.reserve(route.size() - 1);

    double tMaxMs = 0.0;
    for (uint32_t i = 0; i + 1 < route.size(); ++i)
    {
        const LinkMetrics& metrics = topology.at(route[i]).at(route[i + 1]);
        const double setupMs = ResolveSetupTimeMs(metrics);
        tMaxMs = std::max(tMaxMs, setupMs);

        const std::string leftQubit = "seg_" + std::to_string(i) + "_left";
        const std::string rightQubit = "seg_" + std::to_string(i) + "_right";
        leftQubits.push_back(leftQubit);
        rightQubits.push_back(rightQubit);

        Simulator::Schedule(MilliSeconds(setupMs),
                            [qphyent,
                             metrics,
                             leftOwner = route[i],
                             rightOwner = route[i + 1],
                             leftQubit,
                             rightQubit]() {
                                qphyent->GenerateQubitsMixed(
                                    leftOwner,
                                    GetEPRwithNoiseFamily(metrics.noiseFamily,
                                                          metrics.initialFidelity > 0.0
                                                              ? metrics.initialFidelity
                                                              : metrics.fidelity),
                                    {leftQubit, rightQubit});
                                qphyent->TransferQubit(leftOwner, rightOwner, rightQubit);
                            });
    }

    double measuredFidelity = 0.0;
    const std::string leftEndpoint = leftQubits.front();

    if (leftQubits.size() == 1)
    {
        Simulator::Schedule(MilliSeconds(tMaxMs),
                            [qphyent, leftEndpoint, rightEndpoint = rightQubits.front(), &measuredFidelity]() {
                                qphyent->CalculateFidelity({leftEndpoint, rightEndpoint},
                                                           measuredFidelity);
                            });
    }
    else
    {
        auto state = std::make_shared<SwapChainState>();
        state->qphyent = qphyent;
        state->route = route;
        state->leftQubits = leftQubits;
        state->rightQubits = rightQubits;
        state->leftEndpoint = leftEndpoint;
        state->swapSignalDelayMs = swapSignalDelayMs;
        state->measuredFidelity = &measuredFidelity;

        Simulator::Schedule(MilliSeconds(tMaxMs),
                            [state, firstRightQubit = rightQubits.front()]() {
                                ExecuteSwapStep(state, 1, firstRightQubit);
                            });
    }

    Simulator::Run();

    qphyent->Dispose();
    Simulator::Destroy();
    return measuredFidelity;
}

void
PrintTopologyDiagram(double coherenceMs)
{
    std::cout << "Conceptual topology (all nodes tau=" << coherenceMs << " ms)\n";
    std::cout << "\n";
    std::cout << "  A ==[F0=0.98, Tgen=60 ms]== B ==[F0=0.99, Tgen=50 ms]== Y1"
              << " ==[F0=0.99, Tgen=50 ms]== Y2 ==[F0=0.99, Tgen=50 ms]== C\n";
    std::cout << "  |\\                           \n";
    std::cout << "  | [F0=0.995, Tgen=70 ms]     \n";
    std::cout << "  |  \\                         \n";
    std::cout << "  |   X2 ==[F0=0.995, Tgen=70 ms]== B\n";
    std::cout << "  |                            \n";
    std::cout << "  [F0=0.97, Tgen=50 ms]        \n";
    std::cout << "  |                            \n";
    std::cout << "  X1 ==[F0=0.97, Tgen=50 ms]== B\n";
    std::cout << "\n";
    std::cout << "Candidate A->C routes:\n";
    std::cout << "  1. A -> B -> Y1 -> Y2 -> C\n";
    std::cout << "  2. A -> X1 -> B -> Y1 -> Y2 -> C\n";
    std::cout << "  3. A -> X2 -> B -> Y1 -> Y2 -> C\n";
    std::cout << std::endl;
}

void
PrintRouteAnalysis(const std::string& name,
                   Ptr<QuantumRoutingMetric> metric,
                   QuantumNetworkLayer* networkLayer,
                   const Topology& topology,
                   const std::vector<std::string>& route,
                   double actualFidelity,
                   double swapSignalDelayMs)
{
    QuantumRoutingLabel label;
    const bool ok = BuildLabelForRoute(metric, networkLayer, topology, route, label);

    std::cout << name << "\n";
    std::cout << "  path: " << RouteToString(route) << "\n";
    if (!ok)
    {
        std::cout << "  predicted fidelity: <failed to build label>\n";
        std::cout << "  actual fidelity:    " << actualFidelity << "\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  predicted fidelity: " << metric->GetScore(label) << "\n";
    std::cout << "  t_max_ms:           " << GetScalar(label, "t_max_ms") << "\n";
    std::cout << "  hop_count:          " << GetScalar(label, "hop_count") << "\n";
    std::cout << "  actual fidelity:    " << actualFidelity
              << " (with swapSignalDelayMs=" << swapSignalDelayMs << ")\n";
}

void
PrintNodeLabelList(const std::string& node, const std::vector<QuantumRoutingLabel>& labels)
{
    std::vector<QuantumRoutingLabel> sorted = labels;
    std::sort(sorted.begin(),
              sorted.end(),
              [](const QuantumRoutingLabel& lhs, const QuantumRoutingLabel& rhs) {
                  const double lhsTMax = GetScalar(lhs, "t_max_ms");
                  const double rhsTMax = GetScalar(rhs, "t_max_ms");
                  if (lhsTMax != rhsTMax)
                  {
                      return lhsTMax < rhsTMax;
                  }
                  return GetScalar(lhs, "predicted_fidelity") >
                         GetScalar(rhs, "predicted_fidelity");
              });

    std::cout << "  " << node << ":\n";
    if (sorted.empty())
    {
        std::cout << "    <no label>\n";
        return;
    }

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto& label = sorted[i];
        std::cout << std::fixed << std::setprecision(6) << "    [" << i << "] "
                  << RouteToString(label.path)
                  << " | predicted_fidelity=" << GetScalar(label, "predicted_fidelity", 1.0)
                  << " | t_max_ms=" << GetScalar(label, "t_max_ms")
                  << " | hop_count=" << GetScalar(label, "hop_count") << "\n";
    }
}

template <typename LabelGetter>
void
PrintProtocolLabels(const std::string& protocolName,
                    const std::vector<std::string>& nodes,
                    LabelGetter getLabels)
{
    std::cout << protocolName << " maintained labels by node:\n";
    for (const auto& node : nodes)
    {
        PrintNodeLabelList(node, getLabels(node));
    }
    std::cout << std::endl;
}

} // namespace

int
main(int argc, char* argv[])
{
    double coherenceMs = 100.0;
    double swapSignalDelayMs = 1.0;
    uint32_t k = 4;
    double bucketWidthMs = 10.0;
    bool useBuckets = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("coherenceMs", "Coherence time for every node in milliseconds.", coherenceMs);
    cmd.AddValue("swapSignalDelayMs",
                 "Classical signaling delay in milliseconds between consecutive swap stages in the"
                 " final physical validation only.",
                 swapSignalDelayMs);
    cmd.AddValue("k", "Maximum number of labels retained per node.", k);
    cmd.AddValue("bucketWidthMs", "Tmax bucket width for Sliced Dijkstra.", bucketWidthMs);
    cmd.AddValue("useBuckets", "Enable Tmax bucket slicing.", useBuckets);
    cmd.Parse(argc, argv);

    PrintTopologyDiagram(coherenceMs);

    const std::vector<std::string> owners = {"A", "X1", "X2", "B", "Y1", "Y2", "C"};
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
    for (const auto& owner : owners)
    {
        qphyent->SetTimeModel(owner, coherenceMs / 1000.0);
    }

    Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer>();
    netLayer->SetOwner("A");
    netLayer->SetPhyEntity(qphyent);

    Topology topology;
    AddBidirectionalLink(topology, "A", "B", MakeLinkMetrics(0.98, 60.0));
    AddBidirectionalLink(topology, "A", "X2", MakeLinkMetrics(0.995, 70.0));
    AddBidirectionalLink(topology, "X2", "B", MakeLinkMetrics(0.995, 70.0));
    AddBidirectionalLink(topology, "A", "X1", MakeLinkMetrics(0.97, 50.0));
    AddBidirectionalLink(topology, "X1", "B", MakeLinkMetrics(0.97, 50.0));
    AddBidirectionalLink(topology, "B", "Y1", MakeLinkMetrics(0.99, 50.0));
    AddBidirectionalLink(topology, "Y1", "Y2", MakeLinkMetrics(0.99, 50.0));
    AddBidirectionalLink(topology, "Y2", "C", MakeLinkMetrics(0.99, 50.0));

    Ptr<BottleneckFidelityRoutingMetric> metric = CreateObject<BottleneckFidelityRoutingMetric>();

    Ptr<DijkstraRoutingProtocol> dijkstra = CreateObject<DijkstraRoutingProtocol>();
    dijkstra->SetMetricModel(metric);
    dijkstra->SetNetworkLayer(PeekPointer(netLayer));
    dijkstra->Initialize();
    dijkstra->UpdateTopology(topology);

    Ptr<SlicedDijkstraRoutingProtocol> sliced = CreateObject<SlicedDijkstraRoutingProtocol>();
    sliced->SetMetricModel(metric);
    sliced->SetNetworkLayer(PeekPointer(netLayer));
    sliced->SetAttribute("K", UintegerValue(k));
    sliced->SetAttribute("BucketWidthMs", DoubleValue(bucketWidthMs));
    sliced->SetAttribute("UseBuckets", BooleanValue(useBuckets));
    sliced->Initialize();
    sliced->UpdateTopology(topology);

    const std::vector<std::string> directRoute = {"A", "B", "Y1", "Y2", "C"};
    const std::vector<std::string> fastRoute = {"A", "X1", "B", "Y1", "Y2", "C"};
    const std::vector<std::string> slowStrongRoute = {"A", "X2", "B", "Y1", "Y2", "C"};

    QuantumRoutingLabel directPrefix;
    QuantumRoutingLabel fastPrefix;
    QuantumRoutingLabel slowStrongPrefix;
    BuildLabelForRoute(metric, PeekPointer(netLayer), topology, {"A", "B"}, directPrefix);
    BuildLabelForRoute(metric, PeekPointer(netLayer), topology, {"A", "X1", "B"}, fastPrefix);
    BuildLabelForRoute(metric, PeekPointer(netLayer), topology, {"A", "X2", "B"}, slowStrongPrefix);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Prefix comparison at B:\n";
    std::cout << "  A -> B"
              << " | predicted_fidelity=" << metric->GetScore(directPrefix)
              << " | t_max_ms=" << GetScalar(directPrefix, "t_max_ms") << "\n";
    std::cout << "  A -> X1 -> B"
              << " | predicted_fidelity=" << metric->GetScore(fastPrefix)
              << " | t_max_ms=" << GetScalar(fastPrefix, "t_max_ms") << "\n";
    std::cout << "  A -> X2 -> B"
              << " | predicted_fidelity=" << metric->GetScore(slowStrongPrefix)
              << " | t_max_ms=" << GetScalar(slowStrongPrefix, "t_max_ms") << "\n";
    std::cout << "\n";

    const double directActual =
        SimulateRouteActualFidelity(directRoute, topology, coherenceMs, swapSignalDelayMs);
    const double fastActual =
        SimulateRouteActualFidelity(fastRoute, topology, coherenceMs, swapSignalDelayMs);
    const double slowStrongActual =
        SimulateRouteActualFidelity(slowStrongRoute, topology, coherenceMs, swapSignalDelayMs);

    std::cout << "Candidate route analysis:\n";
    PrintRouteAnalysis("Route 1: direct A-B prefix",
                       metric,
                       PeekPointer(netLayer),
                       topology,
                       directRoute,
                       directActual,
                       swapSignalDelayMs);
    std::cout << "\n";
    PrintRouteAnalysis("Route 2: fast A-X1-B prefix",
                       metric,
                       PeekPointer(netLayer),
                       topology,
                       fastRoute,
                       fastActual,
                       swapSignalDelayMs);
    std::cout << "\n";
    PrintRouteAnalysis("Route 3: slow-strong A-X2-B prefix",
                       metric,
                       PeekPointer(netLayer),
                       topology,
                       slowStrongRoute,
                       slowStrongActual,
                       swapSignalDelayMs);
    std::cout << "\n";

    std::vector<std::string> dijkstraRoute = dijkstra->CalculateRoute("A", "C");
    std::vector<std::string> slicedRoute = sliced->CalculateRoute("A", "C");

    std::cout << "Protocol comparison:\n";
    std::cout << "  Dijkstra route:        " << RouteToString(dijkstraRoute) << "\n";
    std::cout << "  Dijkstra metric:       " << dijkstra->GetRouteMetric("A", "C") << "\n";
    std::cout << "  Sliced Dijkstra route: " << RouteToString(slicedRoute) << "\n";
    std::cout << "  Sliced metric:         " << sliced->GetRouteMetric("A", "C") << "\n";
    std::cout << "\n";

    const bool dijkstraPicksSlowStrong = dijkstraRoute == slowStrongRoute;
    const bool slicedPicksFast = slicedRoute == fastRoute;

    std::cout << "Summary:\n";
    if (dijkstraPicksSlowStrong)
    {
        std::cout << "  Single-label Dijkstra keeps the locally strongest prefix and chooses the"
                  << " slower/stronger X2 branch.\n";
    }
    else
    {
        std::cout << "  Single-label Dijkstra did not choose the expected slow-strong X2 route.\n";
    }

    if (slicedPicksFast)
    {
        std::cout << "  Sliced Dijkstra keeps multiple Tmax slices at B and recovers the better"
                  << " fast X1 route.\n";
    }
    else
    {
        std::cout << "  Sliced Dijkstra did not choose the expected fast X1 route.\n";
    }

    if (dijkstraPicksSlowStrong && slicedPicksFast)
    {
        std::cout << "  The requested prefix-trap comparison is reproduced successfully.\n";
    }
    std::cout << "\n";

    PrintProtocolLabels("Dijkstra", owners, [dijkstra](const std::string& node) {
        return dijkstra->GetNodeLabels(node);
    });
    PrintProtocolLabels("Sliced Dijkstra", owners, [sliced](const std::string& node) {
        return sliced->GetNodeLabels(node);
    });

    sliced->Dispose();
    dijkstra->Dispose();
    netLayer->Dispose();
    qphyent->Dispose();
    Simulator::Destroy();
    return 0;
}
