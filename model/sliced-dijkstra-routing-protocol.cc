#include "ns3/sliced-dijkstra-routing-protocol.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SlicedDijkstraRoutingProtocol");

NS_OBJECT_ENSURE_REGISTERED (SlicedDijkstraRoutingProtocol);

namespace {

bool
SameRoutingState (const QuantumRoutingLabel &lhs, const QuantumRoutingLabel &rhs,
                  Ptr<QuantumRoutingMetric> metric)
{
    if (metric == nullptr)
    {
        return lhs.path == rhs.path;
    }
    return lhs.path == rhs.path &&
           std::fabs (metric->GetScore (lhs) - metric->GetScore (rhs)) < 1e-12;
}

} // namespace

TypeId
SlicedDijkstraRoutingProtocol::GetTypeId (void)
{
    static TypeId tid =
        TypeId ("ns3::SlicedDijkstraRoutingProtocol")
            .SetParent<QuantumRoutingProtocol> ()
            .SetGroupName ("Quantum")
            .AddConstructor<SlicedDijkstraRoutingProtocol> ()
            .AddAttribute ("K",
                           "Maximum number of labels retained per node.",
                           UintegerValue (4),
                           MakeUintegerAccessor (&SlicedDijkstraRoutingProtocol::m_k),
                           MakeUintegerChecker<uint32_t> (1))
            .AddAttribute ("BucketWidthMs",
                           "Bucket width used to slice the Tmax axis.",
                           DoubleValue (10.0),
                           MakeDoubleAccessor (&SlicedDijkstraRoutingProtocol::m_bucketWidthMs),
                           MakeDoubleChecker<double> (0.0))
            .AddAttribute ("UseBuckets",
                           "Whether to enforce bucket-based label diversity.",
                           BooleanValue (true),
                           MakeBooleanAccessor (&SlicedDijkstraRoutingProtocol::m_useBuckets),
                           MakeBooleanChecker ());
    return tid;
}

SlicedDijkstraRoutingProtocol::SlicedDijkstraRoutingProtocol ()
    : m_k (4),
      m_bucketWidthMs (10.0),
      m_useBuckets (true)
{
    NS_LOG_FUNCTION (this);
}

SlicedDijkstraRoutingProtocol::~SlicedDijkstraRoutingProtocol ()
{
    NS_LOG_FUNCTION (this);
}

void
SlicedDijkstraRoutingProtocol::DoDispose (void)
{
    NS_LOG_FUNCTION (this);
    m_topology.clear ();
    m_routes.clear ();
    m_routeMetrics.clear ();
    m_lastLabelsByNode.clear ();
    QuantumRoutingProtocol::DoDispose ();
}

void
SlicedDijkstraRoutingProtocol::Initialize (void)
{
    NS_LOG_FUNCTION (this);
    if (m_metricModel == nullptr)
    {
        m_metricModel = CreateObject<BottleneckFidelityRoutingMetric> ();
    }
}

void
SlicedDijkstraRoutingProtocol::SetNetworkLayer (QuantumNetworkLayer* netLayer)
{
    NS_LOG_FUNCTION (this << netLayer);
    QuantumRoutingProtocol::SetNetworkLayer (netLayer);
}

std::vector<std::string>
SlicedDijkstraRoutingProtocol::CalculateRoute (const std::string &src, const std::string &dst)
{
    NS_LOG_FUNCTION (this << src << dst);

    if (src == dst)
    {
        return {src};
    }

    if (m_routes.count (src) && m_routes[src].count (dst))
    {
        return m_routes[src][dst];
    }

    std::map<std::string, std::vector<QuantumRoutingLabel>> labelsByNode;
    QuantumRoutingLabel best = RunMultiLabelSearch (src, dst, labelsByNode);
    m_lastLabelsByNode = labelsByNode;

    if (best.path.empty ())
    {
        return {};
    }

    m_routes[src][dst] = best.path;
    m_routeMetrics[src][dst] = m_metricModel->GetScore (best);
    return best.path;
}

void
SlicedDijkstraRoutingProtocol::NotifyTopologyChange (void)
{
    NS_LOG_FUNCTION (this);
    m_routes.clear ();
    m_routeMetrics.clear ();
    m_lastLabelsByNode.clear ();
}

std::string
SlicedDijkstraRoutingProtocol::RouteToString (const std::vector<std::string> &route)
{
    return QuantumRoutingProtocol::RouteToString (route);
}

void
SlicedDijkstraRoutingProtocol::UpdateTopology (
    const std::map<std::string, std::map<std::string, LinkMetrics>> &topology)
{
    m_topology = topology;
    NotifyTopologyChange ();
}

void
SlicedDijkstraRoutingProtocol::AddNeighbor (const std::string &node,
                                            const std::string &neighbor,
                                            const LinkMetrics &metrics)
{
    m_topology[node][neighbor] = metrics;
    NotifyTopologyChange ();
}

void
SlicedDijkstraRoutingProtocol::RemoveNeighbor (const std::string &node,
                                               const std::string &neighbor)
{
    auto nodeIt = m_topology.find (node);
    if (nodeIt != m_topology.end ())
    {
        nodeIt->second.erase (neighbor);
    }
    NotifyTopologyChange ();
}

