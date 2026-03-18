#ifndef QUANTUM_MISMATCH_TOPOLOGY_HELPER_H
#define QUANTUM_MISMATCH_TOPOLOGY_HELPER_H

#include "ns3/object.h"
#include "ns3/nstime.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <random>
#include <utility>

namespace ns3 {

/**
 * \brief Helper to generate "Mismatch" topologies demonstrating routing non-monotonicity.
 *
 * Bimodal parameter strategy (from theoretical analysis):
 *
 *  Links:
 *    - 10% "Trap"    links: high latency (500 ms), near-perfect fidelity (0.999),
 *                          success-rate 0.95   -- constitute path 'a'
 *    - 90% "Highway" links: low latency  (5 ms),  normal  fidelity U[0.88,0.95],
 *                          success-rate U[0.88,0.97] -- constitute paths 'b' and 'c'
 *
 *  Nodes (with required correlation):
 *    - "Perfect" (20%): tau = 10 000 s  -- must be endpoints of trap links
 *    - "Garbage" (80%): tau =      1 s  -- everything else
 *
 *  Correlation rule: if an edge is a "trap" link, BOTH endpoints are forced to be
 *  "perfect" nodes. This ensures path 'a' keeps high local fidelity despite high delay.
 *
 * Fidelity non-monotonicity:
 *   F(a) > F(b) locally, but F(a ⊕ c) ≪ F(b ⊕ c) because path 'a' accumulated
 *   huge latency which cripples the sensitive "garbage" nodes along path 'c'.
 */
class QuantumMismatchTopologyHelper : public Object
{
public:
  /// Per-edge properties
  struct EdgeProps
  {
    double fidelity;
    double successRate;
    double delayMs;   ///< Classical signaling delay in milliseconds
    bool   isTrap;    ///< True if this is a "trap" (high-latency) link
  };

  static TypeId GetTypeId (void);

  QuantumMismatchTopologyHelper ();
  ~QuantumMismatchTopologyHelper () override;

  void DoDispose () override;

  // ---------- Configuration setters ----------

  void SetNumNodes             (uint32_t n)          { m_numNodes = n; }
  void SetEdgeProbability      (double p)            { m_edgeProb = p; }
  void SetTrapFraction         (double f)            { m_trapFraction = f; }
  void SetRandomSeed           (uint32_t s)          { m_seed = s; }

  /// Latency for trap links (default: 500 ms)
  void SetHighLatencyMs        (double ms)           { m_highLatencyMs = ms; }
  /// Latency for highway links (default: 5 ms)
  void SetLowLatencyMs         (double ms)           { m_lowLatencyMs = ms; }
  /// Tau for perfect nodes (default: 10 000 s)
  void SetPerfectTau           (double tau)          { m_perfectTau = tau; }
  /// Tau for garbage nodes (default: 1 s)
  void SetGarbageTau           (double tau)          { m_garbageTau = tau; }

  /// If true (default), inject a guaranteed mismatch backbone into the
  /// topology so that FindMismatchDiamond() always succeeds regardless
  /// of random seed.  The backbone adds 5 nodes and 4 edges.
  void SetInjectBackbone       (bool b)              { m_injectBackbone = b; }

  // ---------- Main generation ----------

  /**
   * \brief Generate the mismatch topology.
   * Must be called before any Get* methods.
   */
  void Generate ();

  bool IsGenerated () const { return m_generated; }

  // ---------- Accessors ----------

  const std::vector<std::string>&
  GetNodeNames () const { return m_nodeNames; }

  const std::map<std::string, double>&
  GetNodeTaus () const { return m_nodeTaus; }

  /** Returns true if node is a "perfect" node (high tau). */
  bool IsPerfectNode (const std::string &name) const;

  /**
   * \brief Names of the injected backbone nodes, if backbone injection is
   * enabled (default: enabled).  Returns empty strings if not injected.
   *
   * Layout (6 nodes, 2 parallel 3-hop paths sharing GarbagePivot -> Dst):
   *
   *   TrapSrc -[trap,sr=0.95,5ms]-> TrapRelay -[high-delay,sr=0.95,500ms]-> GarbagePivot -[0.90,5ms]-> Dst
   *   HwySrc  -[hwy, sr=0.90,5ms]-> HwyRelay  -[hwy,       sr=0.90, 5ms]-> GarbagePivot
   *
   * E_t(A) = 0.95^2 * 0.90 * S(3) > E_t(B) = 0.90^3 * S(3)  => Q-CAST picks A
   * But 500 ms delay on GarbagePivot (tau=1 s) => F(A) < F(B)  => NON-MONOTONICITY
   */
  struct BackboneNodes
  {
    std::string trapSrc;       ///< perfect, start of trap link
    std::string trapRelay;     ///< perfect, end of trap / start of high-delay hwy
    std::string garbagePivot;  ///< garbage, pivot node for both paths
    std::string hwySrc;        ///< garbage, source of the highway-only path
    std::string hwyRelay;      ///< garbage, relay of the highway path
    std::string backboneDst;   ///< garbage, destination shared by both paths
  };
  const BackboneNodes &GetBackboneNodes () const { return m_backbone; }

  const std::vector<std::pair<std::string, std::string>>&
  GetEdges () const { return m_edges; }

  const std::map<std::pair<std::string, std::string>, EdgeProps>&
  GetEdgeProps () const { return m_edgeProps; }

  /// Print summary to stdout
  void PrintSummary () const;

private:
  // ---------- Parameters ----------
  uint32_t m_numNodes      {20};
  double   m_edgeProb      {0.25};
  double   m_trapFraction  {0.10};  ///< Fraction of edges to mark as "trap"
  uint32_t m_seed          {42};
  double   m_highLatencyMs {500.0};
  double   m_lowLatencyMs  {5.0};
  double   m_perfectTau    {10000.0};
  double   m_garbageTau    {1.0};

  // ---------- Generated data ----------
  bool m_generated      {false};
  bool m_injectBackbone {true};
  std::vector<std::string>                                       m_nodeNames;
  std::map<std::string, double>                                  m_nodeTaus;
  std::set<std::string>                                          m_perfectNodes;
  std::vector<std::pair<std::string, std::string>>               m_edges;
  std::map<std::pair<std::string, std::string>, EdgeProps>       m_edgeProps;
  BackboneNodes                                                  m_backbone;
};

} // namespace ns3

#endif /* QUANTUM_MISMATCH_TOPOLOGY_HELPER_H */
