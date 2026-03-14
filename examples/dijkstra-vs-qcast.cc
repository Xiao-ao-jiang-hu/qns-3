/*
 * Dijkstra vs Q-CAST — Comprehensive Comparison
 *
 * Evaluates Dijkstra and Q-CAST routing across:
 *   - Three topology families  (Random-Geometric, Erdős–Rényi, Scale-Free)
 *   - Multiple network sizes   (8, 12, 20, 30 nodes)
 *   - Seven packet-loss rates  (0 % … 30 %)
 *   - 30 independent runs per configuration
 *
 * Key design choices:
 *   - Both protocols receive IDENTICAL topology, request pairs, and per-link
 *     success/failure realisations so comparisons are strictly apples-to-apples.
 *   - End-to-end fidelity is computed in two components:
 *       1. Link fidelity product (each hop's channel quality).
 *       2. Decoherence penalty from quantum-memory wait time: each qubit
 *          stored at an intermediate node while the remaining hops are being
 *          established loses fidelity as F *= exp(-wait_ms / T_coh_ms).
 *          Default T_coh = 100 ms (adjustable via --tCohMs).
 *   - Q-CAST uses per-request CalculateRoutesGEDA to discover recovery paths,
 *     then the P3/P4 XOR-recovery simulation runs on the shared link states.
 *
 * Output (written to contrib/quantum/results/<timestamp>/):
 *   all_runs.csv        — one row per (run, topo, size, loss, protocol)
 *   per_request.csv     — one row per individual request attempt
 *   aggregated.csv      — mean ± stddev across runs for every configuration
 *   summary.txt         — human-readable tables reproduced from aggregated.csv
 *   topology_<name>.csv — per-topology slice of all_runs.csv for convenience
 */

#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/dijkstra-routing-protocol.h"
#include "ns3/quantum-topology-helper.h"
#include "ns3/quantum-signaling-channel.h"
#include "ns3/quantum-delay-model.h"
#include "ns3/quantum-net-stack-helper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

NS_LOG_COMPONENT_DEFINE ("DijkstraVsQCast");

using namespace ns3;

// ============================================================================
// Data structures
// ============================================================================

struct LinkState
{
    bool succeeded;
    double fidelity;    // Raw link fidelity (0 when failed)
    double latencyMs;   // One-way propagation + operation latency
};

using LinkStateMap = std::map<std::pair<std::string, std::string>, LinkState>;
using TopoMap     = std::map<std::string, std::map<std::string, LinkMetrics>>;

struct RequestRecord
{
    uint32_t runId;
    std::string topoType;
    uint32_t numNodes;
    double   packetLossProb;
    std::string protocol;
    std::string src;
    std::string dst;
    bool     routeFound;    // Did the protocol find ANY path?
    uint32_t hops;
    bool     succeeded;
    uint32_t recoveryUsed;
    double   linkFidelity;         // Product of per-hop channel fidelities
    double   decoherenceFactor;    // exp(-sum_wait / T_coh) product
    double   endToEndFidelity;     // linkFidelity * decoherenceFactor
    double   totalDelayMs;         // Sum of path hop latencies
    double   tCohMs;               // Coherence time used
    std::string pathStr;           // "NodeA->NodeB->..."
};

struct RunSummary
{
    uint32_t    runId;
    std::string topoType;
    uint32_t    numNodes;
    double      packetLossProb;
    std::string protocol;
    uint32_t    requestsSent;
    uint32_t    requestsSucceeded;
    uint32_t    requestsFailed;
    uint32_t    noRoute;           // How many requests had no path at all
    double      successRate;
    double      avgHops;
    double      avgLinkFidelity;
    double      avgDecoherenceFactor;
    double      avgEndToEndFidelity;
    double      minEndToEndFidelity;
    double      maxEndToEndFidelity;
    double      avgTotalDelayMs;
    uint32_t    recoveryPathsUsed;
    double      avgRecoveryPerSuccess; // recoveryUsed / requestsSucceeded
};

// ============================================================================
// Helpers
// ============================================================================

static std::string
PathToString (const std::vector<std::string> &path)
{
    std::string s;
    for (size_t i = 0; i < path.size (); ++i)
    {
        if (i)
            s += "->";
        s += path[i];
    }
    return s;
}

/// Canonical edge key (smaller string first).
static std::pair<std::string, std::string>
CanonKey (const std::string &u, const std::string &v)
{
    return (u < v) ? std::make_pair (u, v) : std::make_pair (v, u);
}

/// Generate per-link success/failure realisations (called once per run).
static LinkStateMap
GenerateLinkStates (const TopoMap &topology, double packetLossProb, std::mt19937 &rng)
{
    LinkStateMap states;
    std::uniform_real_distribution<double> dist (0.0, 1.0);

    for (const auto &[u, nbrs] : topology)
    {
        for (const auto &[v, m] : nbrs)
        {
            auto key = CanonKey (u, v);
            if (states.count (key))
                continue;
            bool ok     = m.isAvailable && (dist (rng) > packetLossProb);
            LinkState ls;
            ls.succeeded  = ok;
            ls.fidelity   = ok ? m.fidelity : 0.0;
            ls.latencyMs  = m.latency;  // stored from topology helper
            states[key]   = ls;
        }
    }
    return states;
}

static bool
LinkSucceeded (const LinkStateMap &s, const std::string &u, const std::string &v)
{
    auto it = s.find (CanonKey (u, v));
    return it != s.end () && it->second.succeeded;
}

