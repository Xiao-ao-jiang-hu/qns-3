/*
 * routing-mismatch-example.cc
 *
 * Demonstrates routing non-monotonicity using a RANDOMLY GENERATED topology.
 *
 * The QuantumMismatchTopologyHelper generates a bimodal topology:
 *   - ~10% "trap" links:    high success rate (0.95), near-perfect fidelity (0.999),
 *                           500 ms classical signaling delay, endpoints have tau=10000 s
 *   - ~90% "highway" links: normal success rate U[0.88,0.97], fidelity U[0.88,0.95],
 *                            5 ms signaling delay, non-endpoint nodes have tau=1 s
 *
 * From the random topology we automatically locate a "mismatch diamond":
 *   SrcA --(trap hops)--> PivotM ---> Dst
 *   SrcB --(hwy  hops)--> PivotM ---> Dst
 *
 * Q-CAST (E_t metric) favours Path A because trap links have higher success rates.
 * But the 500 ms signaling delay forces PivotM's qubits (tau=1 s) to wait and
 * decohere heavily -- physical fidelity F(A) < F(B) despite E_t(A) > E_t(B).
 *
 * ALL fidelity values are computed by ExaTN density matrix contractions.
 * No analytical formulas are used for any physics calculation.
 *
 * Both Path A and Path B teleportation chains share ONE Simulator::Run() to
 * avoid multiple ExaTN init/finalize cycles. Qubit names are prefixed "A_"/"B_".
 *
 * Usage:
 *   ./ns3 run routing-mismatch-example [-- --seed=42 --numNodes=20]
 *   NS_LOG="RoutingMismatchExample=info" ./ns3 run routing-mismatch-example
 */

#include "ns3/core-module.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-error-model.h"
#include "ns3/quantum-mismatch-topology-helper.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-routing-protocol.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <limits>
#include <stdexcept>

NS_LOG_COMPONENT_DEFINE ("RoutingMismatchExample");

using namespace ns3;

// =============================================================================
// Path descriptor: a 3-hop chain  Src -> Relay -> Pivot -> Dst
// (Relay may equal Src if the Src->Pivot link exists directly)
// =============================================================================

struct PathDesc
{
    std::string src;
    std::string relay;   // may equal src  (direct 2-hop path: src->pivot->dst)
    std::string pivot;
    std::string dst;
    bool        hasTrap; // true if any hop on Src..Pivot is a trap link

    // Cumulative statistics over Src-->Pivot hops (not including Pivot->Dst)
    double srSrcRelay   {1.0};  // success rate of Src->Relay link
    double fidSrcRelay  {1.0};  // fidelity of Src->Relay link
    double srRelayPivot {1.0};  // success rate of Relay->Pivot link
    double fidRelayPivot{1.0};  // fidelity of Relay->Pivot link
    double delayMsRelay {0.0};  // classical signalling delay Relay->Pivot
    double tauSrc       {1.0};
    double tauRelay     {1.0};  // may equal tauSrc for direct paths

    bool directHop () const { return relay == src; }
};

// =============================================================================
// Topology search: find a mismatch diamond
// =============================================================================

// Build adjacency map from EdgeProps map.
static std::map<std::string, std::vector<std::string>>
BuildAdj (const std::vector<std::pair<std::string, std::string>> &edges)
{
    std::map<std::string, std::vector<std::string>> adj;
    for (const auto &e : edges)
        {
            adj[e.first].push_back (e.second);
            adj[e.second].push_back (e.first);
        }
    return adj;
}

