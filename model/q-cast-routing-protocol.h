#ifndef Q_CAST_ROUTING_PROTOCOL_H
#define Q_CAST_ROUTING_PROTOCOL_H

#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-channel.h"

#include <vector>
#include <string>
#include <map>
#include <set>
#include <queue>
#include <memory>

namespace ns3 {

class QuantumNetworkLayer;

/**
 * \brief Label structure for Extended Dijkstra Algorithm (EDA)
 * 
 * Each label represents a path from source to current node with:
 * - Expected throughput (E_t)
 * - Path width (bottleneck capacity)
 * - Hop count
 * - Path sequence
 */
struct QCastLabel
{
    std::string node;                          // Current node
    double expectedThroughput;                 // E_t value
    double pathWidth;                          // Minimum channel width along path
    uint32_t hopCount;                         // Number of hops
    std::vector<std::string> path;             // Node sequence
    std::vector<Ptr<QuantumChannel>> channels; // Channel sequence

    QCastLabel ()
        : expectedThroughput (1.0),
          pathWidth (std::numeric_limits<double>::max ()),
          hopCount (0)
    {
    }

    // For priority queue (max heap based on E_t)
    bool operator< (const QCastLabel &other) const
    {
        return expectedThroughput < other.expectedThroughput;
    }
};

/**
 * \brief Path information with recovery paths
 */
struct QCastPath
{
    uint32_t requestId{0};                             // ID of the originating request
    std::vector<std::string> primaryPath;              // Main path nodes
    std::vector<Ptr<QuantumChannel>> primaryChannels;  // Main path channels
    double primaryEt;                                  // Expected throughput
    
    // Recovery paths: map from (start_idx, end_idx) to recovery path
    // Node indices: ring (i,j) covers primary links i..(j-1)
    std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>> recoveryPaths;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<Ptr<QuantumChannel>>> recoveryChannels;
    
    // Per-hop link success probabilities
    std::vector<double> linkProbabilities;
};

/**
 * \brief XOR-based recovery ring information
 */
struct QCastRecoveryRing
{
    uint32_t pathId;                           // Associated path ID
    uint32_t startIdx;                         // Start index on primary path
    uint32_t endIdx;                           // End index on primary path
    std::vector<std::string> recoveryPath;     // Recovery path nodes
    std::vector<Ptr<QuantumChannel>> recoveryChannels; // Recovery path channels
    bool isActive;                             // Whether this ring is selected for recovery
};

/**
 * \brief Request information for Q-CAST
 */
struct QCastRequest
{
    std::string srcNode;
    std::string dstNode;
    double minFidelity;
    uint32_t requestId;
};

/**
 * \brief Q-CAST (Contention-Free pAth Selection at runTime) Routing Protocol
 * 
 * Q-CAST is a quantum routing protocol that:
 * 1. Uses Greedy Extended Dijkstra Algorithm (G-EDA) for primary path selection
 * 2. Discovers recovery paths for local recovery
 * 3. Employs XOR-based recovery strategy
 * 4. Uses logarithmic-time swapping scheduling
 * 
 * Reference: Q-CAST: Quantum Routing Protocol (based on quantum network routing research)
 */
class QCastRoutingProtocol : public QuantumRoutingProtocol
{
public:
    static TypeId GetTypeId (void);

    QCastRoutingProtocol ();
    ~QCastRoutingProtocol () override;

    void DoDispose (void) override;

    // Implementation of QuantumRoutingProtocol interface
    void Initialize (void) override;
    void SetNetworkLayer (QuantumNetworkLayer* netLayer) override;
    std::vector<std::string> CalculateRoute (const std::string &src,
                                             const std::string &dst) override;
    void NotifyTopologyChange (void) override;
    std::string RouteToString (const std::vector<std::string> &route) override;

    // Implementations of pure virtual methods from QuantumRoutingProtocol
    void UpdateTopology (
        const std::map<std::string, std::map<std::string, LinkMetrics>> &topology) override;
    void AddNeighbor (const std::string &node, const std::string &neighbor,
                      const LinkMetrics &metrics) override;
    void RemoveNeighbor (const std::string &node, const std::string &neighbor) override;
    void UpdateLinkMetrics (const std::string &node, const std::string &neighbor,
                            const LinkMetrics &metrics) override;
    bool HasRoute (const std::string &src, const std::string &dst) override;
    double GetRouteMetric (const std::string &src, const std::string &dst) override;

    /**
     * \brief Calculate routes for multiple requests using G-EDA
     * 
     * This is the main Q-CAST algorithm that handles concurrent requests
     * and ensures contention-free path selection.
     * 
     * \param requests Vector of Q-CAST requests
     * \return Map from request ID to QCastPath (primary + recovery paths)
     */
    std::map<uint32_t, QCastPath> CalculateRoutesGEDA (
        const std::vector<QCastRequest> &requests);

    /**
     * \brief Set maximum hop count for EDA
     * \param maxHops Maximum allowed hops
     */
    void SetMaxHops (uint32_t maxHops);

    /**
     * \brief Set k-hop neighborhood for local recovery
     * \param kHop Number of hops for local information
     */
     void SetKHop (uint32_t kHop);

    /**
     * \brief Set alpha parameter for swap scheduling success probability
     * \param alpha Swap failure rate constant
     */
    void SetAlpha (double alpha);

    /**
     * \brief Set node capacity (number of concurrent paths)
     * \param capacity Node capacity
     */
    void SetNodeCapacity (uint32_t capacity);