static double
LinkFidelity (const LinkStateMap &s, const std::string &u, const std::string &v)
{
    auto it = s.find (CanonKey (u, v));
    return (it != s.end ()) ? it->second.fidelity : 0.0;
}

static double
LinkLatencyMs (const LinkStateMap &s, const std::string &u, const std::string &v)
{
    auto it = s.find (CanonKey (u, v));
    return (it != s.end ()) ? it->second.latencyMs : 0.0;
}

/**
 * \brief Compute decoherence penalty for a path.
 *
 * Model: qubits at intermediate nodes wait in quantum memory while subsequent
 * hops are being established.  Qubit at hop-k waits for hops k+1 .. H-1.
 * Decoherence factor per qubit: exp(-wait_ms / T_coh_ms).
 * Total factor: product over all intermediate qubits.
 *
 * \param path          List of node names (length = H+1 for H hops)
 * \param linkStates    Per-link realisation
 * \param tCohMs        Memory coherence time [ms]
 * \return {totalDecoherenceFactor, totalPathDelayMs}
 */
static std::pair<double, double>
ComputeDecoherence (const std::vector<std::string> &path,
                    const LinkStateMap &linkStates,
                    double tCohMs)
{
    if (path.size () < 2)
        return {1.0, 0.0};

    // Collect per-hop latencies
    std::vector<double> lat;
    for (size_t i = 0; i + 1 < path.size (); ++i)
        lat.push_back (LinkLatencyMs (linkStates, path[i], path[i + 1]));

    double totalDelayMs = 0.0;
    for (double d : lat)
        totalDelayMs += d;

    // Suffix sums: suffixWait[k] = sum(lat[k+1 .. H-1])
    double decFactor = 1.0;
    double suffixWait = 0.0;
    // Traverse from last hop backwards
    for (int k = static_cast<int> (lat.size ()) - 2; k >= 0; --k)
    {
        suffixWait += lat[static_cast<size_t> (k + 1)];
        // Qubit at node k+1 (intermediate node between hop k and hop k+1)
        // waits suffixWait before the swap at k+1 can proceed
        decFactor *= std::exp (-suffixWait / tCohMs);
    }
    return {decFactor, totalDelayMs};
}

/**
 * \brief Simulate a path (both Dijkstra and Q-CAST use this for primary).
 * \return {link_fidelity, decoherence_factor, total_delay_ms} or zeros on fail.
 */
static std::tuple<bool, double, double, double>
SimulatePath (const std::vector<std::string> &path,
              const LinkStateMap &linkStates,
              double tCohMs)
{
    if (path.size () < 2)
        return {false, 0.0, 1.0, 0.0};

    double linkFid = 1.0;
    for (size_t i = 0; i + 1 < path.size (); ++i)
    {
        if (!LinkSucceeded (linkStates, path[i], path[i + 1]))
            return {false, 0.0, 1.0, 0.0};
        linkFid *= LinkFidelity (linkStates, path[i], path[i + 1]);
    }
    auto [decFactor, totalDelay] = ComputeDecoherence (path, linkStates, tCohMs);
    return {true, linkFid, decFactor, totalDelay};
}

/** \brief Build a RequestRecord from a Dijkstra result. */
static RequestRecord
SimulateDijkstraRequest (uint32_t runId,
                         const std::string &topoType,
                         uint32_t numNodes,
                         double lossProb,
                         double tCohMs,
                         const std::string &src,
                         const std::string &dst,
                         const std::vector<std::string> &route,
                         const LinkStateMap &linkStates)
{
    RequestRecord r;
    r.runId           = runId;
    r.topoType        = topoType;
    r.numNodes        = numNodes;
    r.packetLossProb  = lossProb;
    r.protocol        = "Dijkstra";
    r.src             = src;
    r.dst             = dst;
    r.tCohMs          = tCohMs;
    r.routeFound      = !route.empty ();
    r.hops            = route.empty () ? 0 : static_cast<uint32_t> (route.size () - 1);
    r.recoveryUsed    = 0;
    r.pathStr         = PathToString (route);

    if (route.empty ())
    {
        r.succeeded           = false;
        r.linkFidelity        = 0.0;
        r.decoherenceFactor   = 1.0;
        r.endToEndFidelity    = 0.0;
        r.totalDelayMs        = 0.0;
        return r;
    }

    auto [ok, linkFid, decFactor, delay] = SimulatePath (route, linkStates, tCohMs);
    r.succeeded           = ok;
    r.linkFidelity        = ok ? linkFid : 0.0;
    r.decoherenceFactor   = ok ? decFactor : 1.0;
    r.endToEndFidelity    = ok ? linkFid * decFactor : 0.0;
    r.totalDelayMs        = delay;
    return r;
}