// Try to find two candidate paths (trap and highway) that share the same
// Pivot->Dst segment.  Returns true on success and fills pathA (trap) and
// pathB (highway).
//
// Strategy:
//  1. Collect all Pivot->Dst edges (highway only -- to keep the shared hop fair).
//  2. For each such (pivot, dst) pair:
//       a. Find all 1-hop and 2-hop routes to 'pivot' that include >=1 trap link.
//            -> candidates for Path A
//       b. Find all 1-hop and 2-hop routes to 'pivot' with ONLY highway links.
//            -> candidates for Path B
//       c. Choose the best-E_t representative from each set (must differ in src).
//  3. Return the first Pivot that yields a valid (A, B) pair.
//
//  Fallback: if random topology search fails, use the injected backbone nodes
//  (if backbone injection is enabled in the helper) to build the guaranteed pair.
static bool
FindMismatchDiamond (
    const QuantumMismatchTopologyHelper &helper,
    PathDesc &pathA,
    PathDesc &pathB)
{
    const auto &edges    = helper.GetEdges ();
    const auto &props    = helper.GetEdgeProps ();
    const auto &nodeTaus = helper.GetNodeTaus ();
    auto adj = BuildAdj (edges);

    // Lambda: get EdgeProps for directed edge (u,v)
    auto getProps = [&] (const std::string &u, const std::string &v)
        -> const QuantumMismatchTopologyHelper::EdgeProps &
    {
        return props.at ({u, v});
    };

    // Lambda: estimated E_t for a chain of (sr1, sr2, ...) using Q-CAST formula
    // E_t = prod(sr) * S(h),  S(h) = h^(-0.1 * log2(h))   (simplified, monotone in sr)
    // For comparison purposes between same-hop-count paths we use prod(sr).
    auto etApprox = [] (const std::vector<double> &srs) {
        double prod = 1.0;
        for (double s : srs) prod *= s;
        double h = static_cast<double> (srs.size ());
        // S(h) factor from Q-CAST equals 1 when h<=1
        if (h > 1.0) prod *= std::pow (h, -0.1 * std::log2 (h));
        return prod;
    };

    for (const auto &pivotDstEdge : edges)
        {
            const std::string &pivot = pivotDstEdge.first;
            const std::string &dst   = pivotDstEdge.second;
            const auto &pdProps = getProps (pivot, dst);

            // Skip trap Pivot->Dst links -- we want a neutral shared hop.
            if (pdProps.isTrap) continue;

            // Pivot must be a garbage node (tau=1s) so 500ms signaling delay
            // on path A causes significant decoherence and kills the fidelity.
            // (Trap links force BOTH endpoints to be perfect nodes, so a trap
            // link directly entering 'pivot' would make pivot perfect -- that
            // would not produce non-monotonicity.  We therefore require a
            // 2-hop path for A: Src(perfect) -[trap]-> Relay(perfect) -[hwy]-> pivot(garbage).)
            if (helper.IsPerfectNode (pivot)) continue;

            // ── Enumerate candidate paths to 'pivot' ───────────────────────
            std::vector<PathDesc> trapCands, hwyCands;

            for (const std::string &nb : adj[pivot])
                {
                    if (nb == dst) continue; // exclude Dst direction

                    // --- 1-hop path: nb -> pivot (nb acts as Src, no relay) ---
                    if (adj[nb].end () != std::find (adj[nb].begin (), adj[nb].end (), dst))
                        {
                            // nb is also directly connected to dst; exclude it to avoid
                            // trivial single-hop paths that skip pivot entirely.
                        }

                    const auto &e1 = getProps (nb, pivot);
                    PathDesc pd1;
                    pd1.src           = nb;
                    pd1.relay         = nb; // direct path
                    pd1.pivot         = pivot;
                    pd1.dst           = dst;
                    pd1.hasTrap       = e1.isTrap;
                    pd1.srSrcRelay    = e1.successRate;
                    pd1.fidSrcRelay   = e1.fidelity;
                    pd1.srRelayPivot  = 1.0; // no relay hop
                    pd1.fidRelayPivot = 1.0;
                    pd1.delayMsRelay  = e1.delayMs; // src->pivot delay
                    pd1.tauSrc        = nodeTaus.at (nb);
                    pd1.tauRelay      = nodeTaus.at (nb);

                    if (e1.isTrap)
                        trapCands.push_back (pd1);
                    else
                        hwyCands.push_back (pd1);

                    // --- 2-hop paths: nb -> relay -> pivot ---
                    for (const std::string &src2 : adj[nb])
                        {
                            if (src2 == pivot || src2 == dst) continue;
                            const auto &e0 = getProps (src2, nb);
                            bool anyTrap = e0.isTrap || e1.isTrap;

                            if (!anyTrap && e0.isTrap == e1.isTrap) {
                                // both same type -- ok
                            }

                            PathDesc pd2;
                            pd2.src           = src2;
                            pd2.relay         = nb;
                            pd2.pivot         = pivot;
                            pd2.dst           = dst;
                            pd2.hasTrap       = anyTrap;
                            pd2.srSrcRelay    = e0.successRate;
                            pd2.fidSrcRelay   = e0.fidelity;
                            pd2.srRelayPivot  = e1.successRate;
                            pd2.fidRelayPivot = e1.fidelity;
                            pd2.delayMsRelay  = e1.delayMs; // relay->pivot delay is the bottleneck
                            pd2.tauSrc        = nodeTaus.at (src2);
                            pd2.tauRelay      = nodeTaus.at (nb);

                            // Avoid cycles / self-loops
                            if (pd2.src == pd2.relay || pd2.src == pd2.pivot) continue;

                            if (anyTrap)
                                trapCands.push_back (pd2);
                            else
                                hwyCands.push_back (pd2);
                        }
                }

            if (trapCands.empty () || hwyCands.empty ()) continue;

            // Sort by descending E_t approximation
            auto sorter = [&] (const PathDesc &x, const PathDesc &y) {
                std::vector<double> srsX = {x.srSrcRelay, x.srRelayPivot, pdProps.successRate};
                std::vector<double> srsY = {y.srSrcRelay, y.srRelayPivot, pdProps.successRate};
                if (x.directHop ()) srsX.erase (srsX.begin ());
                if (y.directHop ()) srsY.erase (srsY.begin ());
                return etApprox (srsX) > etApprox (srsY);
            };
            std::sort (trapCands.begin (), trapCands.end (), sorter);
            std::sort (hwyCands.begin (), hwyCands.end (), sorter);

            // Find best trap cand and best highway cand with DIFFERENT src nodes
            for (const auto &tc : trapCands)
                {
                    for (const auto &hc : hwyCands)
                        {
                            if (tc.src == hc.src) continue; // must be different paths
                            if (tc.relay == hc.relay && tc.relay != tc.src) continue; // different relay too

                            // Verify that Q-CAST would indeed prefer the trap path
                            std::vector<double> srsA = {tc.srSrcRelay, tc.srRelayPivot, pdProps.successRate};
                            std::vector<double> srsB = {hc.srSrcRelay, hc.srRelayPivot, pdProps.successRate};
                            if (tc.directHop ()) srsA.erase (srsA.begin ());
                            if (hc.directHop ()) srsB.erase (srsB.begin ());
                            if (etApprox (srsA) <= etApprox (srsB)) continue;

                            // Verify the delay difference is significant
                            if (tc.delayMsRelay <= hc.delayMsRelay) continue;

                            pathA = tc;
                            pathB = hc;
                            return true;
                        }
                }
        }

    // ── Fallback: use injected backbone nodes ─────────────────────────────
    // If backbone injection was enabled (default), the helper guarantees the
    // following diamond exists in the topology:
    //   TrapSrc -[trap]-> TrapRelay -[hwy]-> GarbagePivot -[hwy]-> Dst
    //                                        HwySrc --------[hwy]-^
    const auto &bn = helper.GetBackboneNodes ();
    if (!bn.garbagePivot.empty ())
        {
            NS_LOG_INFO ("Random search found no mismatch diamond; using injected backbone.");
            std::cout << "  [Using injected backbone diamond for mismatch demonstration.]\n";

            const auto &edgeProps = helper.GetEdgeProps ();
            const auto &nodeTaus  = helper.GetNodeTaus ();

            auto ep = [&] (const std::string &u, const std::string &v)
                -> const QuantumMismatchTopologyHelper::EdgeProps & {
                return edgeProps.at ({u, v});
            };

            // Path A: TrapSrc -> TrapRelay -[500ms hwy]-> GarbagePivot -> Dst
            const auto &eA0 = ep (bn.trapSrc,   bn.trapRelay);    // trap link, 5ms
            const auto &eA1 = ep (bn.trapRelay, bn.garbagePivot); // high-delay hwy, 500ms
            pathA.src           = bn.trapSrc;
            pathA.relay         = bn.trapRelay;
            pathA.pivot         = bn.garbagePivot;
            pathA.dst           = bn.backboneDst;
            pathA.hasTrap       = true;
            pathA.srSrcRelay    = eA0.successRate;
            pathA.fidSrcRelay   = eA0.fidelity;
            pathA.srRelayPivot  = eA1.successRate;
            pathA.fidRelayPivot = eA1.fidelity;
            pathA.delayMsRelay  = eA1.delayMs;  // TrapRelay->GarbagePivot: 500 ms
            pathA.tauSrc        = nodeTaus.at (bn.trapSrc);
            pathA.tauRelay      = nodeTaus.at (bn.trapRelay);

            // Path B: HwySrc -> HwyRelay -> GarbagePivot -> Dst  (3-hop highway)
            const auto &eB0 = ep (bn.hwySrc,   bn.hwyRelay);    // hwy link, 5ms
            const auto &eB1 = ep (bn.hwyRelay, bn.garbagePivot);// hwy link, 5ms
            pathB.src           = bn.hwySrc;
            pathB.relay         = bn.hwyRelay;
            pathB.pivot         = bn.garbagePivot;
            pathB.dst           = bn.backboneDst;
            pathB.hasTrap       = false;
            pathB.srSrcRelay    = eB0.successRate;
            pathB.fidSrcRelay   = eB0.fidelity;
            pathB.srRelayPivot  = eB1.successRate;
            pathB.fidRelayPivot = eB1.fidelity;
            pathB.delayMsRelay  = eB1.delayMs;  // HwyRelay->GarbagePivot: 5 ms
            pathB.tauSrc        = nodeTaus.at (bn.hwySrc);
            pathB.tauRelay      = nodeTaus.at (bn.hwyRelay);

            return true;
        }

    return false;
}

