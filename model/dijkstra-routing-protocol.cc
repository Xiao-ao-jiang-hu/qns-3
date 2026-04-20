#include "ns3/dijkstra-routing-protocol.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DijkstraRoutingProtocol");

NS_OBJECT_ENSURE_REGISTERED (DijkstraRoutingProtocol);

TypeId
DijkstraRoutingProtocol::GetTypeId (void)
{
    static TypeId tid =
        TypeId ("ns3::DijkstraRoutingProtocol")
            .SetParent<QuantumRoutingProtocol> ()
            .SetGroupName ("Quantum")
            .AddConstructor<DijkstraRoutingProtocol> ();
    return tid;
}

DijkstraRoutingProtocol::DijkstraRoutingProtocol ()
    : m_routesComputed (0),
      m_routesFailed (0)
{
    NS_LOG_FUNCTION (this);
}

DijkstraRoutingProtocol::~DijkstraRoutingProtocol ()
{
    NS_LOG_FUNCTION (this);
}

void
DijkstraRoutingProtocol::DoDispose (void)
{
    NS_LOG_FUNCTION (this);
    m_topology.clear ();
    m_routes.clear ();
    m_routeMetrics.clear ();
    QuantumRoutingProtocol::DoDispose ();
}

void
DijkstraRoutingProtocol::Initialize (void)
{
    NS_LOG_FUNCTION (this);
}

void
DijkstraRoutingProtocol::SetNetworkLayer (QuantumNetworkLayer* netLayer)
{
    NS_LOG_FUNCTION (this << netLayer);
    m_networkLayer = netLayer;
}

std::vector<std::string>
DijkstraRoutingProtocol::CalculateRoute (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (src == dst)
    {
        return {src};
    }

    // Try cached route first
    if (m_routes.count (src) && m_routes[src].count (dst))
    {
        NS_LOG_DEBUG ("Using cached route for " << src << " -> " << dst);
        return m_routes[src][dst];
    }

    // Run Dijkstra algorithm
    std::vector<std::string> route = RunDijkstra (src, dst);

    if (route.empty ())
    {
        m_routesFailed++;
        NS_LOG_WARN ("No route found from " << src << " to " << dst);
    }
    else
    {
        m_routesComputed++;
        m_routes[src][dst] = route;
    }

    return route;
}

void
DijkstraRoutingProtocol::NotifyTopologyChange (void)
{
    NS_LOG_FUNCTION (this);
    // Clear all cached routes
    m_routes.clear ();
    m_routeMetrics.clear ();
    NS_LOG_INFO ("Topology changed, clearing all cached routes");
}

std::string
DijkstraRoutingProtocol::RouteToString (const std::vector<std::string> &route)
{
    NS_LOG_FUNCTION (this);
    std::string s;
    for (size_t i = 0; i < route.size (); ++i)
    {
        if (i > 0)
            s += " -> ";
        s += route[i];
    }
    return s;
}

void
DijkstraRoutingProtocol::UpdateTopology (
    const std::map<std::string, std::map<std::string, LinkMetrics>> &topology)
{
    NS_LOG_FUNCTION (this);
    m_topology = topology;
    m_routes.clear ();
    m_routeMetrics.clear ();
}

void
DijkstraRoutingProtocol::AddNeighbor (const std::string &node,
                                      const std::string &neighbor,
                                      const LinkMetrics &metrics)
{
    NS_LOG_FUNCTION (this << node << neighbor);
    m_topology[node][neighbor] = metrics;
    m_routes.clear ();
    m_routeMetrics.clear ();
}

void
DijkstraRoutingProtocol::RemoveNeighbor (const std::string &node,
                                         const std::string &neighbor)
{
    NS_LOG_FUNCTION (this << node << neighbor);
    if (m_topology.count (node) && m_topology[node].count (neighbor))
    {
        m_topology[node].erase (neighbor);
        m_routes.clear ();
        m_routeMetrics.clear ();
    }
}

