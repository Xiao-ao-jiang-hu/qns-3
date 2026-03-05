#ifndef DIJKSTRA_ROUTING_PROTOCOL_H
#define DIJKSTRA_ROUTING_PROTOCOL_H

#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-network-layer.h"

#include <vector>
#include <string>
#include <map>
#include <queue>
#include <limits>

namespace ns3 {

/**
 * \brief Dijkstra-based routing protocol for quantum networks
 *
 * Simple shortest-path routing without recovery paths.
 * Uses link fidelity and success rate as cost metrics.
 * For comparison with Q-CAST (which has recovery paths).
 */
class DijkstraRoutingProtocol : public QuantumRoutingProtocol
{
public:
    static TypeId GetTypeId (void);

    DijkstraRoutingProtocol ();
    ~DijkstraRoutingProtocol () override;

    void DoDispose (void) override;

    void Initialize (void) override;
    void SetNetworkLayer (QuantumNetworkLayer* netLayer) override;
    std::vector<std::string> CalculateRoute (const std::string &src,
                                             const std::string &dst) override;
    void NotifyTopologyChange (void) override;
    std::string RouteToString (const std::vector<std::string> &route) override;

    void UpdateTopology (
        const std::map<std::string, std::map<std::string, LinkMetrics>> &topology) override;
    void AddNeighbor (const std::string &node, const std::string &neighbor,
                      const LinkMetrics &metrics) override;
    void RemoveNeighbor (const std::string &node, const std::string &neighbor) override;
    void UpdateLinkMetrics (const std::string &node, const std::string &neighbor,
                            const LinkMetrics &metrics) override;
    bool HasRoute (const std::string &src, const std::string &dst) override;
    double GetRouteMetric (const std::string &src, const std::string &dst) override;

protected:
    /**
     * \brief Compute Dijkstra shortest path
     * \param src Source node
     * \param dst Destination node
     * \return Path as vector of node names, empty if not found
     */
    std::vector<std::string> RunDijkstra (const std::string &src, const std::string &dst);

    /**
     * \brief Calculate link cost from metrics
     * \param metrics Link metrics
     * \return Cost (lower is better)
     */
    double CalculateLinkCost (const LinkMetrics &metrics) const;

    std::map<std::string, std::map<std::string, LinkMetrics>> m_topology;

    std::map<std::string, std::map<std::string, std::vector<std::string>>> m_routes;
    std::map<std::string, std::map<std::string, double>> m_routeMetrics;

    uint32_t m_routesComputed;
    uint32_t m_routesFailed;
};

} // namespace ns3

#endif /* DIJKSTRA_ROUTING_PROTOCOL_H */