// =============================================================================
// Shared simulation state (filled after topology search)
// =============================================================================

static Ptr<QuantumPhyEntity> g_qpe;

// Per-path qubit names (set dynamically)
struct PathQubits
{
    std::string src_q;   // Src qubit (entangled with rL)
    std::string rL;      // Relay qubit toward Src
    std::string rR;      // Relay qubit toward Pivot  (equals rL for direct paths)
    std::string pL;      // Pivot qubit from Relay
    std::string pR;      // Pivot qubit toward Dst
    std::string dst_q;   // Dst qubit
    bool        direct;  // true if no intermediate relay node
};

static PathQubits gA_q, gB_q;
static unsigned gA_mL = 0, gA_mR = 0;
static unsigned gB_mL = 0, gB_mR = 0;
static double   gA_fidel = 0.0, gB_fidel = 0.0;
static double   gA_delayMs = 500.0, gB_delayMs = 5.0;

// =============================================================================
// Entanglement-swap helpers
// =============================================================================

static void
ApplyCorrections (const std::string &q, unsigned m_R, unsigned m_L)
{
    if (m_R == 1)
        g_qpe->ApplyGate ("God", QNS_GATE_PREFIX + "PX",
                          std::vector<std::complex<double>>{},
                          std::vector<std::string>{q});
    if (m_L == 1)
        g_qpe->ApplyGate ("God", QNS_GATE_PREFIX + "PZ",
                          std::vector<std::complex<double>>{},
                          std::vector<std::string>{q});
}