/** \brief Simulate a single Q-CAST request (P3-P4 phases). */
static RequestRecord
SimulateQCastRequest (uint32_t runId,
                      const std::string &topoType,
                      uint32_t numNodes,
                      double lossProb,
                      double tCohMs,
                      const std::string &src,
                      const std::string &dst,
                      const QCastPath &qcastPath,
                      const LinkStateMap &linkStates)
{
    RequestRecord r;
    r.runId           = runId;
    r.topoType        = topoType;
    r.numNodes        = numNodes;
    r.packetLossProb  = lossProb;
    r.protocol        = "QCAST";
    r.src             = src;
    r.dst             = dst;
    r.tCohMs          = tCohMs;
    r.routeFound      = !qcastPath.primaryPath.empty ();
    r.hops            = qcastPath.primaryPath.empty ()
                            ? 0
                            : static_cast<uint32_t> (qcastPath.primaryPath.size () - 1);
    r.recoveryUsed    = 0;
    r.pathStr         = PathToString (qcastPath.primaryPath);

    const auto &primary = qcastPath.primaryPath;
    if (primary.size () < 2)
    {
        r.succeeded           = false;
        r.linkFidelity        = 0.0;
        r.decoherenceFactor   = 1.0;
        r.endToEndFidelity    = 0.0;
        r.totalDelayMs        = 0.0;
        return r;
    }

    // ---- P3: collect all failed links on primary path ----
    std::set<uint32_t> failedLinks;
    for (size_t i = 0; i + 1 < primary.size (); ++i)
        if (!LinkSucceeded (linkStates, primary[i], primary[i + 1]))
            failedLinks.insert (static_cast<uint32_t> (i));

    if (failedLinks.empty ())
    {
        // Primary path fully succeeded
        double linkFid = 1.0;
        for (size_t i = 0; i + 1 < primary.size (); ++i)
            linkFid *= LinkFidelity (linkStates, primary[i], primary[i + 1]);
        auto [decFactor, delay] = ComputeDecoherence (primary, linkStates, tCohMs);
        r.succeeded           = true;
        r.linkFidelity        = linkFid;
        r.decoherenceFactor   = decFactor;
        r.endToEndFidelity    = linkFid * decFactor;
        r.totalDelayMs        = delay;
        return r;
    }

    // ---- P4: XOR recovery — sort rings by descending span, greedy cover ----
    using RingKey = std::pair<uint32_t, uint32_t>;
    std::vector<std::pair<RingKey, std::vector<std::string>>> sortedRings;
    for (const auto &e : qcastPath.recoveryPaths)
        sortedRings.push_back (e);
    std::sort (sortedRings.begin (), sortedRings.end (),
               [](const auto &a, const auto &b) {
                   return (a.first.second - a.first.first) > (b.first.second - b.first.first);
               });

    std::set<uint32_t> uncovered = failedLinks;
    std::vector<std::pair<RingKey, std::vector<std::string>>> activeRings;

    for (const auto &[key, recovPath] : sortedRings)
    {
        if (uncovered.empty ())
            break;
        uint32_t si = key.first, ej = key.second;

        // No overlap with already-selected rings
        bool overlaps = false;
        for (const auto &[ak, _] : activeRings)
            if (si < ak.second && ak.first < ej) { overlaps = true; break; }
        if (overlaps)
            continue;

        // Covers at least one uncovered failure
        bool coversAny = false;
        for (uint32_t f : uncovered)
            if (f >= si && f < ej) { coversAny = true; break; }
        if (!coversAny)
            continue;

        // Recovery path itself succeeds
        bool recovOk = true;
        for (size_t i = 0; i + 1 < recovPath.size (); ++i)
            if (!LinkSucceeded (linkStates, recovPath[i], recovPath[i + 1]))
            { recovOk = false; break; }
        if (!recovOk)
            continue;

        activeRings.push_back ({key, recovPath});
        for (uint32_t f = si; f < ej; ++f)
            uncovered.erase (f);
    }

    if (!uncovered.empty ())
    {
        r.succeeded           = false;
        r.linkFidelity        = 0.0;
        r.decoherenceFactor   = 1.0;
        r.endToEndFidelity    = 0.0;
        r.totalDelayMs        = 0.0;
        return r;
    }

    // ---- Assemble composite effective path for decoherence calculation ----
    // We build the actual traversed path: primary segments + recovery segments
    std::set<uint32_t> replacedIdx;
    for (const auto &[key, _] : activeRings)
        for (uint32_t i = key.first; i < key.second; ++i)
            replacedIdx.insert (i);

    // Composite path: concatenate non-replaced primary segments and recovery sub-paths
    std::vector<std::string> compositePath;
    {
        size_t i = 0;
        while (i < primary.size ())
        {
            // Check if a recovery ring starts at index i
            bool replaced = false;
            for (const auto &[key, recovPath] : activeRings)
            {
                if (key.first == (uint32_t)i)
                {
                    // Append recovery path (drops duplicate endpoints)
                    if (compositePath.empty ())
                        compositePath.push_back (recovPath.front ());
                    for (size_t k = 1; k < recovPath.size (); ++k)
                        compositePath.push_back (recovPath[k]);
                    i = key.second; // skip primary segment [i..ej]
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
            {
                if (compositePath.empty () || compositePath.back () != primary[i])
                    compositePath.push_back (primary[i]);
                ++i;
            }
        }
    }

    // Compute composite fidelity
    double linkFid = 1.0;
    for (size_t i = 0; i + 1 < compositePath.size (); ++i)
        linkFid *= LinkFidelity (linkStates, compositePath[i], compositePath[i + 1]);

    auto [decFactor, delay] = ComputeDecoherence (compositePath, linkStates, tCohMs);

    r.succeeded           = true;
    r.linkFidelity        = linkFid;
    r.decoherenceFactor   = decFactor;
    r.endToEndFidelity    = linkFid * decFactor;
    r.totalDelayMs        = delay;
    r.recoveryUsed        = static_cast<uint32_t> (activeRings.size ());
    r.hops                = static_cast<uint32_t> (compositePath.size () - 1);
    r.pathStr             = PathToString (compositePath);
    return r;
}

// ============================================================================
// Per-run comparison
// ============================================================================

struct RunOutput
{
    RunSummary              dijkSummary;
    RunSummary              qcastSummary;
    std::vector<RequestRecord> records;
};

static RunOutput
RunComparison (uint32_t runId,
               const std::string &topoType,
               QuantumTopologyHelper::TopologyType topoEnum,
               double edgeParam, // edge probability or avg degree
               uint32_t numNodes,
               double packetLossProb,
               uint32_t numRequests,
               uint32_t baseSeed,
               double tCohMs)
{
    // ------------------------------------------------------------------
    // 1. Build shared topology
    // ------------------------------------------------------------------
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> ();
    QuantumTopologyHelper topoHelper;
    topoHelper.SetNumNodes (numNodes);
    topoHelper.SetRandomSeed (baseSeed + runId * 17 + numNodes * 3);
    topoHelper.SetTopologyType (topoEnum);
    if (topoEnum == QuantumTopologyHelper::ERDOS_RENYI)
        topoHelper.SetEdgeProbability (edgeParam);
    else
        topoHelper.SetAverageDegree (edgeParam);
    topoHelper.SetLinkQualityRange (0.85, 0.995, 0.80, 0.95);

    NodeContainer nodes = topoHelper.GenerateTopology (qphyent);
    auto edges     = topoHelper.GetEdges ();
    auto linkProps = topoHelper.GetLinkProperties ();

    // Assign random per-link latencies [5 ms, 50 ms] for richer delay study
    std::mt19937 latRng (baseSeed + runId * 31 + 7);
    std::uniform_real_distribution<double> latDist (5.0, 50.0);

    TopoMap topology;
    for (const auto &edge : edges)
    {
        LinkMetrics m;
        m.fidelity    = linkProps[edge].first;
        m.successRate = linkProps[edge].second;
        m.latency     = latDist (latRng);  
        m.isAvailable = true;
        topology[edge.first][edge.second]  = m;
        topology[edge.second][edge.first]  = m;
    }

    // ------------------------------------------------------------------
    // 2. Generate shared requests
    // ------------------------------------------------------------------
    std::mt19937 reqRng (baseSeed + runId * 11 + 5);
    std::uniform_int_distribution<uint32_t> nodeDist (0, numNodes - 1);

    std::vector<std::pair<std::string, std::string>> requests;
    for (uint32_t i = 0; i < numRequests; ++i)
    {
        uint32_t s, d;
        do { s = nodeDist (reqRng); d = nodeDist (reqRng); } while (s == d);
        requests.push_back ({"Node" + std::to_string (s), "Node" + std::to_string (d)});
    }

    // ------------------------------------------------------------------
    // 3. Pre-generate link states (SAME for both protocols)
    // ------------------------------------------------------------------
    std::mt19937 lossRng (baseSeed + runId * 23 + 13);
    LinkStateMap linkStates = GenerateLinkStates (topology, packetLossProb, lossRng);

    // ------------------------------------------------------------------
    // 4. Dijkstra
    // ------------------------------------------------------------------
    auto dijkProt = CreateObject<DijkstraRoutingProtocol> ();
    dijkProt->UpdateTopology (topology);

    std::vector<RequestRecord> allRecords;

    auto makeSummary = [&](const std::string &proto) -> RunSummary {
        RunSummary s;
        s.runId              = runId;
        s.topoType           = topoType;
        s.numNodes           = numNodes;
        s.packetLossProb     = packetLossProb;
        s.protocol           = proto;
        s.requestsSent       = numRequests;
        s.requestsSucceeded  = 0;
        s.requestsFailed     = 0;
        s.noRoute            = 0;
        s.successRate        = 0.0;
        s.avgHops            = 0.0;
        s.avgLinkFidelity    = 0.0;
        s.avgDecoherenceFactor = 1.0;
        s.avgEndToEndFidelity  = 0.0;
        s.minEndToEndFidelity  = 1.0;
        s.maxEndToEndFidelity  = 0.0;
        s.avgTotalDelayMs    = 0.0;
        s.recoveryPathsUsed  = 0;
        s.avgRecoveryPerSuccess = 0.0;
        return s;
    };

    RunSummary dijkSummary = makeSummary ("Dijkstra");
    {
        double sumHops = 0, sumLinkFid = 0, sumDecFactor = 0;
        double sumE2E = 0, sumDelay = 0;
        double minE2E = 1.0, maxE2E = 0.0;
        for (const auto &[src, dst] : requests)
        {
            auto route = dijkProt->CalculateRoute (src, dst);
            auto rec   = SimulateDijkstraRequest (runId, topoType, numNodes,
                                                   packetLossProb, tCohMs,
                                                   src, dst, route, linkStates);
            allRecords.push_back (rec);
            if (!rec.routeFound)
                ++dijkSummary.noRoute;
            if (rec.succeeded)
            {
                ++dijkSummary.requestsSucceeded;
                sumHops      += rec.hops;
                sumLinkFid   += rec.linkFidelity;
                sumDecFactor += rec.decoherenceFactor;
                sumE2E       += rec.endToEndFidelity;
                sumDelay     += rec.totalDelayMs;
                minE2E = std::min (minE2E, rec.endToEndFidelity);
                maxE2E = std::max (maxE2E, rec.endToEndFidelity);
            }
            else
                ++dijkSummary.requestsFailed;
        }
        uint32_t n = dijkSummary.requestsSucceeded;
        dijkSummary.successRate           = 100.0 * n / numRequests;
        if (n > 0)
        {
            dijkSummary.avgHops               = sumHops / n;
            dijkSummary.avgLinkFidelity       = sumLinkFid / n;
            dijkSummary.avgDecoherenceFactor  = sumDecFactor / n;
            dijkSummary.avgEndToEndFidelity   = sumE2E / n;
            dijkSummary.minEndToEndFidelity   = minE2E;
            dijkSummary.maxEndToEndFidelity   = maxE2E;
            dijkSummary.avgTotalDelayMs       = sumDelay / n;
        }
    }

    // ------------------------------------------------------------------
    // 5. Q-CAST (per-request, preserving recovery path isolation)
    // ------------------------------------------------------------------
    auto qcastProt = CreateObject<QCastRoutingProtocol> ();
    qcastProt->UpdateTopology (topology);

    RunSummary qcastSummary = makeSummary ("QCAST");
    {
        double sumHops = 0, sumLinkFid = 0, sumDecFactor = 0;
        double sumE2E = 0, sumDelay = 0;
        double minE2E = 1.0, maxE2E = 0.0;
        for (uint32_t i = 0; i < static_cast<uint32_t> (requests.size ()); ++i)
        {
            const auto &[src, dst] = requests[i];
            QCastRequest req;
            req.requestId  = i;
            req.srcNode    = src;
            req.dstNode    = dst;
            req.minFidelity = 0.0;

            auto paths = qcastProt->CalculateRoutesGEDA ({req});
            if (paths.empty ())
            {
                ++qcastSummary.noRoute;
                ++qcastSummary.requestsFailed;
                RequestRecord r;
                r.runId = runId; r.topoType = topoType; r.numNodes = numNodes;
                r.packetLossProb = packetLossProb; r.protocol = "QCAST";
                r.src = src; r.dst = dst; r.tCohMs = tCohMs;
                r.routeFound = false; r.succeeded = false;
                r.hops = 0; r.recoveryUsed = 0;
                r.linkFidelity = 0; r.decoherenceFactor = 1; r.endToEndFidelity = 0;
                r.totalDelayMs = 0;
                allRecords.push_back (r);
                continue;
            }

            const QCastPath &qcastPath = paths.begin ()->second;
            auto rec = SimulateQCastRequest (runId, topoType, numNodes,
                                              packetLossProb, tCohMs,
                                              src, dst, qcastPath, linkStates);
            allRecords.push_back (rec);

            if (rec.succeeded)
            {
                ++qcastSummary.requestsSucceeded;
                sumHops      += rec.hops;
                sumLinkFid   += rec.linkFidelity;
                sumDecFactor += rec.decoherenceFactor;
                sumE2E       += rec.endToEndFidelity;
                sumDelay     += rec.totalDelayMs;
                qcastSummary.recoveryPathsUsed += rec.recoveryUsed;
                minE2E = std::min (minE2E, rec.endToEndFidelity);
                maxE2E = std::max (maxE2E, rec.endToEndFidelity);
            }
            else
                ++qcastSummary.requestsFailed;
        }
        uint32_t n = qcastSummary.requestsSucceeded;
        qcastSummary.successRate          = 100.0 * n / numRequests;
        if (n > 0)
        {
            qcastSummary.avgHops              = sumHops / n;
            qcastSummary.avgLinkFidelity      = sumLinkFid / n;
            qcastSummary.avgDecoherenceFactor = sumDecFactor / n;
            qcastSummary.avgEndToEndFidelity  = sumE2E / n;
            qcastSummary.minEndToEndFidelity  = minE2E;
            qcastSummary.maxEndToEndFidelity  = maxE2E;
            qcastSummary.avgTotalDelayMs      = sumDelay / n;
            qcastSummary.avgRecoveryPerSuccess =
                static_cast<double> (qcastSummary.recoveryPathsUsed) / n;
        }
    }

    Simulator::Destroy ();
    return {dijkSummary, qcastSummary, allRecords};
}

// ============================================================================
// Statistics helpers
// ============================================================================

struct AggRow
{
    std::string topoType;
    uint32_t    numNodes;
    double      packetLossProb;
    std::string protocol;
    uint32_t    numRuns;

    double meanSuccessRate;
    double stdSuccessRate;
    double meanAvgHops;
    double meanAvgLinkFidelity;
    double meanAvgDecoherenceFactor;
    double meanAvgEndToEndFidelity;
    double stdAvgEndToEndFidelity;
    double meanMinEndToEndFidelity;
    double meanMaxEndToEndFidelity;
    double meanAvgTotalDelayMs;
    double meanRecoveryUsed;
};

static double
Mean (const std::vector<double> &v)
{
    if (v.empty ()) return 0.0;
    return std::accumulate (v.begin (), v.end (), 0.0) / v.size ();
}

static double
Stddev (const std::vector<double> &v)
{
    if (v.size () < 2) return 0.0;
    double m = Mean (v);
    double sq = 0;
    for (double x : v) sq += (x - m) * (x - m);
    return std::sqrt (sq / (v.size () - 1));
}

// ============================================================================
// Output helpers
// ============================================================================

static const char *kRunHdr =
    "run_id,topo_type,num_nodes,packet_loss_prob,protocol,"
    "requests_sent,requests_succeeded,requests_failed,no_route,"
    "success_rate,avg_hops,avg_link_fidelity,avg_decoherence_factor,"
    "avg_e2e_fidelity,min_e2e_fidelity,max_e2e_fidelity,"
    "avg_total_delay_ms,recovery_paths_used,avg_recovery_per_success\n";

static std::string
RunToCsv (const RunSummary &r)
{
    std::ostringstream o;
    o << std::fixed << std::setprecision (6);
    o << r.runId << "," << r.topoType << "," << r.numNodes << ","
      << r.packetLossProb << "," << r.protocol << ","
      << r.requestsSent << "," << r.requestsSucceeded << "," << r.requestsFailed << ","
      << r.noRoute << ","
      << r.successRate << "," << r.avgHops << ","
      << r.avgLinkFidelity << "," << r.avgDecoherenceFactor << ","
      << r.avgEndToEndFidelity << "," << r.minEndToEndFidelity << ","
      << r.maxEndToEndFidelity << "," << r.avgTotalDelayMs << ","
      << r.recoveryPathsUsed << "," << r.avgRecoveryPerSuccess;
    return o.str ();
}

static const char *kReqHdr =
    "run_id,topo_type,num_nodes,packet_loss_prob,protocol,src,dst,"
    "route_found,hops,succeeded,recovery_used,"
    "link_fidelity,decoherence_factor,e2e_fidelity,"
    "total_delay_ms,t_coh_ms,path\n";

static std::string
ReqToCsv (const RequestRecord &r)
{
    std::ostringstream o;
    o << std::fixed << std::setprecision (6);
    o << r.runId << "," << r.topoType << "," << r.numNodes << ","
      << r.packetLossProb << "," << r.protocol << ","
      << r.src << "," << r.dst << ","
      << (r.routeFound ? 1 : 0) << "," << r.hops << ","
      << (r.succeeded ? 1 : 0) << "," << r.recoveryUsed << ","
      << r.linkFidelity << "," << r.decoherenceFactor << ","
      << r.endToEndFidelity << "," << r.totalDelayMs << ","
      << r.tCohMs << ",\"" << r.pathStr << "\"";
    return o.str ();
}

static std::string
AggToCsv (const AggRow &a)
{
    std::ostringstream o;
    o << std::fixed << std::setprecision (6);
    o << a.topoType << "," << a.numNodes << "," << a.packetLossProb << ","
      << a.protocol << "," << a.numRuns << ","
      << a.meanSuccessRate << "," << a.stdSuccessRate << ","
      << a.meanAvgHops << ","
      << a.meanAvgLinkFidelity << ","
      << a.meanAvgDecoherenceFactor << ","
      << a.meanAvgEndToEndFidelity << "," << a.stdAvgEndToEndFidelity << ","
      << a.meanMinEndToEndFidelity << "," << a.meanMaxEndToEndFidelity << ","
      << a.meanAvgTotalDelayMs << "," << a.meanRecoveryUsed;
    return o.str ();
}

// ============================================================================
// main
// ============================================================================
int
main (int argc, char *argv[])
{
    LogComponentEnable ("DijkstraVsQCast", LOG_LEVEL_INFO);

    uint32_t numRequests = 30;
    uint32_t numRuns     = 30;
    uint32_t baseSeed    = 42;
    double   tCohMs      = 100.0;  // quantum memory coherence time [ms]
    std::string resultsRoot = ""; // will be set below

    CommandLine cmd;
    cmd.AddValue ("numRequests", "Requests per run",          numRequests);
    cmd.AddValue ("numRuns",     "Independent runs per config", numRuns);
    cmd.AddValue ("baseSeed",    "Base RNG seed",             baseSeed);
    cmd.AddValue ("tCohMs",      "Memory coherence time [ms]", tCohMs);
    cmd.AddValue ("resultsRoot", "Override results directory", resultsRoot);
    cmd.Parse (argc, argv);

    // ------------------------------------------------------------------
    // Create timestamped results directory
    // ------------------------------------------------------------------
    // Find the script's workspace root
    std::string scriptDir = "";
    {
        // Try to locate contrib/quantum relative to working directory
        std::ifstream test ("contrib/quantum/AGENTS.md");
        if (test.good ())
            scriptDir = "contrib/quantum/results";
        else
        {
            // Try two levels up
            std::ifstream test2 ("../../contrib/quantum/AGENTS.md");
            if (test2.good ())
                scriptDir = "../../contrib/quantum/results";
            else
                scriptDir = "results";
        }
    }

    if (!resultsRoot.empty ())
        scriptDir = resultsRoot;

    // Create base results dir (may already exist)
    std::string mkdir_cmd = "mkdir -p " + scriptDir;
    (void)system (mkdir_cmd.c_str ());

    // Timestamped sub-folder
    std::time_t now = std::time (nullptr);
    char tsBuf[32];
    std::strftime (tsBuf, sizeof (tsBuf), "%Y%m%d_%H%M%S", std::localtime (&now));
    std::string outDir = scriptDir + "/" + tsBuf;
    std::string mkdir2 = "mkdir -p " + outDir;
    (void)system (mkdir2.c_str ());

    NS_LOG_INFO ("Results will be written to: " << outDir);

    // ------------------------------------------------------------------
    // Topology configurations to sweep
    // ------------------------------------------------------------------
    struct TopoCfg
    {
        std::string                         name;
        QuantumTopologyHelper::TopologyType type;
        double                              param; // edge prob or avg degree
    };

    std::vector<TopoCfg> topos = {
        {"ErdosRenyi",       QuantumTopologyHelper::ERDOS_RENYI,       0.35},
        {"RandomGeometric",  QuantumTopologyHelper::RANDOM_GEOMETRIC,  4.0},  // avg degree
        {"ScaleFree",        QuantumTopologyHelper::SCALE_FREE,        3.0},  // avg degree
    };

    std::vector<uint32_t> nodeSizes  = {8, 12, 20, 30};
    std::vector<double>   lossRates  = {0.00, 0.05, 0.10, 0.15, 0.20, 0.25, 0.30};

    // ------------------------------------------------------------------
    // Open output files
    // ------------------------------------------------------------------
    std::ofstream fAllRuns  (outDir + "/all_runs.csv");
    std::ofstream fPerReq   (outDir + "/per_request.csv");

    fAllRuns  << kRunHdr;
    fPerReq   << kReqHdr;

    // Per-topo slices
    std::map<std::string, std::ofstream> fTopos;
    for (const auto &tc : topos)
    {
        fTopos[tc.name].open (outDir + "/topology_" + tc.name + ".csv");
        fTopos[tc.name] << kRunHdr;
    }

    // Accumulate all run summaries for aggregation
    std::vector<RunSummary> allSummaries;
    uint32_t totalConfigs = static_cast<uint32_t> (topos.size () * nodeSizes.size () * lossRates.size ());
    uint32_t configsDone  = 0;

    // ------------------------------------------------------------------
    // Main sweep
    // ------------------------------------------------------------------
    for (const auto &tc : topos)
    {
        for (uint32_t N : nodeSizes)
        {
            for (double loss : lossRates)
            {
                NS_LOG_INFO ("Config " << ++configsDone << "/" << totalConfigs
                                       << " | topo=" << tc.name
                                       << " nodes=" << N
                                       << " loss=" << (loss * 100.0) << "%  ("
                                       << numRuns << " runs x " << numRequests << " reqs)");

                for (uint32_t run = 1; run <= numRuns; ++run)
                {
                    auto out = RunComparison (run, tc.name, tc.type, tc.param,
                                             N, loss, numRequests, baseSeed, tCohMs);

                    // Write run summaries
                    std::string dRow = RunToCsv (out.dijkSummary);
                    std::string qRow = RunToCsv (out.qcastSummary);
                    fAllRuns << dRow << "\n" << qRow << "\n";
                    fTopos[tc.name] << dRow << "\n" << qRow << "\n";

                    // Write per-request records
                    for (const auto &rec : out.records)
                        fPerReq << ReqToCsv (rec) << "\n";

                    allSummaries.push_back (out.dijkSummary);
                    allSummaries.push_back (out.qcastSummary);
                }
            }
        }
    }

    fAllRuns.close ();
    fPerReq.close ();
    for (auto &[n, f] : fTopos)
        f.close ();

    NS_LOG_INFO ("All runs complete.  Computing aggregated statistics ...");

    // ------------------------------------------------------------------
    // Aggregate: mean ± stddev over runs for each (topo, N, loss, proto)
    // ------------------------------------------------------------------
    std::ofstream fAgg (outDir + "/aggregated.csv");
    fAgg << "topo_type,num_nodes,packet_loss_prob,protocol,num_runs,"
            "mean_success_rate,std_success_rate,"
            "mean_avg_hops,"
            "mean_avg_link_fidelity,"
            "mean_avg_decoherence_factor,"
            "mean_avg_e2e_fidelity,std_avg_e2e_fidelity,"
            "mean_min_e2e_fidelity,mean_max_e2e_fidelity,"
            "mean_avg_total_delay_ms,"
            "mean_recovery_used\n";

    std::vector<AggRow> aggRows;

    for (const auto &tc : topos)
    {
        for (uint32_t N : nodeSizes)
        {
            for (double loss : lossRates)
            {
                for (const std::string &proto : {"Dijkstra", "QCAST"})
                {
                    std::vector<double> vSucc, vHops, vLinkFid, vDecFactor;
                    std::vector<double> vE2E, vMinE2E, vMaxE2E, vDelay, vRecov;
                    for (const auto &s : allSummaries)
                    {
                        if (s.topoType != tc.name || s.numNodes != N ||
                            s.packetLossProb != loss || s.protocol != proto)
                            continue;
                        vSucc.push_back (s.successRate);
                        vHops.push_back (s.avgHops);
                        vLinkFid.push_back (s.avgLinkFidelity);
                        vDecFactor.push_back (s.avgDecoherenceFactor);
                        vE2E.push_back (s.avgEndToEndFidelity);
                        vMinE2E.push_back (s.minEndToEndFidelity);
                        vMaxE2E.push_back (s.maxEndToEndFidelity);
                        vDelay.push_back (s.avgTotalDelayMs);
                        vRecov.push_back (s.recoveryPathsUsed);
                    }
                    if (vSucc.empty ())
                        continue;

                    AggRow a;
                    a.topoType                    = tc.name;
                    a.numNodes                    = N;
                    a.packetLossProb              = loss;
                    a.protocol                    = proto;
                    a.numRuns                     = static_cast<uint32_t> (vSucc.size ());
                    a.meanSuccessRate             = Mean (vSucc);
                    a.stdSuccessRate              = Stddev (vSucc);
                    a.meanAvgHops                 = Mean (vHops);
                    a.meanAvgLinkFidelity         = Mean (vLinkFid);
                    a.meanAvgDecoherenceFactor    = Mean (vDecFactor);
                    a.meanAvgEndToEndFidelity     = Mean (vE2E);
                    a.stdAvgEndToEndFidelity      = Stddev (vE2E);
                    a.meanMinEndToEndFidelity     = Mean (vMinE2E);
                    a.meanMaxEndToEndFidelity     = Mean (vMaxE2E);
                    a.meanAvgTotalDelayMs         = Mean (vDelay);
                    a.meanRecoveryUsed            = Mean (vRecov);
                    fAgg << AggToCsv (a) << "\n";
                    aggRows.push_back (a);
                }
            }
        }
    }
    fAgg.close ();

    // ------------------------------------------------------------------
    // Write human-readable summary.txt
    // ------------------------------------------------------------------
    std::ofstream fSummary (outDir + "/summary.txt");
    fSummary << "===================================================================\n";
    fSummary << "  Dijkstra vs Q-CAST — Comprehensive Comparison\n";
    fSummary << "  Generated: " << tsBuf << "\n";
    fSummary << "  Runs per config: " << numRuns
             << "  |  Requests per run: " << numRequests << "\n";
    fSummary << "  Coherence time T_coh = " << tCohMs << " ms\n";
    fSummary << "===================================================================\n\n";

    for (const auto &tc : topos)
    {
        for (uint32_t N : nodeSizes)
        {
            fSummary << "Topology: " << tc.name << "  |  Nodes: " << N << "\n";
            fSummary << std::string (72, '-') << "\n";
            fSummary << std::left
                     << std::setw (8)  << "Loss%"
                     << std::setw (14) << "Dijkstra Succ"
                     << std::setw (14) << "Q-CAST Succ"
                     << std::setw (12) << "Advantage"
                     << std::setw (16) << "Dijkstra E2E F"
                     << std::setw (16) << "Q-CAST E2E F"
                     << std::setw (14) << "DecohFactor"
                     << std::setw (12) << "RecovUsed"
                     << "\n";

            for (double loss : lossRates)
            {
                double dSucc = 0, qSucc = 0, dE2E = 0, qE2E = 0;
                double qDecF = 0, qRecov = 0;
                bool found = false;
                for (const auto &a : aggRows)
                {
                    if (a.topoType != tc.name || a.numNodes != N || a.packetLossProb != loss)
                        continue;
                    found = true;
                    if (a.protocol == "Dijkstra")
                    { dSucc = a.meanSuccessRate; dE2E = a.meanAvgEndToEndFidelity; }
                    else
                    { qSucc = a.meanSuccessRate; qE2E = a.meanAvgEndToEndFidelity;
                      qDecF = a.meanAvgDecoherenceFactor; qRecov = a.meanRecoveryUsed; }
                }
                if (!found) continue;
                fSummary << std::fixed << std::setprecision (2)
                         << std::setw (8)  << (loss * 100.0)
                         << std::setw (14) << dSucc
                         << std::setw (14) << qSucc
                         << std::setw (12) << (qSucc - dSucc)
                         << std::setw (16) << dE2E
                         << std::setw (16) << qE2E
                         << std::setw (14) << qDecF
                         << std::setw (12) << qRecov
                         << "\n";
            }
            fSummary << "\n";
        }
    }
    fSummary.close ();

    // ------------------------------------------------------------------
    // Print condensed stdout table (ErdosRenyi, 12 nodes only)
    // ------------------------------------------------------------------
    std::cout << "\n=== Quick Summary (ErdosRenyi, N=12) ===\n\n";
    std::cout << std::fixed << std::setprecision (2);
    std::cout << std::left
              << std::setw (8)  << "Loss%"
              << std::setw (14) << "Dijkstra Succ"
              << std::setw (14) << "Q-CAST Succ"
              << std::setw (12) << "Advantage"
              << std::setw (16) << "D E2E Fid"
              << std::setw (16) << "Q E2E Fid"
              << std::setw (14) << "Decoherence"
              << std::setw (12) << "Recovery"
              << "\n"
              << std::string (106, '-') << "\n";

    for (double loss : lossRates)
    {
        double dS = 0, qS = 0, dF = 0, qF = 0, qD = 0, qR = 0;
        for (const auto &a : aggRows)
        {
            if (a.topoType != "ErdosRenyi" || a.numNodes != 12 || a.packetLossProb != loss)
                continue;
            if (a.protocol == "Dijkstra") { dS = a.meanSuccessRate; dF = a.meanAvgEndToEndFidelity; }
            else { qS = a.meanSuccessRate; qF = a.meanAvgEndToEndFidelity;
                   qD = a.meanAvgDecoherenceFactor; qR = a.meanRecoveryUsed; }
        }
        std::cout << std::setw (8)  << (loss * 100.0)
                  << std::setw (14) << dS
                  << std::setw (14) << qS
                  << std::setw (12) << (qS - dS)
                  << std::setw (16) << dF
                  << std::setw (16) << qF
                  << std::setw (14) << qD
                  << std::setw (12) << qR
                  << "\n";
    }

    NS_LOG_INFO ("\n=== Output files ===");
    NS_LOG_INFO ("  " << outDir << "/all_runs.csv    — per-run aggregates");
    NS_LOG_INFO ("  " << outDir << "/per_request.csv  — every individual request");
    NS_LOG_INFO ("  " << outDir << "/aggregated.csv   — mean+stddev across runs");
    NS_LOG_INFO ("  " << outDir << "/summary.txt       — human-readable tables");
    NS_LOG_INFO ("  " << outDir << "/topology_<name>.csv — per-topology slices");

    return 0;
}

