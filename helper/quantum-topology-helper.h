#ifndef QUANTUM_TOPOLOGY_HELPER_H
#define QUANTUM_TOPOLOGY_HELPER_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-delay-model.h"
#include "ns3/node-container.h"
#include "ns3/net-device-container.h"

#include <vector>
#include <string>
#include <map>
#include <random>

namespace ns3 {

/**
 * \brief Helper class to generate random quantum network topologies
 * 
 * This helper creates random network topologies for testing quantum routing protocols.
 * Supports various topology types:
 * - Random geometric graph (nodes placed randomly, edges based on distance)
 * - Erdos-Renyi random graph (fixed probability of edge existence)
 * - Scale-free network (Barabasi-Albert model)
 * - Grid-based with random connections
 */
class QuantumTopologyHelper : public Object
{
public:
    /**
     * \brief Topology types
     */
    enum TopologyType
    {
        RANDOM_GEOMETRIC,    // Nodes in 2D space, edges if within range
        ERDOS_RENYI,         // Random edges with fixed probability
        SCALE_FREE,          // Barabasi-Albert preferential attachment
        GRID_RANDOM          // Grid layout with random extra edges
    };

    static TypeId GetTypeId (void);

    QuantumTopologyHelper ();
    ~QuantumTopologyHelper () override;

    void DoDispose (void) override;

    /**
     * \brief Set the topology type
     * \param type Topology type
     */
    void SetTopologyType (TopologyType type);

    /**
     * \brief Set the random seed
     * \param seed Random seed for reproducibility
     */
    void SetRandomSeed (uint32_t seed);

    /**
     * \brief Set number of nodes
     * \param numNodes Number of nodes in the topology
     */
    void SetNumNodes (uint32_t numNodes);

    /**
     * \brief Set average node degree (for random geometric)
     * \param avgDegree Average number of neighbors per node
     */
    void SetAverageDegree (double avgDegree);

    /**
     * \brief Set edge probability (for Erdos-Renyi)
     * \param probability Probability of edge existence (0.0 - 1.0)
     */
    void SetEdgeProbability (double probability);

    /**
     * \brief Set grid dimensions (for GRID_RANDOM)
     * \param width Grid width
     * \param height Grid height
     */
    void SetGridDimensions (uint32_t width, uint32_t height);

    /**
     * \brief Set link quality parameters
     * \param minFidelity Minimum link fidelity (default: 0.92)
     * \param maxFidelity Maximum link fidelity (default: 0.995)
     * \param minSuccessRate Minimum link success rate (default: 0.88)
     * \param maxSuccessRate Maximum link success rate (default: 0.98)
     */
    void SetLinkQualityRange (double minFidelity, double maxFidelity,
                              double minSuccessRate, double maxSuccessRate);

    /**
     * \brief Set the delay model for the topology
     * \param delayModel The delay model to use
     */
    void SetDelayModel (Ptr<QuantumDelayModel> delayModel);

    /**
     * \brief Get the delay model
     * \return The delay model
     */
    Ptr<QuantumDelayModel> GetDelayModel (void) const;

    /**
     * \brief Initialize delay models for all edges
     * This creates a copy of the delay model for each edge
     */
    void InitializeDelayModels (void);

    /**
     * \brief Get delay model for a specific edge
     * \param node1 Source node
     * \param node2 Destination node
     * \return The delay model for this edge, or nullptr if not found
     */
    Ptr<QuantumDelayModel> GetEdgeDelayModel (const std::string &node1, 
                                               const std::string &node2) const;

    /**
     * \brief Get all edge delay models
     * \return Map from edge to delay model
     */
    std::map<std::pair<std::string, std::string>, Ptr<QuantumDelayModel>> 
    GetAllDelayModels (void) const;

    /**
     * \brief Print delay statistics for all edges
     */
    void PrintDelayStatistics (void) const;

    /**
     * \brief Export delay history for all edges
     * \param prefix Output file prefix (will create files like prefix_edge_0_1.txt)
     */
    void ExportDelayHistories (const std::string &prefix) const;

    /**
     * \brief Generate the topology
     * 
     * Creates nodes, positions them (if applicable), and establishes edges.
     * 
     * \param qphyent Quantum physical entity to add nodes to
     * \return NodeContainer with all created nodes
     */
    NodeContainer GenerateTopology (Ptr<QuantumPhyEntity> qphyent);

    /**
     * \brief Get the generated edges
     * \return Vector of (node1, node2) pairs representing edges
     */
    std::vector<std::pair<std::string, std::string>> GetEdges (void) const;

    /**
     * \brief Get node positions (for geometric topologies)
     * \return Map from node name to (x, y) position
     */
    std::map<std::string, std::pair<double, double>> GetNodePositions (void) const;

    /**
     * \brief Get link properties
     * \return Map from edge to (fidelity, success_rate)
     */
    std::map<std::pair<std::string, std::string>, std::pair<double, double>>
    GetLinkProperties (void) const;

    /**
     * \brief Print topology statistics
     */
    void PrintStatistics (void) const;

    /**
     * \brief Export topology to file (for visualization)
     * \param filename Output filename
     */
    void ExportToFile (const std::string &filename) const;

private:
    /**
     * \brief Generate random geometric graph
     */
    void GenerateRandomGeometric (void);

    /**
     * \brief Generate Erdos-Renyi random graph
     */
    void GenerateErdosRenyi (void);

    /**
     * \brief Generate scale-free network (Barabasi-Albert)
     */
    void GenerateScaleFree (void);

    /**
     * \brief Generate grid with random connections
     */
    void GenerateGridRandom (void);

    /**
     * \brief Calculate distance between two nodes
     */
    double CalculateDistance (const std::string &node1, const std::string &node2) const;

    /**
     * \brief Generate random link properties
     * \return Pair of (fidelity, success_rate)
     */
    std::pair<double, double> GenerateLinkProperties (void);

    // Configuration
    TopologyType m_topologyType;
    uint32_t m_numNodes;
    uint32_t m_randomSeed;
    double m_averageDegree;
    double m_edgeProbability;
    uint32_t m_gridWidth;
    uint32_t m_gridHeight;
    double m_minFidelity;
    double m_maxFidelity;
    double m_minSuccessRate;
    double m_maxSuccessRate;

    // Generated topology
    NodeContainer m_nodes;
    std::vector<std::string> m_nodeNames;
    std::vector<std::pair<std::string, std::string>> m_edges;
    std::map<std::string, std::pair<double, double>> m_nodePositions;
    std::map<std::pair<std::string, std::string>, std::pair<double, double>> m_linkProperties;

    // Random number generation
    std::mt19937 m_rng;
    bool m_generated;
};

} // namespace ns3

#endif /* QUANTUM_TOPOLOGY_HELPER_H */