static std::pair<unsigned, unsigned>
BellMeasAndTrace (const std::string &qL, const std::string &qR)
{
    g_qpe->ApplyGate ("God", QNS_GATE_PREFIX + "CNOT",
                      std::vector<std::complex<double>>{},
                      std::vector<std::string>{qR, qL});
    g_qpe->ApplyGate ("God", QNS_GATE_PREFIX + "H",
                      std::vector<std::complex<double>>{},
                      std::vector<std::string>{qL});
    unsigned oL = g_qpe->Measure ("God", {qL}).first;
    unsigned oR = g_qpe->Measure ("God", {qR}).first;
    g_qpe->PartialTrace ({qL, qR});
    return {oL, oR};
}

// Perform entanglement swap at the relay node.
// For direct paths (no relay), this is a no-op and we call the pivot action directly.
static void DoRelaySwap (const PathQubits &q, unsigned &mL, unsigned &mR)
{
    auto [ml, mr] = BellMeasAndTrace (q.rL, q.rR);
    mL = ml; mR = mr;
}

static void FinishPath (
    const PathQubits &q,
    unsigned mL, unsigned mR,
    double &fidel,
    bool stopAfter)
{
    NS_LOG_INFO ("t=" << Simulator::Now ().As (Time::MS)
                      << "  PivotM acts (prefix=" << q.pL.substr (0, 2) << ")");
    ApplyCorrections (q.pL, mR, mL);
    auto [pL, pR] = BellMeasAndTrace (q.pL, q.pR);
    ApplyCorrections (q.dst_q, pR, pL);

    g_qpe->GetNode ("God")->EnsureDecoherence (q.src_q);
    g_qpe->GetNode ("God")->EnsureDecoherence (q.dst_q);

    g_qpe->CalculateFidelity ({q.src_q, q.dst_q}, fidel);
    NS_LOG_INFO ("  End-to-end fidelity = " << fidel);

    if (stopAfter) Simulator::Stop ();
}

