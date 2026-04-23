#include "ns3/quantum-routing-metric.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/quantum-fidelity-model.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-phy-entity.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumRoutingMetric");

NS_OBJECT_ENSURE_REGISTERED (QuantumRoutingMetric);
NS_OBJECT_ENSURE_REGISTERED (BottleneckFidelityRoutingMetric);

namespace {

void
StoreBellState (QuantumRoutingLabel &label, const BellDiagonalState &state)
{
    label.scalars["bell_phi_plus"] = state.phiPlus;
    label.scalars["bell_phi_minus"] = state.phiMinus;
    label.scalars["bell_psi_plus"] = state.psiPlus;
    label.scalars["bell_psi_minus"] = state.psiMinus;
}

void
EvaluateSegments (const std::vector<QuantumRoutingSegment> &segments,
                  BellDiagonalState &predictedState,
                  double &tMaxMs,
                  double &logInitialFidelity)
{
    predictedState = {1.0, 0.0, 0.0, 0.0};
    tMaxMs = 0.0;
    logInitialFidelity = 0.0;

    if (segments.empty ())
    {
        return;
    }

    for (const auto &segment : segments)
    {
        tMaxMs = std::max (tMaxMs, std::max (0.0, segment.setupTimeMs));
        logInitialFidelity += std::log (std::max (segment.initialFidelity, 1e-12));
    }

    bool firstSegment = true;
    for (const auto &segment : segments)
    {
        const double waitMs = std::max (0.0, tMaxMs - segment.setupTimeMs);
        BellDiagonalState waitedState = ApplyPhaseFlipMemoryWait (
            MakeBellDiagonalState (segment.noiseFamily, segment.initialFidelity),
            waitMs,
            segment.leftTauMs,
            waitMs,
            segment.rightTauMs);

        if (firstSegment)
        {
            predictedState = waitedState;
            firstSegment = false;
        }
        else
        {
            predictedState = EntanglementSwapBellDiagonal (predictedState, waitedState);
        }
    }
}

} // namespace

TypeId
QuantumRoutingMetric::GetTypeId ()
{
    static TypeId tid =
        TypeId ("ns3::QuantumRoutingMetric")
            .SetParent<Object> ()
            .SetGroupName ("Quantum");
    return tid;
}

QuantumRoutingMetric::QuantumRoutingMetric ()
{
}

QuantumRoutingMetric::~QuantumRoutingMetric ()
{
}

QuantumRoutingLabel
QuantumRoutingMetric::CreateInitialLabel (const std::string &srcNode) const
{
    QuantumRoutingLabel label;
    label.path.push_back (srcNode);
    return label;
}

TypeId
BottleneckFidelityRoutingMetric::GetTypeId ()
{
    static TypeId tid =
        TypeId ("ns3::BottleneckFidelityRoutingMetric")
            .SetParent<QuantumRoutingMetric> ()
            .SetGroupName ("Quantum")
            .AddConstructor<BottleneckFidelityRoutingMetric> ()
            .AddAttribute ("UseCurrentLinkDelay",
                           "Use the latency field as the live control-delay estimate.",
                           BooleanValue (false),
                           MakeBooleanAccessor (&BottleneckFidelityRoutingMetric::m_useCurrentLinkDelay),
                           MakeBooleanChecker ());
    return tid;
}

BottleneckFidelityRoutingMetric::BottleneckFidelityRoutingMetric ()
    : m_useCurrentLinkDelay (false)
{
}

BottleneckFidelityRoutingMetric::~BottleneckFidelityRoutingMetric ()
{
}

QuantumRoutingLabel
BottleneckFidelityRoutingMetric::CreateInitialLabel (const std::string &srcNode) const
{
    QuantumRoutingLabel label = QuantumRoutingMetric::CreateInitialLabel (srcNode);
    label.scalars["t_max_ms"] = 0.0;
    label.scalars["log_initial_fidelity"] = 0.0;
    label.scalars["predicted_log_fidelity"] = 0.0;
    label.scalars["predicted_fidelity"] = 1.0;
    StoreBellState (label, {1.0, 0.0, 0.0, 0.0});
    return label;
}

double
BottleneckFidelityRoutingMetric::GetScalar (const QuantumRoutingLabel &label,
                                            const std::string &name,
                                            double fallback) const
{
    auto it = label.scalars.find (name);
    if (it == label.scalars.end ())
    {
        return fallback;
    }
    return it->second;
}

double
BottleneckFidelityRoutingMetric::ResolveLinkInitialFidelity (const LinkMetrics &attributes) const
{
    double fidelity = attributes.initialFidelity;
    if (fidelity <= 0.0)
    {
        fidelity = attributes.fidelity;
    }
    fidelity = std::max (fidelity, 1e-12);
    fidelity = std::min (fidelity, 1.0);
    return fidelity;
}