void
DijkstraRoutingProtocol::UpdateLinkMetrics (const std::string &node,
                                           const std::string &neighbor,
                                           const LinkMetrics &metrics)
{
    NS_LOG_FUNCTION (this << node << neighbor);
    m_topology[node][neighbor] = metrics;
    m_routes.clear ();
    m_routeMetrics.clear ();
}

bool
DijkstraRoutingProtocol::HasRoute (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);
    
    // Check cached routes
    if (m_routes.count (src) && m_routes[src].count (dst))
    {
        return !m_routes[src][dst].empty ();
    }

    // Try to find route
    std::vector<std::string> route = RunDijkstra (src, dst);
    return !route.empty ();
}

double
DijkstraRoutingProtocol::GetRouteMetric (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (m_routeMetrics.count (src) && m_routeMetrics[src].count (dst))
    {
        return m_routeMetrics[src][dst];
    }

    // Calculate route
    std::vector<std::string> route = RunDijkstra (src, dst);
    if (route.empty ())
    {
        return std::numeric_limits<double>::infinity ();
    }

    // Calculate total cost
    double totalCost = 0.0;
    for (size_t i = 0; i < route.size () - 1; ++i)
    {
        std::string u = route[i];
        std::string v = route[i + 1];
        if (m_topology.count (u) && m_topology[u].count (v))
        {
            totalCost += CalculateLinkCost (m_topology[u][v]);
        }
    }

    m_routeMetrics[src][dst] = totalCost;
    return totalCost;
}

std::vector<std::string>
DijkstraRoutingProtocol::RunDijkstra (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (m_topology.empty ())
    {
        NS_LOG_WARN ("Topology is empty");
        return {};
    }

    // Priority queue: (distance, node)
    using NodeDist = std::pair<double, std::string>;
    std::priority_queue<NodeDist, std::vector<NodeDist>, std::greater<NodeDist>> pq;

    // Distance and predecessor maps
    std::map<std::string, double> dist;
    std::map<std::string, std::string> prev;

    // Initialize
    for (const auto &entry : m_topology)
    {
        dist[entry.first] = std::numeric_limits<double>::infinity ();
    }
    dist[src] = 0.0;
    pq.push ({0.0, src});

    while (!pq.empty ())
    {
        auto top = pq.top ();
        pq.pop ();
        double d = top.first;
        std::string u = top.second;

        if (u == dst)
        {
            break;
        }

        if (d > dist[u])
        {
            continue;
        }

        // Explore neighbors
        if (m_topology.count (u))
        {
            for (const auto &entry : m_topology[u])
            {
                std::string v = entry.first;
                const LinkMetrics &metrics = entry.second;

                if (!metrics.isAvailable)
                {
                    continue;
                }

                double cost = CalculateLinkCost (metrics);
                double newDist = dist[u] + cost;

                if (newDist < dist[v])
                {
                    dist[v] = newDist;
                    prev[v] = u;
                    pq.push ({newDist, v});
                }
            }
        }
    }

    // Reconstruct path
    if (prev.find (dst) == prev.end ())
    {
        NS_LOG_DEBUG ("No path from " << src << " to " << dst);
        return {};
    }

    std::vector<std::string> path;
    std::string current = dst;
    while (current != src)
    {
        path.push_back (current);
        current = prev[current];
        if (prev.find (current) == prev.end () && current != src)
        {
            NS_LOG_ERROR ("Path reconstruction failed");
            return {};
        }
    }
    path.push_back (src);
    std::reverse (path.begin (), path.end ());

    NS_LOG_DEBUG ("Dijkstra path: " << RouteToString (path));
    return path;
}

double
DijkstraRoutingProtocol::CalculateLinkCost (const LinkMetrics &metrics) const
{
    NS_LOG_FUNCTION (this);
    
    // Cost is based on inverse of fidelity and success rate
    // Higher fidelity and success rate = lower cost
    double fidelityCost = 1.0 / (metrics.fidelity + 1e-10);
    double successCost = 1.0 / (metrics.successRate + 1e-10);
    
    // Weighted combination
    return 0.5 * fidelityCost + 0.5 * successCost;
}

} // namespace ns3