// =============================================================================
// Scheduled simulation events
// =============================================================================

static void PivotActB ()
{
    FinishPath (gB_q, gB_mL, gB_mR, gB_fidel, false);
}

static void RelayActB ()
{
    NS_LOG_INFO ("t=0  Config B: Relay acts");
    if (gB_q.direct)
        {
            // No relay swap needed; pL is directly entangled with src_q.
            gB_mL = 0; gB_mR = 0;
        }
    else
        {
            DoRelaySwap (gB_q, gB_mL, gB_mR);
        }
    Simulator::Schedule (MilliSeconds (gB_delayMs), &PivotActB);
}

static void PivotActA ()
{
    FinishPath (gA_q, gA_mL, gA_mR, gA_fidel, true);
}

static void RelayActA ()
{
    NS_LOG_INFO ("t=0  Config A: Relay acts");
    if (gA_q.direct)
        {
            gA_mL = 0; gA_mR = 0;
        }
    else
        {
            DoRelaySwap (gA_q, gA_mL, gA_mR);
        }
    Simulator::Schedule (MilliSeconds (gA_delayMs), &PivotActA);
}

// =============================================================================
// Q-CAST routing analysis on the randomly generated topology
// =============================================================================

static void
RunQCastAnalysis (
    const QuantumMismatchTopologyHelper &helper,
    const PathDesc &pathA,
    const PathDesc &pathB,
    double &et_A, double &et_B)
{
    // Build a Q-CAST topology from the helper's edges.
    auto makeLM = [] (double f, double sr) {
        LinkMetrics m;
        m.fidelity    = f;
        m.successRate = sr;
        m.latency     = 0.0;
        m.isAvailable = true;
        return m;
    };

    std::map<std::string, std::map<std::string, LinkMetrics>> topo;
    const auto &edgeProps = helper.GetEdgeProps ();
    for (const auto &kv : edgeProps)
        topo[kv.first.first][kv.first.second] = makeLM (kv.second.fidelity, kv.second.successRate);

    // Use separate QCast instances to get independent E_t values.
    Ptr<QCastRoutingProtocol> qcastA = CreateObject<QCastRoutingProtocol> ();
    qcastA->UpdateTopology (topo);
    Ptr<QCastRoutingProtocol> qcastB = CreateObject<QCastRoutingProtocol> ();
    qcastB->UpdateTopology (topo);

    QCastRequest reqA {};
    reqA.srcNode = pathA.src; reqA.dstNode = pathA.dst;
    reqA.minFidelity = 0.0; reqA.requestId = 1;

    QCastRequest reqB {};
    reqB.srcNode = pathB.src; reqB.dstNode = pathB.dst;
    reqB.minFidelity = 0.0; reqB.requestId = 2;

    auto resA = qcastA->CalculateRoutesGEDA ({reqA});
    auto resB = qcastB->CalculateRoutesGEDA ({reqB});

    et_A = resA.empty () ? -1.0 : resA.begin ()->second.primaryEt;
    et_B = resB.empty () ? -1.0 : resB.begin ()->second.primaryEt;

    std::cout << "\n=== Q-CAST Routing Analysis (random topology) ===\n\n";

    auto printPath = [&] (char label, const PathDesc &pd, double et,
                          Ptr<QCastRoutingProtocol> qc,
                          const std::map<uint32_t, QCastPath> &res)
    {
        std::cout << "  Path " << label
                  << (pd.hasTrap ? " (trap):   " : " (highway):") << "  "
                  << pd.src;
        if (!pd.directHop ()) std::cout << " -> " << pd.relay;
        std::cout << " -> " << pd.pivot << " -> " << pd.dst << "\n";
        if (!res.empty ())
            std::cout << "    Q-CAST route: "
                      << qc->RouteToString (res.begin ()->second.primaryPath) << "\n";
        std::cout << "    E_t = " << std::fixed << std::setprecision (4) << et
                  << "   signaling delay to pivot: " << pd.delayMsRelay << " ms\n\n";
    };

    printPath ('A', pathA, et_A, qcastA, resA);
    printPath ('B', pathB, et_B, qcastB, resB);

    std::cout << "  => Q-CAST selects: "
              << (et_A >= et_B ? "Path A (trap)" : "Path B (highway)")
              << "  [higher E_t]\n";
}

