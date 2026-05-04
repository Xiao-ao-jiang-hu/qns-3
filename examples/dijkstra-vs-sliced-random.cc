/*
 * dijkstra-vs-sliced-random.cc
 *
 * Random-topology statistical comparison for:
 *   - DijkstraRoutingProtocol       (single-label baseline)
 *   - SlicedDijkstraRoutingProtocol (K labels with fixed-width Tmax buckets)
 *
 * Topology model:
 *   1. Place S, T, and relay nodes in a 2-D area.
 *   2. Build a connected local-neighbor graph with low setup delay and
 *      ordinary initial fidelity.
 *   3. Add sparse long-distance links between non-neighbor nodes. These links
 *      have slightly higher initial fidelity but higher setup delay.
 *
 * This is the situation that exposes the current fidelity metric's prefix
 * trap: a high-fidelity long link can make a prefix label look locally better,
 * while its larger Tmax forces later low-delay links to wait and decohere.
 * Sliced Dijkstra can keep both the low-Tmax and high-fidelity labels at the
 * same node; single-label Dijkstra cannot.
 *
 * Usage:
 *   ./ns3 run 'dijkstra-vs-sliced-random --runs=20 --seed=1 --output=results/random'
 *   ./ns3 run 'dijkstra-vs-sliced-random --runs=100 --output=/tmp/random-routing'
 *
 * The final actual_fidelity result uses the real mixed-state density-matrix
 * simulator: Bell-pair generation, automatic waiting decoherence, swap gates,
 * measurements, Pauli corrections, partial traces, and final fidelity
 * calculation are executed through QuantumPhyEntity.
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
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

NS_LOG_COMPONENT_DEFINE("DijkstraVsSlicedRandom");

using namespace ns3;

namespace
{

using Topology = std::map<std::string, std::map<std::string, LinkMetrics>>;

struct Position
{
    double x{0.0};
    double y{0.0};
};

struct LongLink
{
    std::string left;
    std::string right;
};

struct GeneratedTopology
{
    Topology topology;
    Topology localTopology;
    std::vector<std::string> nodes;
    std::map<std::string, Position> positions;
    std::vector<LongLink> longLinks;
    uint32_t trapOpportunities{0};
    std::string src{"S"};
    std::string dst{"T"};
};

struct RunResult
{
    uint32_t runId{0};
    uint32_t seed{0};
    uint32_t nodes{0};
    uint32_t localEdges{0};
    uint32_t longEdges{0};
    uint32_t totalEdges{0};
    uint32_t trapOpportunities{0};
    bool dijkstraSuccess{false};
    bool slicedSuccess{false};
    bool routeDiff{false};
    bool slicedWins{false};
    bool slicedActualWins{false};
    double dijkstraFidelity{0.0};
    double slicedFidelity{0.0};
    double gain{0.0};
    double relativeGain{0.0};
    double dijkstraActualFidelity{0.0};
    double slicedActualFidelity{0.0};
    double actualGain{0.0};
    double actualRelativeGain{0.0};
    double dijkstraTMaxMs{0.0};
    double slicedTMaxMs{0.0};
    double dijkstraHopCount{0.0};
    double slicedHopCount{0.0};
    double dijkstraMs{0.0};
    double slicedMs{0.0};
    uint32_t dijkstraLabels{0};
    uint32_t slicedLabels{0};
    uint32_t slicedMaxLabelsAtNode{0};
    std::vector<std::string> dijkstraRoute;
    std::vector<std::string> slicedRoute;
};

double
Clamp(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

double
Uniform(std::mt19937& rng, double low, double high)
{
    std::uniform_real_distribution<double> dist(low, high);
    return dist(rng);
}

double
Distance(const Position& a, const Position& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

LinkMetrics
MakeLinkMetrics(double initialFidelity,
                double quantumSetupTimeMs,
                BellPairNoiseFamily noiseFamily = BellPairNoiseFamily::WERNER)
{
    LinkMetrics metrics;
    metrics.fidelity = initialFidelity;
    metrics.initialFidelity = initialFidelity;
    metrics.noiseFamily = noiseFamily;
    metrics.successRate = 1.0;
    metrics.latency = quantumSetupTimeMs;
    metrics.quantumSetupTimeMs = quantumSetupTimeMs;
    metrics.classicalControlDelayMs = 0.0;
    metrics.isAvailable = true;
    return metrics;
}

LinkMetrics
MakeLocalMetrics(std::mt19937& rng, double distance)
{
    const double fidelity = Clamp(0.980 - 0.030 * distance + Uniform(rng, -0.004, 0.004),
                                  0.955,
                                  0.982);
    const double setupMs = Clamp(38.0 + 45.0 * distance + Uniform(rng, -4.0, 4.0),
                                 35.0,
                                 58.0);
    return MakeLinkMetrics(fidelity, setupMs);
}

LinkMetrics
MakeLongMetrics(std::mt19937& rng, double distance)
{
    const double fidelity = Clamp(0.998 - 0.004 * distance + Uniform(rng, -0.0015, 0.0015),
                                  0.992,
                                  0.999);
    const double setupMs = Clamp(88.0 + 35.0 * distance + Uniform(rng, -5.0, 5.0),
                                 85.0,
                                 125.0);
    return MakeLinkMetrics(fidelity, setupMs);
}

double
GetScalar(const QuantumRoutingLabel& label, const std::string& key, double fallback = 0.0)
{
    auto it = label.scalars.find(key);
    return it == label.scalars.end() ? fallback : it->second;
}

std::string
RouteToString(const std::vector<std::string>& route)
{
    std::string result;
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (i > 0)
        {
            result += "|";
        }
        result += route[i];
    }
    return result;
}

std::string
PrettyRouteToString(const std::vector<std::string>& route)
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

std::string
FormatRunId(uint32_t runId)
{
    std::ostringstream os;
    os << std::setw(4) << std::setfill('0') << runId;
    return os.str();
}

bool
HasEdge(const Topology& topology, const std::string& left, const std::string& right)
{
    auto leftIt = topology.find(left);
    return leftIt != topology.end() && leftIt->second.count(right) > 0;
}

void
AddBidirectionalLink(Topology& topology,
                     const std::string& left,
                     const std::string& right,
                     const LinkMetrics& metrics)
{
    if (left == right || HasEdge(topology, left, right))
    {
        return;
    }
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

void
AddLocalLink(GeneratedTopology& generated,
             std::mt19937& rng,
             const std::string& left,
             const std::string& right)
{
    const double distance = Distance(generated.positions.at(left), generated.positions.at(right));
    const LinkMetrics metrics = MakeLocalMetrics(rng, distance);
    AddBidirectionalLink(generated.topology, left, right, metrics);
    AddBidirectionalLink(generated.localTopology, left, right, metrics);
}

void
AddLongLink(GeneratedTopology& generated,
            std::mt19937& rng,
            const std::string& left,
            const std::string& right)
{
    const double distance = Distance(generated.positions.at(left), generated.positions.at(right));
    AddBidirectionalLink(generated.topology, left, right, MakeLongMetrics(rng, distance));
    generated.longLinks.push_back({left, right});
}

uint32_t
CountUndirectedEdges(const Topology& topology)
{
    uint32_t count = 0;
    for (const auto& [u, neighbors] : topology)
    {
        for (const auto& [v, unused] : neighbors)
        {
            if (u < v)
            {
                ++count;
            }
        }
    }
    return count;
}

std::vector<std::string>
ShortestPathByHops(const Topology& topology, const std::string& src, const std::string& dst)
{
    std::queue<std::string> pending;
    std::set<std::string> visited;
    std::map<std::string, std::string> parent;

    pending.push(src);
    visited.insert(src);

    while (!pending.empty())
    {
        const std::string current = pending.front();
        pending.pop();
        if (current == dst)
        {
            break;
        }

        auto it = topology.find(current);
        if (it == topology.end())
        {
            continue;
        }

        for (const auto& [next, unused] : it->second)
        {
            if (visited.insert(next).second)
            {
                parent[next] = current;
                pending.push(next);
            }
        }
    }

    if (visited.count(dst) == 0)
    {
        return {};
    }

    std::vector<std::string> path;
    for (std::string node = dst;; node = parent[node])
    {
        path.push_back(node);
        if (node == src)
        {
            break;
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::string>
AppendPath(std::vector<std::string> left, const std::vector<std::string>& right)
{
    if (left.empty())
    {
        return right;
    }
    for (size_t i = 0; i < right.size(); ++i)
    {
        if (i == 0 && right[i] == left.back())
        {
            continue;
        }
        left.push_back(right[i]);
    }
    return left;
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
    double swapSignalDelayMs{0.0};
    double* measuredFidelity{nullptr};
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

    // PartialTrace removes the qubits from the tensor state. Remove them from
    // node memory too, otherwise later decoherence sweeps may touch stale names.
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

bool
IsPrefixTrap(const GeneratedTopology& generated,
             const LongLink& link,
             Ptr<QuantumRoutingMetric> metric,
             QuantumNetworkLayer* networkLayer,
             double bucketWidthMs)
{
    const std::string& left = link.left;
    const std::string& right = link.right;

    std::vector<std::string> localPrefix =
        ShortestPathByHops(generated.localTopology, generated.src, right);
    std::vector<std::string> shortcutPrefix =
        ShortestPathByHops(generated.localTopology, generated.src, left);
    std::vector<std::string> suffix =
        ShortestPathByHops(generated.localTopology, right, generated.dst);

    if (localPrefix.empty() || shortcutPrefix.empty() || suffix.empty())
    {
        return false;
    }
    shortcutPrefix.push_back(right);

    const std::vector<std::string> localFull = AppendPath(localPrefix, suffix);
    const std::vector<std::string> shortcutFull = AppendPath(shortcutPrefix, suffix);

    QuantumRoutingLabel localPrefixLabel;
    QuantumRoutingLabel shortcutPrefixLabel;
    QuantumRoutingLabel localFullLabel;
    QuantumRoutingLabel shortcutFullLabel;
    if (!BuildLabelForRoute(metric, networkLayer, generated.topology, localPrefix, localPrefixLabel) ||
        !BuildLabelForRoute(metric,
                            networkLayer,
                            generated.topology,
                            shortcutPrefix,
                            shortcutPrefixLabel) ||
        !BuildLabelForRoute(metric, networkLayer, generated.topology, localFull, localFullLabel) ||
        !BuildLabelForRoute(metric, networkLayer, generated.topology, shortcutFull, shortcutFullLabel))
    {
        return false;
    }

    const double localPrefixF = metric->GetScore(localPrefixLabel);
    const double shortcutPrefixF = metric->GetScore(shortcutPrefixLabel);
    const double localFullF = metric->GetScore(localFullLabel);
    const double shortcutFullF = metric->GetScore(shortcutFullLabel);
    const double localT = GetScalar(localPrefixLabel, "t_max_ms");
    const double shortcutT = GetScalar(shortcutPrefixLabel, "t_max_ms");
    const double width = bucketWidthMs > 0.0 ? bucketWidthMs : 1.0;
    const int64_t localBucket = static_cast<int64_t>(std::floor(localT / width));
    const int64_t shortcutBucket = static_cast<int64_t>(std::floor(shortcutT / width));

    return shortcutPrefixF > localPrefixF + 1e-12 &&
           localFullF > shortcutFullF + 1e-12 &&
           shortcutT > localT + 1e-12 &&
           localBucket != shortcutBucket;
}

void
ConnectLocalComponents(GeneratedTopology& generated, std::mt19937& rng)
{
    while (true)
    {
        std::map<std::string, uint32_t> component;
        uint32_t componentId = 0;
        for (const auto& node : generated.nodes)
        {
            if (component.count(node) > 0)
            {
                continue;
            }

            std::queue<std::string> pending;
            pending.push(node);
            component[node] = componentId;
            while (!pending.empty())
            {
                const std::string current = pending.front();
                pending.pop();
                auto it = generated.localTopology.find(current);
                if (it == generated.localTopology.end())
                {
                    continue;
                }
                for (const auto& [next, unused] : it->second)
                {
                    if (component.count(next) == 0)
                    {
                        component[next] = componentId;
                        pending.push(next);
                    }
                }
            }
            ++componentId;
        }

        if (componentId <= 1)
        {
            return;
        }

        double bestDistance = std::numeric_limits<double>::infinity();
        std::string bestLeft;
        std::string bestRight;
        for (const auto& left : generated.nodes)
        {
            for (const auto& right : generated.nodes)
            {
                if (component[left] == component[right])
                {
                    continue;
                }
                const double distance = Distance(generated.positions.at(left), generated.positions.at(right));
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestLeft = left;
                    bestRight = right;
                }
            }
        }
        AddLocalLink(generated, rng, bestLeft, bestRight);
    }
}

GeneratedTopology
GenerateTopology(uint32_t seed,
                 uint32_t nodeCount,
                 double localRadius,
                 double localProbability,
                 uint32_t localK,
                 uint32_t longLinkCount,
                 double minLongDistance,
                 double minLongProgress,
                 double maxLongStartX,
                 double maxLongEndX,
                 double bucketWidthMs,
                 Ptr<QuantumRoutingMetric> metric,
                 QuantumNetworkLayer* networkLayer)
{
    std::mt19937 rng(seed);
    GeneratedTopology generated;
    generated.nodes.push_back(generated.src);
    generated.nodes.push_back(generated.dst);
    generated.positions[generated.src] = {0.0, 0.50};
    generated.positions[generated.dst] = {1.0, 0.50};

    for (uint32_t index = 0; index < nodeCount; ++index)
    {
        const std::string name = "N" + std::to_string(index);
        generated.nodes.push_back(name);
        generated.positions[name] = {Uniform(rng, 0.02, 0.98), Uniform(rng, 0.02, 0.98)};
    }

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (size_t i = 0; i < generated.nodes.size(); ++i)
    {
        for (size_t j = i + 1; j < generated.nodes.size(); ++j)
        {
            const std::string& left = generated.nodes[i];
            const std::string& right = generated.nodes[j];
            const double distance = Distance(generated.positions.at(left), generated.positions.at(right));
            if (distance <= localRadius && unit(rng) < localProbability)
            {
                AddLocalLink(generated, rng, left, right);
            }
        }
    }

    for (const auto& node : generated.nodes)
    {
        std::vector<std::pair<double, std::string>> nearest;
        nearest.reserve(generated.nodes.size() - 1);
        for (const auto& other : generated.nodes)
        {
            if (node == other)
            {
                continue;
            }
            nearest.emplace_back(Distance(generated.positions.at(node), generated.positions.at(other)),
                                 other);
        }
        std::sort(nearest.begin(), nearest.end());

        const uint32_t limit = std::min<uint32_t>(localK, static_cast<uint32_t>(nearest.size()));
        for (uint32_t i = 0; i < limit; ++i)
        {
            AddLocalLink(generated, rng, node, nearest[i].second);
        }
    }

    ConnectLocalComponents(generated, rng);

    std::vector<LongLink> candidates;
    for (const auto& left : generated.nodes)
    {
        for (const auto& right : generated.nodes)
        {
            if (left == right || right == generated.dst || left == generated.dst)
            {
                continue;
            }
            if (generated.positions.at(left).x > maxLongStartX ||
                generated.positions.at(right).x > maxLongEndX)
            {
                continue;
            }
            if (generated.positions.at(right).x <= generated.positions.at(left).x + minLongProgress)
            {
                continue;
            }
            const double distance = Distance(generated.positions.at(left), generated.positions.at(right));
            if (distance >= minLongDistance && !HasEdge(generated.topology, left, right))
            {
                candidates.push_back({left, right});
            }
        }
    }
    std::shuffle(candidates.begin(), candidates.end(), rng);

    for (const auto& candidate : candidates)
    {
        if (generated.longLinks.size() >= longLinkCount)
        {
            break;
        }
        AddLongLink(generated, rng, candidate.left, candidate.right);
    }

    for (const auto& link : generated.longLinks)
    {
        if (IsPrefixTrap(generated, link, metric, networkLayer, bucketWidthMs))
        {
            ++generated.trapOpportunities;
        }
    }

    std::sort(generated.nodes.begin(), generated.nodes.end());
    return generated;
}

uint32_t
CountLabels(const std::vector<std::string>& nodes,
            const std::function<std::vector<QuantumRoutingLabel>(const std::string&)>& getLabels,
            uint32_t* maxLabelsAtNode = nullptr)
{
    uint32_t total = 0;
    uint32_t maxLabels = 0;
    for (const auto& node : nodes)
    {
        const uint32_t count = static_cast<uint32_t>(getLabels(node).size());
        total += count;
        maxLabels = std::max(maxLabels, count);
    }
    if (maxLabelsAtNode != nullptr)
    {
        *maxLabelsAtNode = maxLabels;
    }
    return total;
}

template <typename Fn>
double
MeasureMs(Fn&& fn)
{
    auto start = std::chrono::steady_clock::now();
    fn();
    auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

void
WriteTopologyCsv(const std::filesystem::path& path, const GeneratedTopology& generated)
{
    std::ofstream os(path);
    os << "edge_type,u,v,u_x,u_y,v_x,v_y,distance,initial_fidelity,setup_time_ms\n";
    os << std::fixed << std::setprecision(12);
    for (const auto& [u, neighbors] : generated.topology)
    {
        for (const auto& [v, metrics] : neighbors)
        {
            if (u >= v)
            {
                continue;
            }
            const bool isLocal = HasEdge(generated.localTopology, u, v);
            const Position& up = generated.positions.at(u);
            const Position& vp = generated.positions.at(v);
            os << (isLocal ? "local_low_delay" : "long_high_fidelity_high_delay") << ","
               << u << ","
               << v << ","
               << up.x << ","
               << up.y << ","
               << vp.x << ","
               << vp.y << ","
               << Distance(up, vp) << ","
               << (metrics.initialFidelity > 0.0 ? metrics.initialFidelity : metrics.fidelity) << ","
               << ResolveSetupTimeMs(metrics) << "\n";
        }
    }
}

void
WriteRunReport(const std::filesystem::path& path,
               const GeneratedTopology& generated,
               const RunResult& result,
               uint32_t requestedRelayNodes,
               double localRadius,
               double localProbability,
               uint32_t localK,
               uint32_t requestedLongLinks,
               double minLongDistance,
               double minLongProgress,
               double maxLongStartX,
               double maxLongEndX,
               double coherenceMs,
               double swapSignalDelayMs,
               uint32_t k,
               double bucketWidthMs,
               bool useBuckets)
{
    std::ofstream os(path);
    os << std::fixed << std::setprecision(6);
    os << "# Run " << result.runId << "\n\n";
    os << "## Topology Settings\n";
    os << "- seed: " << result.seed << "\n";
    os << "- source: " << generated.src << "\n";
    os << "- destination: " << generated.dst << "\n";
    os << "- requested_relay_nodes: " << requestedRelayNodes << "\n";
    os << "- total_nodes_including_S_T: " << result.nodes << "\n";
    os << "- localRadius: " << localRadius << "\n";
    os << "- pLocal: " << localProbability << "\n";
    os << "- localK: " << localK << "\n";
    os << "- requestedLongLinks: " << requestedLongLinks << "\n";
    os << "- actualLongLinks: " << result.longEdges << "\n";
    os << "- minLongDistance: " << minLongDistance << "\n";
    os << "- minLongProgress: " << minLongProgress << "\n";
    os << "- maxLongStartX: " << maxLongStartX << "\n";
    os << "- maxLongEndX: " << maxLongEndX << "\n";
    os << "- coherenceMs: " << coherenceMs << "\n";
    os << "- swapSignalDelayMs: " << swapSignalDelayMs << "\n";
    os << "- actualSimulation: real_mixed_density_matrix\n";
    os << "- slicedK: " << k << "\n";
    os << "- bucketWidthMs: " << bucketWidthMs << "\n";
    os << "- useBuckets: " << useBuckets << "\n";
    os << "- local_edges: " << result.localEdges << "\n";
    os << "- total_edges: " << result.totalEdges << "\n";
    os << "- trap_opportunities: " << result.trapOpportunities << "\n\n";

    os << "## Selected Routes\n";
    os << "| algorithm | success | route | hops | predicted_fidelity | t_max_ms | actual_fidelity |\n";
    os << "|---|---:|---|---:|---:|---:|---:|\n";
    os << "| Dijkstra | " << result.dijkstraSuccess << " | "
       << PrettyRouteToString(result.dijkstraRoute) << " | "
       << result.dijkstraHopCount << " | "
       << result.dijkstraFidelity << " | "
       << result.dijkstraTMaxMs << " | "
       << result.dijkstraActualFidelity << " |\n";
    os << "| Sliced Dijkstra | " << result.slicedSuccess << " | "
       << PrettyRouteToString(result.slicedRoute) << " | "
       << result.slicedHopCount << " | "
       << result.slicedFidelity << " | "
       << result.slicedTMaxMs << " | "
       << result.slicedActualFidelity << " |\n\n";

    os << "## Comparison\n";
    os << "- route_diff: " << result.routeDiff << "\n";
    os << "- predicted_gain: " << result.gain << "\n";
    os << "- predicted_relative_gain: " << result.relativeGain << "\n";
    os << "- actual_gain: " << result.actualGain << "\n";
    os << "- actual_relative_gain: " << result.actualRelativeGain << "\n";
    os << "- sliced_predicted_win: " << result.slicedWins << "\n";
    os << "- sliced_actual_win: " << result.slicedActualWins << "\n";
    os << "- dijkstra_runtime_ms: " << result.dijkstraMs << "\n";
    os << "- sliced_runtime_ms: " << result.slicedMs << "\n";
    os << "- dijkstra_labels: " << result.dijkstraLabels << "\n";
    os << "- sliced_labels: " << result.slicedLabels << "\n";
    os << "- sliced_max_labels_at_node: " << result.slicedMaxLabelsAtNode << "\n\n";

    os << "## Node Positions\n";
    os << "| node | x | y |\n";
    os << "|---|---:|---:|\n";
    for (const auto& node : generated.nodes)
    {
        const Position& position = generated.positions.at(node);
        os << "| " << node << " | " << position.x << " | " << position.y << " |\n";
    }
    os << "\n";

    os << "## Edge Table\n";
    os << "Full edge details are written to `run_" << FormatRunId(result.runId)
       << "_topology.csv`.\n";
}

RunResult
RunOne(uint32_t runId,
       uint32_t seed,
       uint32_t nodeCount,
       double localRadius,
       double localProbability,
       uint32_t localK,
       uint32_t longLinks,
       double minLongDistance,
       double minLongProgress,
       double maxLongStartX,
       double maxLongEndX,
       double coherenceMs,
       double swapSignalDelayMs,
       uint32_t k,
       double bucketWidthMs,
       bool useBuckets,
       const std::filesystem::path& outputPath)
{
    std::vector<std::string> phyNodes;
    phyNodes.push_back("S");
    phyNodes.push_back("T");
    for (uint32_t index = 0; index < nodeCount; ++index)
    {
        phyNodes.push_back("N" + std::to_string(index));
    }

    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(phyNodes);
    for (const auto& node : phyNodes)
    {
        qphyent->SetTimeModel(node, coherenceMs / 1000.0);
    }

    Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer>();
    netLayer->SetOwner("S");
    netLayer->SetPhyEntity(qphyent);

    Ptr<BottleneckFidelityRoutingMetric> metric = CreateObject<BottleneckFidelityRoutingMetric>();

    GeneratedTopology generated = GenerateTopology(seed,
                                                   nodeCount,
                                                   localRadius,
                                                   localProbability,
                                                   localK,
                                                   longLinks,
                                                   minLongDistance,
                                                   minLongProgress,
                                                   maxLongStartX,
                                                   maxLongEndX,
                                                   bucketWidthMs,
                                                   metric,
                                                   PeekPointer(netLayer));

    Ptr<DijkstraRoutingProtocol> dijkstra = CreateObject<DijkstraRoutingProtocol>();
    dijkstra->SetMetricModel(metric);
    dijkstra->SetNetworkLayer(PeekPointer(netLayer));
    dijkstra->Initialize();
    dijkstra->UpdateTopology(generated.topology);

    Ptr<SlicedDijkstraRoutingProtocol> sliced = CreateObject<SlicedDijkstraRoutingProtocol>();
    sliced->SetMetricModel(metric);
    sliced->SetNetworkLayer(PeekPointer(netLayer));
    sliced->SetAttribute("K", UintegerValue(k));
    sliced->SetAttribute("BucketWidthMs", DoubleValue(bucketWidthMs));
    sliced->SetAttribute("UseBuckets", BooleanValue(useBuckets));
    sliced->Initialize();
    sliced->UpdateTopology(generated.topology);

    RunResult result;
    result.runId = runId;
    result.seed = seed;
    result.nodes = static_cast<uint32_t>(generated.nodes.size());
    result.localEdges = CountUndirectedEdges(generated.localTopology);
    result.longEdges = static_cast<uint32_t>(generated.longLinks.size());
    result.totalEdges = CountUndirectedEdges(generated.topology);
    result.trapOpportunities = generated.trapOpportunities;

    result.dijkstraMs = MeasureMs([&]() {
        result.dijkstraRoute = dijkstra->CalculateRoute(generated.src, generated.dst);
    });
    result.slicedMs = MeasureMs([&]() {
        result.slicedRoute = sliced->CalculateRoute(generated.src, generated.dst);
    });

    QuantumRoutingLabel dijkstraLabel;
    QuantumRoutingLabel slicedLabel;
    result.dijkstraSuccess = BuildLabelForRoute(metric,
                                                PeekPointer(netLayer),
                                                generated.topology,
                                                result.dijkstraRoute,
                                                dijkstraLabel);
    result.slicedSuccess = BuildLabelForRoute(metric,
                                              PeekPointer(netLayer),
                                              generated.topology,
                                              result.slicedRoute,
                                              slicedLabel);

    result.dijkstraFidelity =
        result.dijkstraSuccess ? metric->GetScore(dijkstraLabel) : 0.0;
    result.slicedFidelity = result.slicedSuccess ? metric->GetScore(slicedLabel) : 0.0;
    result.dijkstraTMaxMs = result.dijkstraSuccess ? GetScalar(dijkstraLabel, "t_max_ms") : 0.0;
    result.slicedTMaxMs = result.slicedSuccess ? GetScalar(slicedLabel, "t_max_ms") : 0.0;
    result.dijkstraHopCount = result.dijkstraSuccess ? GetScalar(dijkstraLabel, "hop_count") : 0.0;
    result.slicedHopCount = result.slicedSuccess ? GetScalar(slicedLabel, "hop_count") : 0.0;
    result.gain = result.slicedFidelity - result.dijkstraFidelity;
    result.relativeGain =
        result.dijkstraFidelity > 1e-12 ? result.gain / result.dijkstraFidelity : 0.0;
    result.routeDiff = result.dijkstraRoute != result.slicedRoute;
    result.slicedWins = result.gain > 1e-12;
    result.dijkstraLabels = CountLabels(generated.nodes, [dijkstra](const std::string& node) {
        return dijkstra->GetNodeLabels(node);
    });
    result.slicedLabels = CountLabels(generated.nodes,
                                      [sliced](const std::string& node) {
                                          return sliced->GetNodeLabels(node);
                                      },
                                      &result.slicedMaxLabelsAtNode);

    result.dijkstraActualFidelity =
        result.dijkstraSuccess
            ? SimulateRouteActualFidelity(result.dijkstraRoute,
                                          generated.topology,
                                          coherenceMs,
                                          swapSignalDelayMs)
            : 0.0;
    if (result.slicedSuccess && result.dijkstraSuccess && result.slicedRoute == result.dijkstraRoute)
    {
        result.slicedActualFidelity = result.dijkstraActualFidelity;
    }
    else
    {
        result.slicedActualFidelity =
            result.slicedSuccess
                ? SimulateRouteActualFidelity(result.slicedRoute,
                                              generated.topology,
                                              coherenceMs,
                                              swapSignalDelayMs)
                : 0.0;
    }
    result.actualGain = result.slicedActualFidelity - result.dijkstraActualFidelity;
    result.actualRelativeGain =
        result.dijkstraActualFidelity > 1e-12 ? result.actualGain / result.dijkstraActualFidelity
                                              : 0.0;
    result.slicedActualWins = result.actualGain > 1e-12;

    const std::string runIdText = FormatRunId(runId);
    WriteTopologyCsv(outputPath / ("run_" + runIdText + "_topology.csv"), generated);
    WriteRunReport(outputPath / ("run_" + runIdText + "_report.md"),
                   generated,
                   result,
                   nodeCount,
                   localRadius,
                   localProbability,
                   localK,
                   longLinks,
                   minLongDistance,
                   minLongProgress,
                   maxLongStartX,
                   maxLongEndX,
                   coherenceMs,
                   swapSignalDelayMs,
                   k,
                   bucketWidthMs,
                   useBuckets);

    sliced->Dispose();
    dijkstra->Dispose();
    netLayer->Dispose();
    qphyent->Dispose();
    Simulator::Destroy();
    return result;
}

void
WriteCsvHeader(std::ostream& os)
{
    os << "run,seed,nodes,local_edges,long_edges,total_edges,trap_opportunities,"
       << "dijkstra_success,sliced_success,dijkstra_fidelity,sliced_fidelity,"
       << "gain,relative_gain,dijkstra_actual_fidelity,sliced_actual_fidelity,"
       << "actual_gain,actual_relative_gain,route_diff,sliced_wins,sliced_actual_wins,"
       << "dijkstra_t_max_ms,sliced_t_max_ms,dijkstra_ms,sliced_ms,"
       << "dijkstra_labels,sliced_labels,sliced_max_labels_at_node,"
       << "dijkstra_hops,sliced_hops,dijkstra_route,sliced_route\n";
}

void
WriteCsvRow(std::ostream& os, const RunResult& result)
{
    os << result.runId << ","
       << result.seed << ","
       << result.nodes << ","
       << result.localEdges << ","
       << result.longEdges << ","
       << result.totalEdges << ","
       << result.trapOpportunities << ","
       << result.dijkstraSuccess << ","
       << result.slicedSuccess << ","
       << std::setprecision(12)
       << result.dijkstraFidelity << ","
       << result.slicedFidelity << ","
       << result.gain << ","
       << result.relativeGain << ","
       << result.dijkstraActualFidelity << ","
       << result.slicedActualFidelity << ","
       << result.actualGain << ","
       << result.actualRelativeGain << ","
       << result.routeDiff << ","
       << result.slicedWins << ","
       << result.slicedActualWins << ","
       << result.dijkstraTMaxMs << ","
       << result.slicedTMaxMs << ","
       << result.dijkstraMs << ","
       << result.slicedMs << ","
       << result.dijkstraLabels << ","
       << result.slicedLabels << ","
       << result.slicedMaxLabelsAtNode << ","
       << result.dijkstraHopCount << ","
       << result.slicedHopCount << ","
       << RouteToString(result.dijkstraRoute) << ","
       << RouteToString(result.slicedRoute) << "\n";
}

void
WriteSummaryMarkdown(const std::filesystem::path& path,
                     const std::vector<RunResult>& results,
                     uint32_t successful,
                     uint32_t routeDiffs,
                     uint32_t slicedWins,
                     uint32_t slicedActualWins,
                     double routeDisagreementRate,
                     double slicedWinRate,
                     double slicedActualWinRate,
                     double meanGain,
                     double meanActualGain,
                     double meanTrapOpportunities)
{
    std::ofstream os(path);
    os << std::fixed << std::setprecision(6);
    os << "# Dijkstra vs Sliced Dijkstra Random Topology Summary\n\n";
    os << "## Aggregate\n";
    os << "- successful_pairs: " << successful << "/" << results.size() << "\n";
    os << "- route_disagreement_rate: " << routeDisagreementRate << "\n";
    os << "- sliced_predicted_win_rate: " << slicedWinRate << "\n";
    os << "- sliced_actual_win_rate: " << slicedActualWinRate << "\n";
    os << "- mean_predicted_gain: " << meanGain << "\n";
    os << "- mean_actual_gain: " << meanActualGain << "\n";
    os << "- mean_trap_opportunities: " << meanTrapOpportunities << "\n";
    os << "- route_disagreement_count: " << routeDiffs << "\n";
    os << "- sliced_predicted_win_count: " << slicedWins << "\n";
    os << "- sliced_actual_win_count: " << slicedActualWins << "\n\n";

    os << "## Runs\n";
    os << "| run | seed | nodes | edges | traps | D predicted | S predicted | predicted gain "
          "| D actual | S actual | actual gain | route diff |\n";
    os << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& result : results)
    {
        os << "| " << result.runId
           << " | " << result.seed
           << " | " << result.nodes
           << " | " << result.totalEdges
           << " | " << result.trapOpportunities
           << " | " << result.dijkstraFidelity
           << " | " << result.slicedFidelity
           << " | " << result.gain
           << " | " << result.dijkstraActualFidelity
           << " | " << result.slicedActualFidelity
           << " | " << result.actualGain
           << " | " << result.routeDiff
           << " |\n";
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t runs = 20;
    uint32_t seed = 1;
    uint32_t nodes = 64;
    double localRadius = 0.18;
    double localProbability = 0.55;
    uint32_t localK = 3;
    uint32_t longLinks = 24;
    double minLongDistance = 0.35;
    double minLongProgress = 0.15;
    double maxLongStartX = 0.70;
    double maxLongEndX = 0.82;
    double coherenceMs = 100.0;
    double swapSignalDelayMs = 1.0;
    uint32_t k = 8;
    double bucketWidthMs = 10.0;
    bool useBuckets = true;
    std::string output = "dijkstra_vs_sliced_random_results";

    CommandLine cmd(__FILE__);
    cmd.AddValue("runs", "Number of random topologies to evaluate.", runs);
    cmd.AddValue("seed", "Base random seed.", seed);
    cmd.AddValue("nodes", "Number of random relay nodes excluding S and T.", nodes);
    cmd.AddValue("localRadius", "Distance threshold for probabilistic local low-delay edges.", localRadius);
    cmd.AddValue("pLocal", "Probability of adding an edge between two local-radius neighbors.", localProbability);
    cmd.AddValue("localK", "Minimum nearest-neighbor local links attempted per node.", localK);
    cmd.AddValue("longLinks", "Number of sparse long-distance high-fidelity/high-delay links.", longLinks);
    cmd.AddValue("minLongDistance", "Minimum Euclidean distance for a long-distance link.", minLongDistance);
    cmd.AddValue("minLongProgress", "Minimum x-axis progress for a long-distance shortcut toward T.", minLongProgress);
    cmd.AddValue("maxLongStartX", "Maximum source-side x coordinate for long-distance shortcuts.", maxLongStartX);
    cmd.AddValue("maxLongEndX", "Maximum destination-side x coordinate for long-distance shortcuts.", maxLongEndX);
    cmd.AddValue("coherenceMs", "Coherence time assigned to every node, in milliseconds.", coherenceMs);
    cmd.AddValue("swapSignalDelayMs",
                 "Classical signaling delay between consecutive swap stages in final physical"
                 " validation, in milliseconds.",
                 swapSignalDelayMs);
    cmd.AddValue("k", "Maximum labels retained per node by Sliced Dijkstra.", k);
    cmd.AddValue("bucketWidthMs", "Fixed Tmax bucket width for Sliced Dijkstra.", bucketWidthMs);
    cmd.AddValue("useBuckets", "Enable Tmax bucket slicing in Sliced Dijkstra.", useBuckets);
    cmd.AddValue("output", "Output directory path for per-run reports and summary tables.", output);
    cmd.Parse(argc, argv);

    const std::filesystem::path outputPath(output);
    std::error_code fsError;
    std::filesystem::create_directories(outputPath, fsError);
    if (fsError)
    {
        std::cerr << "Failed to create output directory: " << outputPath
                  << " error=" << fsError.message() << "\n";
        return 1;
    }

    std::ofstream csv(outputPath / "summary.csv");
    if (!csv.is_open())
    {
        std::cerr << "Failed to open summary CSV in output directory: " << outputPath << "\n";
        return 1;
    }
    WriteCsvHeader(csv);

    std::vector<RunResult> results;
    results.reserve(runs);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Random geometric topology comparison\n";
    std::cout << "  runs=" << runs
              << " seed=" << seed
              << " nodes=" << nodes
              << " localRadius=" << localRadius
              << " pLocal=" << localProbability
              << " localK=" << localK
              << " longLinks=" << longLinks
              << " minLongDistance=" << minLongDistance
              << " minLongProgress=" << minLongProgress
              << " maxLongStartX=" << maxLongStartX
              << " maxLongEndX=" << maxLongEndX
              << " tauMs=" << coherenceMs
              << " swapSignalDelayMs=" << swapSignalDelayMs
              << " K=" << k
              << " bucketWidthMs=" << bucketWidthMs
              << " useBuckets=" << useBuckets << "\n";
    std::cout << "  output=" << outputPath << "\n";
    std::cout << "  summary_csv=" << (outputPath / "summary.csv") << "\n\n";

    std::cout << "run seed nodes local_edges long_edges traps pred_d pred_s pred_gain "
              << "actual_d actual_s actual_gain route_diff labels_d labels_s ms_d ms_s\n";

    for (uint32_t run = 0; run < runs; ++run)
    {
        RunResult result = RunOne(run,
                                  seed + run,
                                  nodes,
                                  localRadius,
                                  localProbability,
                                  localK,
                                  longLinks,
                                  minLongDistance,
                                  minLongProgress,
                                  maxLongStartX,
                                  maxLongEndX,
                                  coherenceMs,
                                  swapSignalDelayMs,
                                  k,
                                  bucketWidthMs,
                                  useBuckets,
                                  outputPath);
        WriteCsvRow(csv, result);
        results.push_back(result);

        std::cout << result.runId << " "
                  << result.seed << " "
                  << result.nodes << " "
                  << result.localEdges << " "
                  << result.longEdges << " "
                  << result.trapOpportunities << " "
                  << result.dijkstraFidelity << " "
                  << result.slicedFidelity << " "
                  << result.gain << " "
                  << result.dijkstraActualFidelity << " "
                  << result.slicedActualFidelity << " "
                  << result.actualGain << " "
                  << result.routeDiff << " "
                  << result.dijkstraLabels << " "
                  << result.slicedLabels << " "
                  << result.dijkstraMs << " "
                  << result.slicedMs << "\n";
    }

    const uint32_t successful = static_cast<uint32_t>(
        std::count_if(results.begin(), results.end(), [](const RunResult& result) {
            return result.dijkstraSuccess && result.slicedSuccess;
        }));
    const uint32_t routeDiffs = static_cast<uint32_t>(
        std::count_if(results.begin(), results.end(), [](const RunResult& result) {
            return result.routeDiff;
        }));
    const uint32_t slicedWins = static_cast<uint32_t>(
        std::count_if(results.begin(), results.end(), [](const RunResult& result) {
            return result.slicedWins;
        }));
    const uint32_t slicedActualWins = static_cast<uint32_t>(
        std::count_if(results.begin(), results.end(), [](const RunResult& result) {
            return result.slicedActualWins;
        }));

    auto sumOf = [&](auto getter) {
        double sum = 0.0;
        for (const auto& result : results)
        {
            sum += getter(result);
        }
        return sum;
    };

    const double denom = results.empty() ? 1.0 : static_cast<double>(results.size());
    const double routeDisagreementRate = routeDiffs / denom;
    const double slicedWinRate = slicedWins / denom;
    const double slicedActualWinRate = slicedActualWins / denom;
    const double meanGain = sumOf([](const RunResult& r) { return r.gain; }) / denom;
    const double meanActualGain = sumOf([](const RunResult& r) { return r.actualGain; }) / denom;
    const double meanTrapOpportunities =
        sumOf([](const RunResult& r) { return static_cast<double>(r.trapOpportunities); }) /
        denom;
    std::cout << "\nSummary\n";
    std::cout << "  successful_pairs:       " << successful << "/" << results.size() << "\n";
    std::cout << "  route_disagreement_rate:" << routeDisagreementRate << "\n";
    std::cout << "  sliced_pred_win_rate:   " << slicedWinRate << "\n";
    std::cout << "  sliced_actual_win_rate: " << slicedActualWinRate << "\n";
    std::cout << "  mean_predicted_gain:    " << meanGain << "\n";
    std::cout << "  mean_actual_gain:       " << meanActualGain << "\n";
    std::cout << "  mean_relative_gain:     "
              << (sumOf([](const RunResult& r) { return r.relativeGain; }) / denom) << "\n";
    std::cout << "  mean_actual_rel_gain:   "
              << (sumOf([](const RunResult& r) { return r.actualRelativeGain; }) / denom)
              << "\n";
    std::cout << "  mean_trap_opportunities:" << meanTrapOpportunities << "\n";
    std::cout << "  mean_dijkstra_ms:       "
              << (sumOf([](const RunResult& r) { return r.dijkstraMs; }) / denom) << "\n";
    std::cout << "  mean_sliced_ms:         "
              << (sumOf([](const RunResult& r) { return r.slicedMs; }) / denom) << "\n";
    std::cout << "  mean_dijkstra_labels:   "
              << (sumOf([](const RunResult& r) { return static_cast<double>(r.dijkstraLabels); }) /
                  denom)
              << "\n";
    std::cout << "  mean_sliced_labels:     "
              << (sumOf([](const RunResult& r) { return static_cast<double>(r.slicedLabels); }) /
                  denom)
              << "\n";

    csv.close();
    WriteSummaryMarkdown(outputPath / "summary.md",
                         results,
                         successful,
                         routeDiffs,
                         slicedWins,
                         slicedActualWins,
                         routeDisagreementRate,
                         slicedWinRate,
                         slicedActualWinRate,
                         meanGain,
                         meanActualGain,
                         meanTrapOpportunities);
    return 0;
}