void
SlicedDijkstraRoutingProtocol::UpdateLinkMetrics (const std::string &node,
                                                  const std::string &neighbor,
                                                  const LinkMetrics &metrics)
{
    m_topology[node][neighbor] = metrics;
    NotifyTopologyChange ();
}

bool
SlicedDijkstraRoutingProtocol::HasRoute (const std::string &src, const std::string &dst)
{
    return !CalculateRoute (src, dst).empty ();
}

double
SlicedDijkstraRoutingProtocol::GetRouteMetric (const std::string &src, const std::string &dst)
{
    if (m_routeMetrics.count (src) && m_routeMetrics[src].count (dst))
    {
        return m_routeMetrics[src][dst];
    }

    std::map<std::string, std::vector<QuantumRoutingLabel>> labelsByNode;
    QuantumRoutingLabel best = RunMultiLabelSearch (src, dst, labelsByNode);
    m_lastLabelsByNode = labelsByNode;
    if (best.path.empty ())
    {
        return std::numeric_limits<double>::infinity ();
    }

    m_routeMetrics[src][dst] = m_metricModel->GetScore (best);
    return m_routeMetrics[src][dst];
}

std::vector<QuantumRoutingLabel>
SlicedDijkstraRoutingProtocol::GetNodeLabels (const std::string &node) const
{
    auto it = m_lastLabelsByNode.find (node);
    if (it == m_lastLabelsByNode.end ())
    {
        return {};
    }
    return it->second;
}

QuantumRoutingLabel
SlicedDijkstraRoutingProtocol::RunMultiLabelSearch (
    const std::string &src,
    const std::string &dst,
    std::map<std::string, std::vector<QuantumRoutingLabel>> &labelsByNode)
{
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
    QuantumRoutingLabel start = m_metricModel->CreateInitialLabel (src);
    labelsByNode[src].push_back (start);
    frontier.push ({m_metricModel->GetScore (start), start});

    QuantumRoutingLabel bestDestination;

    while (!frontier.empty ())
    {
        QueueEntry entry = frontier.top ();
        frontier.pop ();

        const QuantumRoutingLabel &current = entry.state;
        const std::string &node = current.path.back ();

        auto labelIt = labelsByNode.find (node);
        if (labelIt == labelsByNode.end ())
        {
            continue;
        }

        bool stillActive = false;
        for (const auto &kept : labelIt->second)
        {
            if (SameRoutingState (kept, current, m_metricModel))
            {
                stillActive = true;
                break;
            }
        }
        if (!stillActive)
        {
            continue;
        }

        if (node == dst)
        {
            if (bestDestination.path.empty () || m_metricModel->IsBetter (current, bestDestination))
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

            if (AdmitLabel (neighborEntry.first, next, labelsByNode))
            {
                frontier.push ({m_metricModel->GetScore (next), next});
            }
        }
    }

    return bestDestination;
}

bool
SlicedDijkstraRoutingProtocol::AdmitLabel (
    const std::string &node,
    const QuantumRoutingLabel &candidate,
    std::map<std::string, std::vector<QuantumRoutingLabel>> &labelsByNode) const
{
    auto &labels = labelsByNode[node];

    if (m_useBuckets)
    {
        int64_t bucket = GetBucketId (candidate);
        for (auto it = labels.begin (); it != labels.end (); ++it)
        {
            if (GetBucketId (*it) != bucket)
            {
                continue;
            }

            if (m_metricModel->IsBetter (*it, candidate) ||
                SameRoutingState (*it, candidate, m_metricModel))
            {
                return false;
            }

            if (m_metricModel->IsBetter (candidate, *it))
            {
                labels.erase (it);
                break;
            }
        }
    }
    else
    {
        for (auto it = labels.begin (); it != labels.end ();)
        {
            if (m_metricModel->Dominates (*it, candidate) ||
                SameRoutingState (*it, candidate, m_metricModel))
            {
                return false;
            }
            if (m_metricModel->Dominates (candidate, *it))
            {
                it = labels.erase (it);
                continue;
            }
            ++it;
        }
    }

    labels.push_back (candidate);
    std::sort (labels.begin (), labels.end (),
               [this] (const QuantumRoutingLabel &lhs, const QuantumRoutingLabel &rhs) {
                   return m_metricModel->IsBetter (lhs, rhs);
               });

    if (labels.size () > m_k)
    {
        labels.resize (m_k);
    }

    for (const auto &kept : labels)
    {
        if (SameRoutingState (kept, candidate, m_metricModel))
        {
            return true;
        }
    }
    return false;
}

int64_t
SlicedDijkstraRoutingProtocol::GetBucketId (const QuantumRoutingLabel &state) const
{
    double tMaxMs = 0.0;
    auto it = state.scalars.find ("t_max_ms");
    if (it != state.scalars.end ())
    {
        tMaxMs = it->second;
    }

    if (m_bucketWidthMs <= 0.0)
    {
        return static_cast<int64_t> (std::floor (tMaxMs));
    }
    return static_cast<int64_t> (std::floor (tMaxMs / m_bucketWidthMs));
}

} // namespace ns3