// =============================================================================
// Setup physical simulation from randomly found paths
// =============================================================================

static void
SetupPhysicalSim (
    const PathDesc &pathA,
    const PathDesc &pathB,
    const QuantumMismatchTopologyHelper &helper)
{
    const auto &nodeTaus = helper.GetNodeTaus ();
    const auto &edgeProps = helper.GetEdgeProps ();

    // Shared Pivot->Dst link fidelity (same for both paths).
    auto &pdProps = edgeProps.at ({pathA.pivot, pathA.dst});
    double fidPivotDst = pdProps.fidelity;

    // Print simulation parameters.
    std::cout << "\n=== Physical Simulation (ExaTN Density Matrices) ===\n\n";

    auto printCfg = [&] (char label, const PathDesc &pd) {
        std::cout << "  Config " << label
                  << (pd.hasTrap ? "  (trap):   " : "  (highway):")
                  << "  " << pd.src;
        if (!pd.directHop ()) std::cout << " -> " << pd.relay;
        std::cout << " -> " << pd.pivot << " -> " << pd.dst << "\n"
                  << "    tau_src=" << nodeTaus.at (pd.src) << "s"
                  << "  tau_relay=" << (pd.directHop () ? nodeTaus.at (pd.src) : nodeTaus.at (pd.relay)) << "s"
                  << "  tau_pivot=" << nodeTaus.at (pd.pivot) << "s"
                  << "  tau_dst=" << nodeTaus.at (pd.dst) << "s\n"
                  << "    link fidelities: "
                  << std::fixed << std::setprecision (3)
                  << pd.fidSrcRelay;
        if (!pd.directHop ()) std::cout << " / " << pd.fidRelayPivot;
        std::cout << " / " << fidPivotDst << "\n"
                  << "    signaling delay to pivot: " << pd.delayMsRelay << " ms\n\n";
    };
    printCfg ('A', pathA);
    printCfg ('B', pathB);
    std::cout << "  Running simulation...\n";

    // ── Create QuantumPhyEntity ───────────────────────────────────────────
    g_qpe = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
    g_qpe->SetTimeModel ("God", 1e6);

    // ── Helper: assign qubit names and store in PathQubits struct ─────────
    auto buildQ = [] (const std::string &prefix, const PathDesc &pd) -> PathQubits {
        PathQubits q;
        q.src_q = prefix + "src";
        q.rL    = prefix + "rL";
        q.rR    = prefix + "rR";  // same as rL for direct paths
        q.pL    = prefix + "pL";
        q.pR    = prefix + "pR";
        q.dst_q = prefix + "dst";
        q.direct = pd.directHop ();
        return q;
    };

    gA_q = buildQ ("A_", pathA);
    gB_q = buildQ ("B_", pathB);
    gA_delayMs = pathA.delayMsRelay;
    gB_delayMs = pathB.delayMsRelay;

    // ── Depolar channel models ────────────────────────────────────────────
    // Path A
    g_qpe->SetDepolarModel ({"A_link0a", "A_link0b"}, pathA.fidSrcRelay);
    if (!pathA.directHop ())
        g_qpe->SetDepolarModel ({"A_link1a", "A_link1b"}, pathA.fidRelayPivot);
    g_qpe->SetDepolarModel ({"A_linkPa", "A_linkPb"}, fidPivotDst);
    // Path B
    g_qpe->SetDepolarModel ({"B_link0a", "B_link0b"}, pathB.fidSrcRelay);
    if (!pathB.directHop ())
        g_qpe->SetDepolarModel ({"B_link1a", "B_link1b"}, pathB.fidRelayPivot);
    g_qpe->SetDepolarModel ({"B_linkPa", "B_linkPb"}, fidPivotDst);

    // ── Generate Bell pairs ───────────────────────────────────────────────
    // Path A
    if (pathA.directHop ()) {
        // Src -> Pivot direct: only one EPR pair before pivot
        g_qpe->GenerateQubitsPure ("God", q_bell, {gA_q.src_q, gA_q.pL});
    } else {
        g_qpe->GenerateQubitsPure ("God", q_bell, {gA_q.src_q, gA_q.rL});
        g_qpe->GenerateQubitsPure ("God", q_bell, {gA_q.rR,    gA_q.pL});
    }
    g_qpe->GenerateQubitsPure ("God", q_bell, {gA_q.pR, gA_q.dst_q});

    // Path B
    if (pathB.directHop ()) {
        g_qpe->GenerateQubitsPure ("God", q_bell, {gB_q.src_q, gB_q.pL});
    } else {
        g_qpe->GenerateQubitsPure ("God", q_bell, {gB_q.src_q, gB_q.rL});
        g_qpe->GenerateQubitsPure ("God", q_bell, {gB_q.rR,    gB_q.pL});
    }
    g_qpe->GenerateQubitsPure ("God", q_bell, {gB_q.pR, gB_q.dst_q});

    // ── Per-qubit TimeModels ──────────────────────────────────────────────
    auto setTau = [&] (const std::string &q, double tau) {
        g_qpe->SetErrorModel (CreateObject<TimeModel> (tau), q);
    };
    double tauPivot = nodeTaus.at (pathA.pivot); // same pivot for both paths
    double tauDst   = nodeTaus.at (pathA.dst);

    // Path A
    setTau (gA_q.src_q, nodeTaus.at (pathA.src));
    if (!pathA.directHop ()) {
        setTau (gA_q.rL,    nodeTaus.at (pathA.src));
        setTau (gA_q.rR,    nodeTaus.at (pathA.relay));
    }
    setTau (gA_q.pL,    tauPivot);
    setTau (gA_q.pR,    tauPivot);
    setTau (gA_q.dst_q, tauDst);

    // Path B
    setTau (gB_q.src_q, nodeTaus.at (pathB.src));
    if (!pathB.directHop ()) {
        setTau (gB_q.rL,    nodeTaus.at (pathB.src));
        setTau (gB_q.rR,    nodeTaus.at (pathB.relay));
    }
    setTau (gB_q.pL,    tauPivot);
    setTau (gB_q.pR,    tauPivot);
    setTau (gB_q.dst_q, tauDst);

    // ── Apply channel depolarization ──────────────────────────────────────
    // Path A
    if (pathA.directHop ())
        g_qpe->ApplyErrorModel ({"A_link0a", "A_link0b"}, {gA_q.src_q, gA_q.pL});
    else {
        g_qpe->ApplyErrorModel ({"A_link0a", "A_link0b"}, {gA_q.src_q, gA_q.rL});
        g_qpe->ApplyErrorModel ({"A_link1a", "A_link1b"}, {gA_q.rR,    gA_q.pL});
    }
    g_qpe->ApplyErrorModel ({"A_linkPa", "A_linkPb"}, {gA_q.pR, gA_q.dst_q});

    // Path B
    if (pathB.directHop ())
        g_qpe->ApplyErrorModel ({"B_link0a", "B_link0b"}, {gB_q.src_q, gB_q.pL});
    else {
        g_qpe->ApplyErrorModel ({"B_link0a", "B_link0b"}, {gB_q.src_q, gB_q.rL});
        g_qpe->ApplyErrorModel ({"B_link1a", "B_link1b"}, {gB_q.rR,    gB_q.pL});
    }
    g_qpe->ApplyErrorModel ({"B_linkPa", "B_linkPb"}, {gB_q.pR, gB_q.dst_q});
}