    /**
     * \brief Get the Q-CAST path information for a path ID
     * \param pathId Path identifier
     * \return QCastPath structure
     */
    QCastPath GetQCastPath (uint32_t pathId) const;

    /**
     * \brief Execute XOR-based recovery for a path
     * 
     * Based on local link status (k-hop), determine which recovery rings to use.
     * 
     * \param pathId Path identifier
     * \param failedLinks Set of failed link indices on primary path
     * \return Vector of recovery rings to activate
     */
    std::vector<QCastRecoveryRing> ExecuteXORRecovery (
        uint32_t pathId, const std::set<uint32_t> &failedLinks);

    /**
     * \brief Generate logarithmic-time swap schedule
     * 
     * Creates a schedule for entanglement swapping with O(log h) rounds.
     * 
     * \param pathNodes Vector of nodes in the path
     * \return Vector of (round, node_index) pairs indicating when each node swaps
     */
    std::vector<std::pair<uint32_t, uint32_t>> GenerateSwapSchedule (
        const std::vector<std::string> &pathNodes);

protected:
    /**
     * \brief Extended Dijkstra Algorithm (EDA)
     * 
     * Finds the E_t-optimal path from src to dst in the residual graph.
     * Uses label setting with Pareto dominance checks.
     * 
     * \param src Source node
     * \param dst Destination node
     * \param availableNodes Set of nodes with available capacity
     * \param availableEdges Set of available edges (channel identifiers)
     * \return Best QCastLabel or null if no path found
     */
    std::unique_ptr<QCastLabel> ExtendedDijkstra (
        const std::string &src, const std::string &dst,
        const std::set<std::string> &availableNodes,
        const std::set<std::pair<std::string, std::string>> &availableEdges);

    /**
     * \brief Check if label1 dominates label2 (Pareto dominance)
     * 
     * Label1 dominates Label2 if:
     * - E_t1 >= E_t2 AND
     * - Width1 >= Width2 AND
     * - Hops1 <= Hops2
     * 
     * \param label1 First label
     * \param label2 Second label
     * \return True if label1 dominates label2
     */
    bool Dominates (const QCastLabel &label1, const QCastLabel &label2) const;

    /**
     * \brief Calculate expected throughput for a path
     * 
     * E_t(P) = (prod of link success probs) * S(hop_count)
     * where S(h) is swap scheduling success probability
     * 
     * \param label Current path label
     * \param nextHopProb Success probability of next hop
     * \return Updated E_t
     */
    double CalculateExpectedThroughput (const QCastLabel &label,
                                        double nextHopProb) const;

    /**
     * \brief Calculate swap scheduling success probability S(h)
     * 
     * S(h) ≈ exp(-α * log2(h)) for h > 1
     * S(1) = 1.0
     * 
     * \param hopCount Number of hops
     * \return Success probability
     */
    double CalculateSwapSuccessProb (uint32_t hopCount) const;

    /**
     * \brief Discover recovery paths for a primary path
     * 
     * Finds recovery paths connecting nodes within k hops on the primary path.
     * 
     * \param primaryPath The primary path
     * \param availableNodes Nodes with remaining capacity
     * \param availableEdges Edges with remaining capacity
     * \return Map from (start_idx, end_idx) to recovery path
     */
    std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>>
    DiscoverRecoveryPaths (const std::vector<std::string> &primaryPath,
                          const std::set<std::string> &availableNodes,
                          const std::set<std::pair<std::string, std::string>> &availableEdges);

    /**
     * \brief Find a recovery path between two nodes using BFS
     * 
     * \param src Source node
     * \param dst Destination node
     * \param availableNodes Available nodes
     * \param availableEdges Available edges
     * \param maxLength Maximum path length allowed
     * \return Recovery path or empty if not found
     */
    std::vector<std::string> FindRecoveryPathBFS (
        const std::string &src, const std::string &dst,
        const std::set<std::string> &availableNodes,
        const std::set<std::pair<std::string, std::string>> &availableEdges,
        uint32_t maxLength);

    /**
     * \brief Check if edge is available
     * \param u Source node
     * \param v Destination node
     * \param availableEdges Set of available edges
     * \return True if edge is available
     */
    bool IsEdgeAvailable (const std::string &u, const std::string &v,
                          const std::set<std::pair<std::string, std::string>> &availableEdges) const;

    /**
     * \brief Get link success probability
     * \param u Source node
     * \param v Destination node
     * \return Success probability (0.0 if no link)
     */
    double GetLinkProbability (const std::string &u, const std::string &v) const;

    /**
     * \brief Update residual resources after selecting a path
     * 
     * \param path Selected path
     * \param availableNodes Updated node availability
     * \param availableEdges Updated edge availability
     */
    void UpdateResidualResources (
        const QCastPath &path, std::set<std::string> &availableNodes,
        std::set<std::pair<std::string, std::string>> &availableEdges);

    // Configuration parameters
    uint32_t m_maxHops;            // Maximum allowed hops
    uint32_t m_kHop;               // k-hop neighborhood for recovery
    double m_alpha;                // Swap failure rate constant
    uint32_t m_nodeCapacity;       // Per-node capacity

    // State
    std::map<uint32_t, QCastPath> m_qcastPaths; // Path ID to QCastPath
    std::map<std::string, std::map<std::string, double>> m_linkProbabilities; // Cached link probs
    uint32_t m_nextPathId;
    bool m_initialized;

    // Statistics
    uint32_t m_pathsComputed;
    uint32_t m_recoveryPathsFound;
    Time m_totalComputeTime;
};

} // namespace ns3

#endif /* Q_CAST_ROUTING_PROTOCOL_H */
