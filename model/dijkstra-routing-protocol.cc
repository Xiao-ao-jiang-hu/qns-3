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
    m_lastLabelsByNode.clear ();
    QuantumRoutingProtocol::DoDispose ();
}

void
DijkstraRoutingProtocol::Initialize (void)
{
    NS_LOG_FUNCTION (this);
    if (m_metricModel == nullptr)
    {
        m_metricModel = CreateObject<BottleneckFidelityRoutingMetric> ();
    }
}

void
DijkstraRoutingProtocol::SetNetworkLayer (QuantumNetworkLayer* netLayer)
{
    NS_LOG_FUNCTION (this << netLayer);
    QuantumRoutingProtocol::SetNetworkLayer (netLayer);
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

    QuantumRoutingLabel bestState = RunSingleLabelSearch (src, dst);
    std::vector<std::string> route = bestState.path;

    if (route.empty ())
    {
        m_routesFailed++;
        NS_LOG_WARN ("No route found from " << src << " to " << dst);
    }
    else
    {
        m_routesComputed++;
        m_routes[src][dst] = route;
        m_routeMetrics[src][dst] = m_metricModel->GetScore (bestState);
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
    m_lastLabelsByNode.clear ();
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
    m_lastLabelsByNode.clear ();
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
    m_lastLabelsByNode.clear ();
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
        m_lastLabelsByNode.clear ();
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
    m_lastLabelsByNode.clear ();
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
    QuantumRoutingLabel state = RunSingleLabelSearch (src, dst);
    return !state.path.empty ();
}

double
DijkstraRoutingProtocol::GetRouteMetric (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (m_routeMetrics.count (src) && m_routeMetrics[src].count (dst))
    {
        return m_routeMetrics[src][dst];
    }

    QuantumRoutingLabel state = RunSingleLabelSearch (src, dst);
    if (state.path.empty ())
    {
        return std::numeric_limits<double>::infinity ();
    }

    m_routeMetrics[src][dst] = m_metricModel->GetScore (state);
    return m_routeMetrics[src][dst];
}

std::vector<QuantumRoutingLabel>
DijkstraRoutingProtocol::GetNodeLabels (const std::string &node) const
{
    auto it = m_lastLabelsByNode.find (node);
    if (it == m_lastLabelsByNode.end ())
    {
        return {};
    }
    return it->second;
}

QuantumRoutingLabel
DijkstraRoutingProtocol::RunSingleLabelSearch (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (m_topology.empty ())
    {
        NS_LOG_WARN ("Topology is empty");
        m_lastLabelsByNode.clear ();
        return QuantumRoutingLabel{};
    }

    if (m_metricModel == nullptr)
    {
        m_metricModel = CreateObject<BottleneckFidelityRoutingMetric> ();
    }

    struct QueueEntry
    {
        double score;
        QuantumRoutingLabel state;

        bool operator< (const QueueEntry &other) const
        {
            return score < other.score;
        }
    };

    std::priority_queue<QueueEntry> frontier;
    std::map<std::string, QuantumRoutingLabel> bestByNode;

    QuantumRoutingLabel start = m_metricModel->CreateInitialLabel (src);
    frontier.push ({m_metricModel->GetScore (start), start});
    bestByNode[src] = start;

    QuantumRoutingLabel bestDestination;

    while (!frontier.empty ())
    {
        QueueEntry entry = frontier.top ();
        frontier.pop ();

        const QuantumRoutingLabel &current = entry.state;
        const std::string &node = current.path.back ();

        auto bestIt = bestByNode.find (node);
        if (bestIt != bestByNode.end () && m_metricModel->IsBetter (bestIt->second, current) &&
            bestIt->second.path != current.path)
        {
            continue;
        }

        if (node == dst)
        {
            if (bestDestination.path.empty () ||
                m_metricModel->IsBetter (current, bestDestination))
            {
                bestDestination = current;
            }
            continue;
        }

        auto topoIt = m_topology.find (node);
        if (topoIt == m_topology.end ())
        {
            continue;
        }

        for (const auto &neighborEntry : topoIt->second)
        {
            QuantumRoutingLabel next;
            if (!m_metricModel->ExtendLabel (m_networkLayer,
                                             current,
                                             neighborEntry.first,
                                             neighborEntry.second,
                                             next))
            {
                continue;
            }

            auto incumbent = bestByNode.find (neighborEntry.first);
            if (incumbent != bestByNode.end () &&
                !m_metricModel->IsBetter (next, incumbent->second))
            {
                continue;
            }

            bestByNode[neighborEntry.first] = next;
            frontier.push ({m_metricModel->GetScore (next), next});
        }
    }

    if (!bestDestination.path.empty ())
    {
        NS_LOG_DEBUG ("Single-label Dijkstra path: " << RouteToString (bestDestination.path));
    }

    m_lastLabelsByNode.clear ();
    for (const auto &entry : bestByNode)
    {
        m_lastLabelsByNode[entry.first].push_back (entry.second);
    }

    return bestDestination;
}

} // namespace ns3
