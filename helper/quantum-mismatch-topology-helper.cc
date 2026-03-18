#include "ns3/quantum-mismatch-topology-helper.h"
#include "ns3/log.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumMismatchTopologyHelper");
NS_OBJECT_ENSURE_REGISTERED (QuantumMismatchTopologyHelper);

TypeId
QuantumMismatchTopologyHelper::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QuantumMismatchTopologyHelper")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddConstructor<QuantumMismatchTopologyHelper> ();
  return tid;
}

QuantumMismatchTopologyHelper::QuantumMismatchTopologyHelper ()
{
  NS_LOG_FUNCTION (this);
}

QuantumMismatchTopologyHelper::~QuantumMismatchTopologyHelper ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumMismatchTopologyHelper::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  Object::DoDispose ();
}

bool
QuantumMismatchTopologyHelper::IsPerfectNode (const std::string &name) const
{
  return m_perfectNodes.count (name) > 0;
}

void
QuantumMismatchTopologyHelper::Generate ()
{
  NS_LOG_FUNCTION (this);
  if (m_generated)
    {
      NS_LOG_WARN ("QuantumMismatchTopologyHelper::Generate already called");
      return;
    }

  std::mt19937 rng (m_seed);
  std::uniform_real_distribution<double> uniform01 (0.0, 1.0);
  std::uniform_real_distribution<double> fidDist  (0.88, 0.95);
  std::uniform_real_distribution<double> srDist   (0.88, 0.97);

  // ── Step 1: create node names ──────────────────────────────────────────
  m_nodeNames.clear ();
  for (uint32_t i = 0; i < m_numNodes; ++i)
    m_nodeNames.push_back ("Node" + std::to_string (i));

  // ── Step 2: Erdos-Renyi edge list ──────────────────────────────────────
  std::vector<std::pair<std::string, std::string>> candidateEdges;
  for (uint32_t i = 0; i < m_numNodes; ++i)
    for (uint32_t j = i + 1; j < m_numNodes; ++j)
      if (uniform01 (rng) < m_edgeProb)
        candidateEdges.push_back ({m_nodeNames[i], m_nodeNames[j]});

  // Ensure connectivity: add a random spanning-chain if too sparse
  if (candidateEdges.size () < m_numNodes - 1)
    {
      std::vector<uint32_t> perm (m_numNodes);
      std::iota (perm.begin (), perm.end (), 0);
      std::shuffle (perm.begin (), perm.end (), rng);
      for (uint32_t k = 0; k + 1 < m_numNodes; ++k)
        {
          auto e = std::make_pair (m_nodeNames[perm[k]], m_nodeNames[perm[k + 1]]);
          auto erev = std::make_pair (e.second, e.first);
          bool exists = false;
          for (auto &ex : candidateEdges)
            if (ex == e || ex == erev) { exists = true; break; }
          if (!exists)
            candidateEdges.push_back (e);
        }
    }

  // ── Step 3: assign edge types (trap vs highway) ────────────────────────
  // shuffle so trap links are randomly distributed
  std::shuffle (candidateEdges.begin (), candidateEdges.end (), rng);
  uint32_t numTrap = std::max (1u,
      static_cast<uint32_t> (std::round (m_trapFraction * candidateEdges.size ())));

  m_edges.clear ();
  m_edgeProps.clear ();
  m_perfectNodes.clear ();

  for (size_t idx = 0; idx < candidateEdges.size (); ++idx)
    {
      const auto &e = candidateEdges[idx];
      bool isTrap = (idx < numTrap);

      EdgeProps props;
      props.isTrap = isTrap;
      if (isTrap)
        {
          // Trap link: high delay, near-perfect entanglement fidelity
          props.fidelity    = 0.999;
          props.successRate = 0.95;
          props.delayMs     = m_highLatencyMs;
          // Force both endpoints to be "perfect" nodes
          m_perfectNodes.insert (e.first);
          m_perfectNodes.insert (e.second);
        }
      else
        {
          // Highway link: low delay, ordinary fidelity
          props.fidelity    = fidDist (rng);
          props.successRate = srDist (rng);
          props.delayMs     = m_lowLatencyMs;
        }

      m_edges.push_back (e);
      m_edgeProps[e]             = props;
      m_edgeProps[{e.second, e.first}] = props;  // symmetric
    }

  // ── Step 4: assign node taus ───────────────────────────────────────────
  m_nodeTaus.clear ();
  for (const auto &name : m_nodeNames)
    m_nodeTaus[name] = (m_perfectNodes.count (name) ? m_perfectTau : m_garbageTau);

  // ── Step 5: optionally inject a backbone mismatch diamond ─────────────
  // The backbone guarantees FindMismatchDiamond() always succeeds regardless
  // of random seed.  It adds 6 nodes and 5 directed edges, forming two
  // parallel 3-hop paths that share the last hop (GarbagePivot->Dst):
  //
  //   TrapSrc(perfect) -[trap,sr=0.95,5ms]---> TrapRelay(perfect)
  //                                                    |
  //                     [high-delay highway, sr=0.95, 500ms]
  //                                                    v
  //   HwySrc(garbage) -[hwy,sr=0.90,5ms]---> HwyRelay(garbage)
  //                                                    |
  //                     [hwy, sr=0.90, 5ms]            |
  //                                                    v
  //                                         GarbagePivot(garbage) -[hwy,sr=0.90,5ms]-> Dst(garbage)
  //
  //  Path A: TrapSrc->TrapRelay->GarbagePivot->Dst
  //    E_t = 0.95 * 0.95 * 0.90 * S(3)  (Q-CAST prefers this)
  //    Classical delay at GarbagePivot = 500 ms  => heavy decoherence (tau=1s)
  //
  //  Path B: HwySrc->HwyRelay->GarbagePivot->Dst
  //    E_t = 0.90 * 0.90 * 0.90 * S(3)  (Q-CAST rejects this)
  //    Classical delay at GarbagePivot =   5 ms  => small decoherence
  //
  //  NON-MONOTONICITY: E_t(A) > E_t(B) but F(A) < F(B).

  m_backbone = BackboneNodes{};

  if (m_injectBackbone)
    {
      const std::string pfx       = "_Bone";
      const std::string trapSrc   = pfx + "TrapSrc";
      const std::string trapRelay = pfx + "TrapRelay";
      const std::string gPivot    = pfx + "GarbagePivot";
      const std::string hwySrc    = pfx + "HwySrc";
      const std::string hwyRelay  = pfx + "HwyRelay";
      const std::string boneDst   = pfx + "Dst";

      // Register nodes
      for (const std::string &n : {trapSrc, trapRelay, gPivot, hwySrc, hwyRelay, boneDst})
          m_nodeNames.push_back (n);

      // Assign taus: TrapSrc and TrapRelay are perfect; rest are garbage
      m_perfectNodes.insert (trapSrc);
      m_perfectNodes.insert (trapRelay);
      m_nodeTaus[trapSrc]   = m_perfectTau;
      m_nodeTaus[trapRelay] = m_perfectTau;
      m_nodeTaus[gPivot]    = m_garbageTau;
      m_nodeTaus[hwySrc]    = m_garbageTau;
      m_nodeTaus[hwyRelay]  = m_garbageTau;
      m_nodeTaus[boneDst]   = m_garbageTau;

      // Helper to add undirected edge
      auto addEdge = [&] (const std::string &u, const std::string &v, EdgeProps props) {
          m_edges.push_back ({u, v});
          m_edgeProps[{u, v}] = props;
          m_edgeProps[{v, u}] = props;
      };

      // TrapSrc -> TrapRelay: trap link (sr=0.95, delay=5ms for quantum channel)
      EdgeProps trapLink;
      trapLink.isTrap      = true;
      trapLink.fidelity    = 0.999;
      trapLink.successRate = 0.95;
      trapLink.delayMs     = m_lowLatencyMs;  // quantum channel delay (fast)

      // TrapRelay -> GarbagePivot: high-delay highway link
      // isTrap=false keeps GarbagePivot as garbage; delayMs=500ms causes decoherence.
      EdgeProps highDelayHwyLink;
      highDelayHwyLink.isTrap      = false;
      highDelayHwyLink.fidelity    = 0.95;
      highDelayHwyLink.successRate = 0.95;
      highDelayHwyLink.delayMs     = m_highLatencyMs;  // 500 ms classical signaling!

      // HwySrc -> HwyRelay and HwyRelay -> GarbagePivot: regular highway links
      EdgeProps hwLink;
      hwLink.isTrap      = false;
      hwLink.fidelity    = 0.92;
      hwLink.successRate = 0.90;
      hwLink.delayMs     = m_lowLatencyMs;

      // GarbagePivot -> Dst: regular highway link (shared hop)
      EdgeProps pivotDstLink;
      pivotDstLink.isTrap      = false;
      pivotDstLink.fidelity    = 0.92;
      pivotDstLink.successRate = 0.90;
      pivotDstLink.delayMs     = m_lowLatencyMs;

      addEdge (trapSrc,   trapRelay,  trapLink);
      addEdge (trapRelay, gPivot,     highDelayHwyLink);
      addEdge (hwySrc,    hwyRelay,   hwLink);
      addEdge (hwyRelay,  gPivot,     hwLink);
      addEdge (gPivot,    boneDst,    pivotDstLink);

      m_backbone.trapSrc       = trapSrc;
      m_backbone.trapRelay     = trapRelay;
      m_backbone.garbagePivot  = gPivot;
      m_backbone.hwySrc        = hwySrc;
      m_backbone.hwyRelay      = hwyRelay;
      m_backbone.backboneDst   = boneDst;

      NS_LOG_INFO ("Backbone diamond injected: "
                   << trapSrc << "-[trap]-" << trapRelay
                   << "-[500ms]->" << gPivot << "<-[5ms]-" << hwyRelay
                   << "<-" << hwySrc << " ; Dst=" << boneDst);
    }

  m_generated = true;

  NS_LOG_INFO ("MismatchTopology generated: "
               << m_numNodes << " nodes, "
               << m_edges.size () << " edges, "
               << m_perfectNodes.size () << " perfect nodes, "
               << numTrap << " trap links");
}

