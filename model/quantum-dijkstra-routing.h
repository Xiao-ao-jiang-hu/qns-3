#ifndef QUANTUM_DIJKSTRA_ROUTING_H
#define QUANTUM_DIJKSTRA_ROUTING_H

#include "quantum-routing-protocol.h"

#include "ns3/ipv4-address.h"

#include <map>
#include <vector>

namespace ns3
{

class QuantumL3Protocol;

/**
 * \ingroup quantum
 * \brief Quantum Dijkstra Routing Protocol
 *
 * This protocol implements a shortest-path routing based on Dijkstra's algorithm.
 * The "cost" of a link can be configured to represent physical properties like
 * -log(P_success) or distance.
 */
class QuantumDijkstraRouting : public QuantumRoutingProtocol
{
  public:
    static TypeId GetTypeId(void);
    QuantumDijkstraRouting();
    virtual ~QuantumDijkstraRouting();

    // Inherited from QuantumRoutingProtocol
    virtual Ipv4Address RouteOutput(Ipv4Address dest) override;
    virtual Ipv4Address RouteInput(const QuantumHeader& header) override;
    virtual void NotifyEntanglementFailure(Ipv4Address neighbor) override;
    virtual void SetQuantumL3Protocol(Ptr<QuantumL3Protocol> l3) override;

    /**
     * \brief Add or update a link in the global topology map
     * \param u Source node address
     * \param v Destination node address
     * \param cost Link cost
     */
    void AddLink(Ipv4Address u, Ipv4Address v, double cost);

  private:
    /**
     * \brief Run Dijkstra's algorithm to find the next hop towards destination
     * \param dest The destination address
     * \return The next hop address
     */
    Ipv4Address DoDijkstra(Ipv4Address dest);

    Ptr<QuantumL3Protocol> m_l3;

    struct Edge
    {
        Ipv4Address to;
        double cost;
    };

    // Global adjacency list (simulating link-state knowledge)
    std::map<Ipv4Address, std::vector<Edge>> m_topology;
};

} // namespace ns3

#endif /* QUANTUM_DIJKSTRA_ROUTING_H */
