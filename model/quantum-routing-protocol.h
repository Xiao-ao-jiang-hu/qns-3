#ifndef QUANTUM_ROUTING_PROTOCOL_H
#define QUANTUM_ROUTING_PROTOCOL_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/quantum-fidelity-model.h"

#include <vector>
#include <string>
#include <map>

namespace ns3 {

class QuantumNetworkLayer;
class QuantumRoutingMetric;

/**
 * \brief Link-attribute carrier shared by routing and link-layer modeling.
 *
 * This struct intentionally stores the raw per-link parameters that a routing
 * metric model may consume, rather than a single precomputed cost. Concrete
 * QuantumRoutingMetric implementations derive their own surrogate score from
 * these attributes.
 */
struct LinkMetrics
{
    double fidelity;        // Link fidelity (0.0 to 1.0)
    double initialFidelity; // Initial link EPR fidelity used by the physical model
    BellPairNoiseFamily noiseFamily; // Initial Bell-pair state family used by routing and link layer
    double successRate;     // Link success probability (0.0 to 1.0)
    double latency;         // Link latency in ms
    double quantumSetupTimeMs;    // Quantum entanglement setup / heralding time in ms
    double classicalControlDelayMs; // Classical control latency in ms
    bool isAvailable;       // Whether link is currently available

    LinkMetrics ()
        : fidelity (0.0),
          initialFidelity (0.0),
          noiseFamily (BellPairNoiseFamily::WERNER),
          successRate (0.0),
          latency (0.0),
          quantumSetupTimeMs (0.0),
          classicalControlDelayMs (0.0),
          isAvailable (false)
    {
    }
};

/**
 * \brief Routing protocol interface for quantum networks
 * 
 * This abstract class defines the interface that all quantum routing protocols must implement.
 * The routing protocol is responsible for computing routes based on the network topology
 * and link metrics (fidelity, success rate, etc.).
 * 
 * The network layer (QuantumNetworkLayer) delegates routing decisions to this interface,
 * allowing different routing algorithms to be used interchangeably.
 * 
 * Implementations of this interface include:
 * - DijkstraQuantumRouting: Shortest path based on link metrics
 * - Future: Link-state routing, distance-vector routing, etc.
 */
class QuantumRoutingProtocol : public Object
{
public:
    static TypeId GetTypeId ();

    QuantumRoutingProtocol ();
    ~QuantumRoutingProtocol () override;

    /**
     * \brief Initialize the routing protocol
     */
    virtual void Initialize (void);

    /**
     * \brief Set the network layer this protocol is associated with
     * \param netLayer Pointer to the network layer
     */
    virtual void SetNetworkLayer (QuantumNetworkLayer* netLayer);

    /**
     * \brief Notify the protocol of a topology change
     */
    virtual void NotifyTopologyChange (void);

    /**
     * \brief Convert a route to a human-readable string
     * \param route Vector of node names
     * \return String representation
     */
    virtual std::string RouteToString (const std::vector<std::string> &route);

    /**
     * \brief Compute a route from source to destination
     * 
     * This is the main routing function. Given a source and destination node,
     * the protocol must compute a valid path through the network.
     * 
     * \param src Source node name
     * \param dst Destination node name
     * \return Vector of node names representing the path from src to dst,
     *         including both src and dst. Empty vector if no route found.
     */
    virtual std::vector<std::string> CalculateRoute (
        const std::string &src,
        const std::string &dst
    ) = 0;

    /**
     * \brief Update the network topology
     * 
     * Called when the network topology changes (new nodes, new links,
     * link failures, etc.). The routing protocol should update its
     * internal routing tables accordingly.
     * 
     * \param topology Map from node name to its neighbors with link metrics
     */
    virtual void UpdateTopology (
        const std::map<std::string, std::map<std::string, LinkMetrics>> &topology
    ) = 0;

    /**
     * \brief Add a direct neighbor
     * 
     * Called when a new neighbor is discovered or configured.
     * 
     * \param node Node name
     * \param neighbor Neighbor node name
     * \param metrics Link metrics
     */
    virtual void AddNeighbor (
        const std::string &node,
        const std::string &neighbor,
        const LinkMetrics &metrics
    ) = 0;

    /**
     * \brief Remove a direct neighbor
     * 
     * Called when a neighbor becomes unreachable or is removed.
     * 
     * \param node Node name
     * \param neighbor Neighbor node name to remove
     */
    virtual void RemoveNeighbor (
        const std::string &node,
        const std::string &neighbor
    ) = 0;

    /**
     * \brief Update link metrics for a direct neighbor
     * 
     * Called when link quality changes (e.g., fidelity degradation).
     * 
     * \param node Node name
     * \param neighbor Neighbor node name
     * \param metrics Updated link metrics
     */
    virtual void UpdateLinkMetrics (
        const std::string &node,
        const std::string &neighbor,
        const LinkMetrics &metrics
    ) = 0;

    /**
     * \brief Check if a route exists between two nodes
     * 
     * \param src Source node name
     * \param dst Destination node name
     * \return True if a valid route exists
     */
    virtual bool HasRoute (
        const std::string &src,
        const std::string &dst
    ) = 0;

    /**
     * \brief Get the routing metric value for a computed route
     * 
     * Returns the computed metric value (e.g., total path cost) for the
     * last route calculation between src and dst.
     * 
     * \param src Source node name
     * \param dst Destination node name
     * \return Metric value, or infinity if no route exists
     */
    virtual double GetRouteMetric (
        const std::string &src,
        const std::string &dst
    ) = 0;

    virtual void SetMetricModel (Ptr<QuantumRoutingMetric> metricModel);

    virtual Ptr<QuantumRoutingMetric> GetMetricModel () const;

    /**
     * \brief Set the local node name
     * 
     * The routing protocol needs to know which node it is running on
     * to compute relative routes.
     * 
     * \param nodeName Name of the local node
     */
    void SetLocalNode (const std::string &nodeName);

    /**
     * \brief Get the local node name
     * \return Name of the local node
     */
    std::string GetLocalNode () const;

protected:
    std::string m_localNode;           // Name of the node this protocol serves
    QuantumNetworkLayer* m_networkLayer; // Associated network layer (not owned)
    Ptr<QuantumRoutingMetric> m_metricModel;
};

} // namespace ns3

#endif /* QUANTUM_ROUTING_PROTOCOL_H */