void
QuantumMismatchTopologyHelper::PrintSummary () const
{
  if (!m_generated)
    {
      std::cout << "[MismatchTopology] Not yet generated.\n";
      return;
    }

  uint32_t numTrap = 0, numHighway = 0;
  uint32_t numPerfect = static_cast<uint32_t> (m_perfectNodes.size ());
  for (const auto &kv : m_edgeProps)
    {
      // only count one direction
      if (kv.first.first < kv.first.second)
        kv.second.isTrap ? ++numTrap : ++numHighway;
    }

  std::cout << "\n[MismatchTopology Summary]\n"
            << "  Nodes       : " << m_numNodes
            << "  (random: " << (m_numNodes - (m_injectBackbone ? 6 : 0))
            << ", backbone: " << (m_injectBackbone ? 6 : 0) << ")\n"
            << "  Perfect nodes: " << numPerfect
            << ", Garbage nodes: " << (static_cast<uint32_t>(m_nodeNames.size()) - numPerfect) << "\n"
            << "  Edges       : " << (numTrap + numHighway)
            << "  (trap: " << numTrap
            << ", highway: " << numHighway << ")\n"
            << "  Trap delay  : " << m_highLatencyMs << " ms"
            << "   Perfect tau: " << m_perfectTau    << " s\n"
            << "  Hwy  delay  : " << m_lowLatencyMs  << " ms"
            << "   Garbage tau: " << m_garbageTau    << " s\n";
  if (m_injectBackbone && !m_backbone.garbagePivot.empty ())
    std::cout << "  Backbone    : injected ("
              << m_backbone.trapSrc << "-[trap]->" << m_backbone.trapRelay
              << "-[500ms]->" << m_backbone.garbagePivot
              << "<-[5ms]-" << m_backbone.hwyRelay
              << "<-" << m_backbone.hwySrc << ")\n";
}

} // namespace ns3
