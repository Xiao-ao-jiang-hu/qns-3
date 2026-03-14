#include "quantum-dijkstra-routing.h"

#include "quantum-l3-protocol.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/log.h"
#include "ns3/node.h"

#include <queue>
#include <set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("QuantumDijkstraRouting");
NS_OBJECT_ENSURE_REGISTERED(QuantumDijkstraRouting);

TypeId
QuantumDijkstraRouting::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::QuantumDijkstraRouting")
                            .SetParent<QuantumRoutingProtocol>()
                            .SetGroupName("Quantum")
                            .AddConstructor<QuantumDijkstraRouting>();
    return tid;
}

QuantumDijkstraRouting::QuantumDijkstraRouting()
{
}

QuantumDijkstraRouting::~QuantumDijkstraRouting()
{
}

void
QuantumDijkstraRouting::SetQuantumL3Protocol(Ptr<QuantumL3Protocol> l3)
{
    m_l3 = l3;
}

void
QuantumDijkstraRouting::AddLink(Ipv4Address u, Ipv4Address v, double cost)
{
    m_topology[u].push_back({v, cost});
    m_topology[v].push_back({u, cost}); // Assuming undirected links for quantum entanglement
}

Ipv4Address
QuantumDijkstraRouting::RouteOutput(Ipv4Address dest)
{
    return DoDijkstra(dest);
}

Ipv4Address
QuantumDijkstraRouting::RouteInput(const QuantumHeader& header)
{
    return DoDijkstra(header.GetDestination());
}

void
QuantumDijkstraRouting::NotifyEntanglementFailure(Ipv4Address neighbor)
{
    NS_LOG_WARN("Entanglement failure notified for neighbor " << neighbor);
    // In a real implementation, this would trigger a topology update or cost increase
}

Ipv4Address
QuantumDijkstraRouting::DoDijkstra(Ipv4Address dest)
{
    Ipv4Address source =
        m_l3->GetObject<Node>()->GetObject<Ipv4L3Protocol>()->GetAddress(1, 0).GetLocal();

    if (source == dest)
    {
        return source;
    }

    std::map<Ipv4Address, double> dist;
    std::map<Ipv4Address, Ipv4Address> parent;

    // Priority queue: <distance, address>
    std::priority_queue<std::pair<double, Ipv4Address>,
                        std::vector<std::pair<double, Ipv4Address>>,
                        std::greater<std::pair<double, Ipv4Address>>>
        pq;

    pq.push({0.0, source});
    dist[source] = 0.0;

    while (!pq.empty())
    {
        double d = pq.top().first;
        Ipv4Address u = pq.top().second;
        pq.pop();

        if (d > dist[u] && dist.count(u))
            continue;
        if (u == dest)
            break;

        for (const auto& edge : m_topology[u])
        {
            double newDist = d + edge.cost;
            if (!dist.count(edge.to) || newDist < dist[edge.to])
            {
                dist[edge.to] = newDist;
                parent[edge.to] = u;
                pq.push({newDist, edge.to});
            }
        }
    }

    if (!parent.count(dest))
    {
        NS_LOG_ERROR("No path found to " << dest);
        return Ipv4Address::GetAny();
    }

    // Backtrack to find the first hop from source
    Ipv4Address curr = dest;
    while (parent[curr] != source)
    {
        curr = parent[curr];
    }

    return curr;
}

} // namespace ns3