// =============================================================================
// main
// =============================================================================

int
main (int argc, char *argv[])
{
    uint32_t seed    = 42;
    uint32_t numNodes = 20;

    CommandLine cmd;
    cmd.AddValue ("seed",     "Random seed for topology generation", seed);
    cmd.AddValue ("numNodes", "Number of nodes in the random topology", numNodes);
    cmd.Parse (argc, argv);

    // -- Part 1: Generate random topology -----------------------------------
    Ptr<QuantumMismatchTopologyHelper> topoHelper =
        CreateObject<QuantumMismatchTopologyHelper> ();
    topoHelper->SetRandomSeed (seed);
    topoHelper->SetNumNodes (numNodes);
    topoHelper->SetEdgeProbability (0.30);
    topoHelper->Generate ();
    topoHelper->PrintSummary ();

    // -- Part 2: Find a mismatch diamond in the random topology -------------
    PathDesc pathA, pathB;
    bool found = FindMismatchDiamond (*topoHelper, pathA, pathB);
    if (!found)
        {
            // This should not happen when backbone injection is enabled (default).
            std::cerr << "\n[ERROR] Could not find a mismatch diamond.\n"
                      << "        Try a different --seed or larger --numNodes.\n";
            return 1;
        }

    // -- Part 3: Q-CAST routing analysis ------------------------------------
    double et_A = 0.0, et_B = 0.0;
    RunQCastAnalysis (*topoHelper, pathA, pathB, et_A, et_B);

    // -- Part 4: Physical simulation (ExaTN density matrices) ---------------
    SetupPhysicalSim (pathA, pathB, *topoHelper);

    // Schedule: B runs first (short delay), A runs last (long delay, stops sim).
    Simulator::ScheduleNow (&RelayActA);
    Simulator::ScheduleNow (&RelayActB);
    Simulator::Run ();
    Simulator::Destroy ();

    // -- Part 5: Non-monotonicity report ------------------------------------

    std::cout << "\n=== Results ===\n\n"
              << std::fixed << std::setprecision (4)
              << "  Q-CAST routing metric (E_t):\n"
              << "    Path A (trap):    " << et_A << "\n"
              << "    Path B (highway): " << et_B
              << "   -> Q-CAST prefers " << (et_A >= et_B ? "A" : "B") << "\n\n"
              << "  End-to-end fidelity (ExaTN density matrix):\n"
              << "    F(A) = " << gA_fidel
              << "   [trap path,    " << pathA.delayMsRelay
              << " ms delay, tau_pivot=" << topoHelper->GetNodeTaus ().at (pathA.pivot) << "s]\n"
              << "    F(B) = " << gB_fidel
              << "   [highway path, " << pathB.delayMsRelay
              << " ms delay, tau_pivot=" << topoHelper->GetNodeTaus ().at (pathB.pivot) << "s]\n\n";

    if (et_A >= et_B && gA_fidel < gB_fidel)
        std::cout << "  *** NON-MONOTONICITY CONFIRMED ***\n"
                  << "  Q-CAST chose Path A (E_t = " << et_A << ")\n"
                  << "  but density-matrix fidelity: F(A) = " << gA_fidel
                  << " < F(B) = " << gB_fidel << "\n"
                  << "  The E_t metric ignores classical signaling delay\n"
                  << "  and its decoherence impact on tau = "
                  << topoHelper->GetNodeTaus ().at (pathA.pivot) << " s memory nodes.\n";
    else if (et_A >= et_B)
        std::cout << "  Path A has both higher E_t and higher fidelity.\n"
                  << "  No mismatch with these parameters.\n";
    else
        std::cout << "  Path B is preferred by both metrics.\n";

    std::cout << "\n";
    return 0;
}