BellPairNoiseFamily
BottleneckFidelityRoutingMetric::ResolveLinkNoiseFamily (const LinkMetrics &attributes) const
{
    return attributes.noiseFamily;
}

double
BottleneckFidelityRoutingMetric::ResolveLinkReadyTimeMs (const LinkMetrics &attributes) const
{
    if (attributes.quantumSetupTimeMs > 0.0)
    {
        return attributes.quantumSetupTimeMs;
    }

    if (m_useCurrentLinkDelay && attributes.latency > 0.0)
    {
        return attributes.latency;
    }

    if (attributes.latency > 0.0 && attributes.classicalControlDelayMs <= 0.0)
    {
        return attributes.latency;
    }

    if (attributes.classicalControlDelayMs > 0.0)
    {
        return attributes.classicalControlDelayMs;
    }

    return 0.0;
}

double
BottleneckFidelityRoutingMetric::ResolveNodeCoherenceTimeMs (QuantumNetworkLayer* networkLayer,
                                                             const std::string &nodeName) const
{
    if (networkLayer == nullptr || networkLayer->GetPhyEntity () == nullptr)
    {
        return 1e12;
    }

    double tauMs = networkLayer->GetPhyEntity ()->GetCoherenceTimeMs (nodeName);
    if (tauMs <= 0.0)
    {
        return 1e12;
    }
    return tauMs;
}

bool
BottleneckFidelityRoutingMetric::ExtendLabel (QuantumNetworkLayer* networkLayer,
                                              const QuantumRoutingLabel &current,
                                              const std::string &nextNode,
                                              const LinkMetrics &linkAttributes,
                                              QuantumRoutingLabel &extended) const
{
    if (!linkAttributes.isAvailable || linkAttributes.successRate <= 0.0 || current.path.empty ())
    {
        return false;
    }

    if (std::find (current.path.begin (), current.path.end (), nextNode) != current.path.end ())
    {
        return false;
    }

    const std::string &currentNode = current.path.back ();
    extended = current;
    extended.path.push_back (nextNode);

    QuantumRoutingSegment segment;
    segment.leftNode = currentNode;
    segment.rightNode = nextNode;
    segment.initialFidelity = ResolveLinkInitialFidelity (linkAttributes);
    segment.noiseFamily = ResolveLinkNoiseFamily (linkAttributes);
    segment.setupTimeMs = ResolveLinkReadyTimeMs (linkAttributes);
    segment.leftTauMs = ResolveNodeCoherenceTimeMs (networkLayer, currentNode);
    segment.rightTauMs = ResolveNodeCoherenceTimeMs (networkLayer, nextNode);
    extended.segments.push_back (segment);

    BellDiagonalState predictedState;
    double nextTMax = 0.0;
    double nextLogInitial = 0.0;
    EvaluateSegments (extended.segments, predictedState, nextTMax, nextLogInitial);

    double predictedFidelity = GetBellFidelity (predictedState);
    double predictedLog = predictedFidelity > 0.0 ? std::log (predictedFidelity) : -700.0;

    extended.scalars["t_max_ms"] = nextTMax;
    extended.scalars["hop_count"] = static_cast<double> (extended.segments.size ());
    extended.scalars["log_initial_fidelity"] = nextLogInitial;
    extended.scalars["predicted_log_fidelity"] = predictedLog;
    extended.scalars["predicted_fidelity"] = predictedFidelity;
    StoreBellState (extended, predictedState);
    return true;
}

bool
BottleneckFidelityRoutingMetric::IsBetter (const QuantumRoutingLabel &lhs,
                                           const QuantumRoutingLabel &rhs) const
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

    double lhsTMax = GetScalar (lhs, "t_max_ms");
    double rhsTMax = GetScalar (rhs, "t_max_ms");
    if (lhsTMax + 1e-12 < rhsTMax)
    {
        return true;
    }
    if (rhsTMax + 1e-12 < lhsTMax)
    {
        return false;
    }

    return lhs.path.size () < rhs.path.size ();
}

bool
BottleneckFidelityRoutingMetric::Dominates (const QuantumRoutingLabel &lhs,
                                            const QuantumRoutingLabel &rhs) const
{
    double lhsScore = GetScore (lhs);
    double rhsScore = GetScore (rhs);
    double lhsTMax = GetScalar (lhs, "t_max_ms");
    double rhsTMax = GetScalar (rhs, "t_max_ms");

    bool notWorseScore = lhsScore + 1e-12 >= rhsScore;
    bool notWorseTMax = lhsTMax <= rhsTMax + 1e-12;
    bool strictlyBetter =
        lhsScore > rhsScore + 1e-12 || lhsTMax + 1e-12 < rhsTMax;
    return notWorseScore && notWorseTMax && strictlyBetter;
}

double
BottleneckFidelityRoutingMetric::GetScore (const QuantumRoutingLabel &label) const
{
    return GetScalar (label, "predicted_fidelity", 1.0);
}

} // namespace ns3
