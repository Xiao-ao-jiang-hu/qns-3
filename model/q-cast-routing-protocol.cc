#include "ns3/q-cast-routing-protocol.h"

#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <limits>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QCastRoutingProtocol");

NS_OBJECT_ENSURE_REGISTERED (QCastRoutingProtocol);

TypeId
QCastRoutingProtocol::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QCastRoutingProtocol")
          .SetParent<QuantumRoutingProtocol> ()
          .SetGroupName ("Quantum")
          .AddConstructor<QCastRoutingProtocol> ()
          .AddAttribute ("MaxHops", "Maximum allowed hops for path",
                         UintegerValue (10),
                         MakeUintegerAccessor (&QCastRoutingProtocol::m_maxHops),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("KHop", "k-hop neighborhood for local recovery",
                         UintegerValue (3),
                         MakeUintegerAccessor (&QCastRoutingProtocol::m_kHop),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("Alpha", "Swap failure rate constant",
                         DoubleValue (0.1),
                         MakeDoubleAccessor (&QCastRoutingProtocol::m_alpha),
                         MakeDoubleChecker<double> (0.0, 1.0))
          .AddAttribute ("NodeCapacity", "Node capacity (concurrent paths)",
                         UintegerValue (5),
                         MakeUintegerAccessor (&QCastRoutingProtocol::m_nodeCapacity),
                         MakeUintegerChecker<uint32_t> ());
  return tid;
}

QCastRoutingProtocol::QCastRoutingProtocol ()
    : m_maxHops (10),
      m_kHop (3),
      m_alpha (0.1),
      m_nodeCapacity (5),
      m_nextPathId (1),
      m_initialized (false),
      m_pathsComputed (0),
      m_recoveryPathsFound (0),
      m_totalComputeTime (Seconds (0))
{
  NS_LOG_FUNCTION (this);
}

QCastRoutingProtocol::~QCastRoutingProtocol ()
{
  NS_LOG_FUNCTION (this);
}

void
QCastRoutingProtocol::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_qcastPaths.clear ();
  m_linkProbabilities.clear ();
  QuantumRoutingProtocol::DoDispose ();
}

void
QCastRoutingProtocol::Initialize (void)
{
  NS_LOG_FUNCTION (this);

  if (m_initialized)
    {
      return;
    }

  // Cache link probabilities from network layer
  if (m_networkLayer)
    {
      const auto &neighbors = m_networkLayer->GetNeighbors ();
      for (const auto &entry : neighbors)
        {
          const std::string &neighbor = entry.first;
          const NeighborInfo &info = entry.second;
          
          // Store link success probability
          // Assuming linkFidelity relates to success rate
          double linkProb = info.linkSuccessRate;
          
          if (!m_localNode.empty ())
            {
              m_linkProbabilities[m_localNode][neighbor] = linkProb;
            }
        }
    }

  m_initialized = true;
  NS_LOG_INFO ("Q-CAST routing protocol initialized");
}

std::vector<std::string>
QCastRoutingProtocol::CalculateRoute (const std::string &src, const std::string &dst)
{
  NS_LOG_FUNCTION (this << src << dst);

  if (!m_initialized)
    {
      Initialize ();
    }

  // Create single request and use G-EDA
  QCastRequest request;
  request.srcNode = src;
  request.dstNode = dst;
  request.minFidelity = 0.0; // No minimum fidelity constraint for basic routing
  request.requestId = 0;

  auto results = CalculateRoutesGEDA ({request});

  if (results.empty ())
    {
      NS_LOG_WARN ("No route found from " << src << " to " << dst);
      return {};
    }

  return results.begin ()->second.primaryPath;
}

void
QCastRoutingProtocol::NotifyTopologyChange (void)
{
  NS_LOG_FUNCTION (this);

  // Re-initialize to update link probabilities
  m_initialized = false;
  Initialize ();

  // Clear cached paths as they may no longer be valid
  m_qcastPaths.clear ();
}

void
QCastRoutingProtocol::SetNetworkLayer (QuantumNetworkLayer* netLayer)
{
  NS_LOG_FUNCTION (this);
  // Call base class - stores m_networkLayer and derives m_localNode from network layer owner
  QuantumRoutingProtocol::SetNetworkLayer (netLayer);
  // Reset so Initialize() re-caches link probabilities on next call
  m_initialized = false;
  m_linkProbabilities.clear ();
}

void
QCastRoutingProtocol::UpdateTopology (
    const std::map<std::string, std::map<std::string, LinkMetrics>> &topology)
{
  NS_LOG_FUNCTION (this);
  // Rebuild link probability cache from full topology
  m_linkProbabilities.clear ();
  for (const auto &nodeEntry : topology)
    {
      for (const auto &linkEntry : nodeEntry.second)
        {
          m_linkProbabilities[nodeEntry.first][linkEntry.first] =
              linkEntry.second.successRate;
        }
    }
  m_initialized = false;
}

void
QCastRoutingProtocol::AddNeighbor (const std::string &node, const std::string &neighbor,
                                    const LinkMetrics &metrics)
{
  NS_LOG_FUNCTION (this << node << neighbor);
  m_linkProbabilities[node][neighbor] = metrics.successRate;
  m_initialized = false;
}

void
QCastRoutingProtocol::RemoveNeighbor (const std::string &node, const std::string &neighbor)
{
  NS_LOG_FUNCTION (this << node << neighbor);
  auto it = m_linkProbabilities.find (node);
  if (it != m_linkProbabilities.end ())
    {
      it->second.erase (neighbor);
    }
  m_initialized = false;
}

void
QCastRoutingProtocol::UpdateLinkMetrics (const std::string &node, const std::string &neighbor,
                                          const LinkMetrics &metrics)
{
  NS_LOG_FUNCTION (this << node << neighbor);
  m_linkProbabilities[node][neighbor] = metrics.successRate;
}

bool
QCastRoutingProtocol::HasRoute (const std::string &src, const std::string &dst)
{
  NS_LOG_FUNCTION (this << src << dst);
  auto route = CalculateRoute (src, dst);
  return !route.empty ();
}

double
QCastRoutingProtocol::GetRouteMetric (const std::string &src, const std::string &dst)
{
  NS_LOG_FUNCTION (this << src << dst);
  QCastRequest req;
  req.srcNode = src;
  req.dstNode = dst;
  req.minFidelity = 0.0;
  req.requestId = 0;
  auto results = CalculateRoutesGEDA ({req});
  if (results.empty ())
    return std::numeric_limits<double>::infinity ();
  return 1.0 / results.begin ()->second.primaryEt; // Inverse of E_t as cost
}

std::string
QCastRoutingProtocol::RouteToString (const std::vector<std::string> &route)
{
  std::string result;
  for (size_t i = 0; i < route.size (); ++i)
    {
      result += route[i];
      if (i < route.size () - 1)
        result += " -> ";
    }
  return result;
}

void
QCastRoutingProtocol::SetMaxHops (uint32_t maxHops)
{
  m_maxHops = maxHops;
}

void
QCastRoutingProtocol::SetKHop (uint32_t kHop)
{
  m_kHop = kHop;
}

void
QCastRoutingProtocol::SetAlpha (double alpha)
{
  m_alpha = alpha;
}

void
QCastRoutingProtocol::SetNodeCapacity (uint32_t capacity)
{
  m_nodeCapacity = capacity;
}

std::map<uint32_t, QCastPath>
QCastRoutingProtocol::CalculateRoutesGEDA (const std::vector<QCastRequest> &requests)
{
  NS_LOG_FUNCTION (this << requests.size ());

  auto startTime = Simulator::Now ();

  std::map<uint32_t, QCastPath> selectedPaths;
  std::set<std::string> availableNodes;
  std::set<std::pair<std::string, std::string>> availableEdges;

  // Initialize available resources from global topology (m_linkProbabilities).
  // When UpdateTopology() has been called with the full network graph, this enables
  // multi-hop path discovery well beyond the local node's direct neighbors.
  if (!m_linkProbabilities.empty ())
    {
      for (const auto &nodeEntry : m_linkProbabilities)
        {
          availableNodes.insert (nodeEntry.first);
          for (const auto &neighborEntry : nodeEntry.second)
            {
              availableNodes.insert (neighborEntry.first);
              availableEdges.insert (std::make_pair (nodeEntry.first, neighborEntry.first));
            }
        }
    }
  else if (m_networkLayer)
    {
      // Fallback: seed only from the local node's direct neighbors
      const auto &neighbors = m_networkLayer->GetNeighbors ();
      for (const auto &entry : neighbors)
        {
          availableNodes.insert (entry.first);
          availableNodes.insert (m_localNode);

          // Add edge in both directions
          availableEdges.insert (std::make_pair (m_localNode, entry.first));
          availableEdges.insert (std::make_pair (entry.first, m_localNode));
        }
    }

  // Per-spec §2.6 step 10: treat C_v as a decrementable counter, not a binary flag.
  // Only remove a node from availableNodes once its remaining capacity reaches 0.
  std::map<std::string, uint32_t> nodeRemainingCap;
  for (const auto &n : availableNodes)
    {
      nodeRemainingCap[n] = m_nodeCapacity;
    }

  // Track unprocessed requests
  std::vector<QCastRequest> remainingRequests = requests;

  // Greedy selection loop (G-EDA)
  while (!remainingRequests.empty ())
    {
      // Find best path among all remaining requests
      std::unique_ptr<QCastLabel> bestLabel = nullptr;
      uint32_t bestRequestIdx = 0;
      double bestEt = 0.0;

      for (size_t i = 0; i < remainingRequests.size (); ++i)
        {
          const auto &req = remainingRequests[i];
          
          auto label = ExtendedDijkstra (req.srcNode, req.dstNode, availableNodes, availableEdges);
          
          if (label && label->expectedThroughput > bestEt)
            {
              bestEt = label->expectedThroughput;
              bestLabel = std::move (label);
              bestRequestIdx = i;
            }
        }

      if (!bestLabel)
        {
          NS_LOG_INFO ("No more feasible paths found");
          break;
        }

      // Store the selected path
      QCastPath qcastPath;
      qcastPath.requestId = remainingRequests[bestRequestIdx].requestId;
      qcastPath.primaryPath = bestLabel->path;
      qcastPath.primaryChannels = bestLabel->channels;
      qcastPath.primaryEt = bestLabel->expectedThroughput;

      // Calculate per-hop probabilities
      for (size_t i = 0; i < bestLabel->path.size () - 1; ++i)
        {
          double prob = GetLinkProbability (bestLabel->path[i], bestLabel->path[i + 1]);
          qcastPath.linkProbabilities.push_back (prob);
        }

      // Discover recovery paths
      qcastPath.recoveryPaths =
          DiscoverRecoveryPaths (qcastPath.primaryPath, availableNodes, availableEdges);

      // Store path
      uint32_t pathId = m_nextPathId++;
      selectedPaths[pathId] = qcastPath;
      m_qcastPaths[pathId] = qcastPath;

      NS_LOG_INFO ("Selected path " << pathId << ": " << RouteToString (qcastPath.primaryPath)
                                   << " with E_t=" << qcastPath.primaryEt);

      // Update residual resources
      // Edges: W_e=1 per spec — consumed fully by one path (remove both directions).
      // Nodes: decrement per-node capacity counter; only remove when it hits 0.
      {
        const auto &updatePath = [&] (const std::vector<std::string> &nodePath)
        {
          for (size_t k = 0; k + 1 < nodePath.size (); ++k)
            {
              availableEdges.erase ({nodePath[k], nodePath[k + 1]});
              availableEdges.erase ({nodePath[k + 1], nodePath[k]});
            }
          for (const auto &nd : nodePath)
            {
              auto &rem = nodeRemainingCap[nd];
              if (rem > 0)
                {
                  --rem;
                  if (rem == 0)
                    availableNodes.erase (nd);
                }
            }
        };

        updatePath (qcastPath.primaryPath);
        for (const auto &rpEntry : qcastPath.recoveryPaths)
          updatePath (rpEntry.second);
      }

      // Remove selected request
      remainingRequests.erase (remainingRequests.begin () + bestRequestIdx);

      m_pathsComputed++;
    }

  // Update statistics
  auto endTime = Simulator::Now ();
  m_totalComputeTime += (endTime - startTime);

  NS_LOG_INFO ("G-EDA completed: " << selectedPaths.size () << " paths selected out of "
                                   << requests.size () << " requests");

  return selectedPaths;
}

std::unique_ptr<QCastLabel>
QCastRoutingProtocol::ExtendedDijkstra (
    const std::string &src, const std::string &dst,
    const std::set<std::string> &availableNodes,
    const std::set<std::pair<std::string, std::string>> &availableEdges)
{
  NS_LOG_FUNCTION (this << src << dst);

  // Priority queue for labels (max heap based on E_t)
  std::priority_queue<QCastLabel> pq;
  
  // Map from node to its Pareto-optimal labels
  std::map<std::string, std::vector<QCastLabel>> paretoLabels;

  // Initialize source label
  QCastLabel srcLabel;
  srcLabel.node = src;
  srcLabel.expectedThroughput = 1.0;
  srcLabel.pathWidth = std::numeric_limits<double>::max ();
  srcLabel.hopCount = 0;
  srcLabel.path.push_back (src);

  pq.push (srcLabel);
  paretoLabels[src].push_back (srcLabel);

  while (!pq.empty ())
    {
      QCastLabel current = pq.top ();
      pq.pop ();

      // Check if we reached the destination
      if (current.node == dst)
        {
          NS_LOG_LOGIC ("Found path to " << dst << " with E_t=" << current.expectedThroughput);
          return std::make_unique<QCastLabel> (current);
        }

      // Check hop limit
      if (current.hopCount >= m_maxHops)
        {
          continue;
        }

      // Expand to neighbors using global topology (m_linkProbabilities).
      // This allows the Dijkstra to traverse intermediate nodes for multi-hop paths.
      auto probIt = m_linkProbabilities.find (current.node);
      if (probIt != m_linkProbabilities.end ())
        {
          // For the local node we can look up the actual quantum channel object.
          const std::map<std::string, NeighborInfo> *localNeighbors = nullptr;
          if (m_networkLayer && current.node == m_localNode)
            {
              localNeighbors = &m_networkLayer->GetNeighbors ();
            }

          for (const auto &neighborEntry : probIt->second)
            {
              const std::string &neighbor = neighborEntry.first;
              double successRate = neighborEntry.second;

              // Check if neighbor is available
              if (availableNodes.find (neighbor) == availableNodes.end ())
                {
                  continue;
                }

              // Check if edge is available
              if (!IsEdgeAvailable (current.node, neighbor, availableEdges))
                {
                  continue;
                }

              // Resolve quantum channel (only available for local node's direct neighbors)
              Ptr<QuantumChannel> channel = nullptr;
              if (localNeighbors)
                {
                  auto nit = localNeighbors->find (neighbor);
                  if (nit != localNeighbors->end ())
                    {
                      channel = nit->second.channel;
                    }
                }

              // Create new label
              QCastLabel newLabel;
              newLabel.node = neighbor;
              newLabel.path = current.path;
              newLabel.path.push_back (neighbor);
              newLabel.channels = current.channels;
              if (channel)
                {
                  newLabel.channels.push_back (channel);
                }
              newLabel.pathWidth = std::min (current.pathWidth, successRate);
              newLabel.hopCount = current.hopCount + 1;

              // Calculate expected throughput
              double linkProb = GetLinkProbability (current.node, neighbor);
              newLabel.expectedThroughput = CalculateExpectedThroughput (current, linkProb);

              // Check Pareto dominance
              bool dominated = false;
              auto &labels = paretoLabels[neighbor];

              for (auto it = labels.begin (); it != labels.end ();)
                {
                  if (Dominates (*it, newLabel))
                    {
                      dominated = true;
                      break;
                    }
                  if (Dominates (newLabel, *it))
                    {
                      it = labels.erase (it);
                    }
                  else
                    {
                      ++it;
                    }
                }

              if (!dominated)
                {
                  labels.push_back (newLabel);
                  pq.push (newLabel);
                }
            }
        }
      else if (m_networkLayer)
        {
          // Fallback: no global topology — expand only to local direct neighbors
          const auto &neighbors = m_networkLayer->GetNeighbors ();

          for (const auto &entry : neighbors)
            {
              const std::string &neighbor = entry.first;
              const NeighborInfo &info = entry.second;

              if (availableNodes.find (neighbor) == availableNodes.end ())
                {
                  continue;
                }
              if (!IsEdgeAvailable (current.node, neighbor, availableEdges))
                {
                  continue;
                }

              QCastLabel newLabel;
              newLabel.node = neighbor;
              newLabel.path = current.path;
              newLabel.path.push_back (neighbor);
              newLabel.channels = current.channels;
              if (info.channel)
                {
                  newLabel.channels.push_back (info.channel);
                }
              newLabel.pathWidth = std::min (current.pathWidth, info.linkSuccessRate);
              newLabel.hopCount = current.hopCount + 1;

              double linkProb = GetLinkProbability (current.node, neighbor);
              newLabel.expectedThroughput = CalculateExpectedThroughput (current, linkProb);

              bool dominated = false;
              auto &labels = paretoLabels[neighbor];

              for (auto it = labels.begin (); it != labels.end ();)
                {
                  if (Dominates (*it, newLabel))
                    {
                      dominated = true;
                      break;
                    }
                  if (Dominates (newLabel, *it))
                    {
                      it = labels.erase (it);
                    }
                  else
                    {
                      ++it;
                    }
                }

              if (!dominated)
                {
                  labels.push_back (newLabel);
                  pq.push (newLabel);
                }
            }
        }
    }

  // No path found
  return nullptr;
}

bool
QCastRoutingProtocol::Dominates (const QCastLabel &label1, const QCastLabel &label2) const
{
  // Label1 dominates Label2 if:
  // - E_t1 >= E_t2 AND
  // - Width1 >= Width2 AND
  // - Hops1 <= Hops2
  return (label1.expectedThroughput >= label2.expectedThroughput &&
          label1.pathWidth >= label2.pathWidth && label1.hopCount <= label2.hopCount);
}

double
QCastRoutingProtocol::CalculateExpectedThroughput (const QCastLabel &label,
                                                   double nextHopProb) const
{
  // E_t(new) = E_t(current) * p_next_hop * S(h+1)/S(h)
  double currentSwapProb = CalculateSwapSuccessProb (label.hopCount);
  double newSwapProb = CalculateSwapSuccessProb (label.hopCount + 1);
  
  if (currentSwapProb == 0.0)
    {
      return label.expectedThroughput * nextHopProb * newSwapProb;
    }
  
  return label.expectedThroughput * nextHopProb * (newSwapProb / currentSwapProb);
}

double
QCastRoutingProtocol::CalculateSwapSuccessProb (uint32_t hopCount) const
{
  if (hopCount <= 1)
    {
      return 1.0;
    }
  
  // S(h) ≈ exp(-α * log2(h))
  return std::exp (-m_alpha * std::log2 (static_cast<double> (hopCount)));
}

std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>>
QCastRoutingProtocol::DiscoverRecoveryPaths (
    const std::vector<std::string> &primaryPath,
    const std::set<std::string> &availableNodes,
    const std::set<std::pair<std::string, std::string>> &availableEdges)
{
  NS_LOG_FUNCTION (this << RouteToString (primaryPath));

  std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>> recoveryPaths;

  if (primaryPath.size () < 3)
    {
      return recoveryPaths;
    }

  // Find recovery paths for nodes within k hops.
  // IMPORTANT: the BFS for ring (i, j) must exclude the primary-path edges
  // in the recovered segment [i..j-1] so that the recovery path is truly
  // independent.  Otherwise BFS may return the primary itself, which would
  // also fail when those links are down.
  for (size_t i = 0; i < primaryPath.size () - 1; ++i)
    {
      for (size_t j = i + 2; j < std::min (i + m_kHop + 1, primaryPath.size ()); ++j)
        {
          const std::string &src = primaryPath[i];
          const std::string &dst = primaryPath[j];

          // Build available-edges set that excludes primary-path links [i..j-1]
          std::set<std::pair<std::string, std::string>> bfsEdges = availableEdges;
          for (size_t k = i; k < j; ++k)
            {
              bfsEdges.erase ({primaryPath[k], primaryPath[k + 1]});
              bfsEdges.erase ({primaryPath[k + 1], primaryPath[k]});
            }

          // Find recovery path using BFS on the restricted edge set
          auto recoveryPath = FindRecoveryPathBFS (src, dst, availableNodes, bfsEdges, m_maxHops);

          if (!recoveryPath.empty ())
            {
              recoveryPaths[{static_cast<uint32_t> (i), static_cast<uint32_t> (j)}] =
                  recoveryPath;
              m_recoveryPathsFound++;

              NS_LOG_LOGIC ("Found recovery path from " << src << " to " << dst << ": "
                                                        << RouteToString (recoveryPath));
            }
        }
    }

  NS_LOG_INFO ("Found " << recoveryPaths.size () << " recovery paths");
  return recoveryPaths;
}

std::vector<std::string>
QCastRoutingProtocol::FindRecoveryPathBFS (
    const std::string &src, const std::string &dst,
    const std::set<std::string> &availableNodes,
    const std::set<std::pair<std::string, std::string>> &availableEdges, uint32_t maxLength)
{
  // BFS to find shortest path
  std::queue<std::pair<std::string, std::vector<std::string>>> q;
  std::set<std::string> visited;

  q.push ({src, {src}});
  visited.insert (src);

  while (!q.empty ())
    {
      auto [current, path] = q.front ();
      q.pop ();

      if (current == dst)
        {
          return path;
        }

      if (path.size () >= maxLength)
        {
          continue;
        }

      // Expand neighbors via global topology (same as ExtendedDijkstra) so recovery
      // paths can span nodes that are not in the local node's direct neighbor list.
      auto probIt = m_linkProbabilities.find (current);
      if (probIt != m_linkProbabilities.end ())
        {
          for (const auto &neighborEntry : probIt->second)
            {
              const std::string &neighbor = neighborEntry.first;
              if (visited.count (neighbor))
                continue;
              if (!availableNodes.count (neighbor))
                continue;
              if (!IsEdgeAvailable (current, neighbor, availableEdges))
                continue;
              visited.insert (neighbor);
              auto newPath = path;
              newPath.push_back (neighbor);
              q.push ({neighbor, newPath});
            }
        }
      else if (m_networkLayer)
        {
          // Fallback: local direct neighbors only
          const auto &neighbors = m_networkLayer->GetNeighbors ();
          for (const auto &entry : neighbors)
            {
              const std::string &neighbor = entry.first;
              if (visited.count (neighbor))
                continue;
              if (!availableNodes.count (neighbor))
                continue;
              if (!IsEdgeAvailable (current, neighbor, availableEdges))
                continue;
              visited.insert (neighbor);
              auto newPath = path;
              newPath.push_back (neighbor);
              q.push ({neighbor, newPath});
            }
        }
    }

  return {}; // No path found
}

bool
QCastRoutingProtocol::IsEdgeAvailable (
    const std::string &u, const std::string &v,
    const std::set<std::pair<std::string, std::string>> &availableEdges) const
{
  return availableEdges.find (std::make_pair (u, v)) != availableEdges.end () ||
         availableEdges.find (std::make_pair (v, u)) != availableEdges.end ();
}

double
QCastRoutingProtocol::GetLinkProbability (const std::string &u, const std::string &v) const
{
  // Try to get from cached probabilities
  auto it1 = m_linkProbabilities.find (u);
  if (it1 != m_linkProbabilities.end ())
    {
      auto it2 = it1->second.find (v);
      if (it2 != it1->second.end ())
        {
          return it2->second;
        }
    }

  // Query from network layer
  if (m_networkLayer)
    {
      const auto &neighbors = m_networkLayer->GetNeighbors ();
      auto it = neighbors.find (v);
      if (it != neighbors.end ())
        {
          return it->second.linkSuccessRate;
        }
    }

  return 0.0;
}

void
QCastRoutingProtocol::UpdateResidualResources (
    const QCastPath &path, std::set<std::string> &availableNodes,
    std::set<std::pair<std::string, std::string>> &availableEdges)
{
  // Remove primary path resources
  for (const auto &node : path.primaryPath)
    {
      availableNodes.erase (node);
    }

  for (size_t i = 0; i < path.primaryPath.size () - 1; ++i)
    {
      availableEdges.erase (std::make_pair (path.primaryPath[i], path.primaryPath[i + 1]));
      availableEdges.erase (std::make_pair (path.primaryPath[i + 1], path.primaryPath[i]));
    }

  // Remove recovery path resources
  for (const auto &entry : path.recoveryPaths)
    {
      const auto &recoveryPath = entry.second;
      for (const auto &node : recoveryPath)
        {
          availableNodes.erase (node);
        }

      for (size_t i = 0; i < recoveryPath.size () - 1; ++i)
        {
          availableEdges.erase (std::make_pair (recoveryPath[i], recoveryPath[i + 1]));
          availableEdges.erase (std::make_pair (recoveryPath[i + 1], recoveryPath[i]));
        }
    }
}

QCastPath
QCastRoutingProtocol::GetQCastPath (uint32_t pathId) const
{
  auto it = m_qcastPaths.find (pathId);
  if (it != m_qcastPaths.end ())
    {
      return it->second;
    }
  return QCastPath{};
}

std::vector<QCastRecoveryRing>
QCastRoutingProtocol::ExecuteXORRecovery (uint32_t pathId,
                                          const std::set<uint32_t> &failedLinks)
{
  NS_LOG_FUNCTION (this << pathId << failedLinks.size ());

  auto it = m_qcastPaths.find (pathId);
  if (it == m_qcastPaths.end ())
    {
      NS_LOG_WARN ("Path " << pathId << " not found");
      return {};
    }

  const QCastPath &path = it->second;
  std::vector<QCastRecoveryRing> activeRings;

  // For each recovery path, check if it can help recover failed links
  for (const auto &entry : path.recoveryPaths)
    {
      uint32_t startIdx = entry.first.first;
      uint32_t endIdx = entry.first.second;
      const auto &recoveryPath = entry.second;

      // Check if this recovery path covers any failed links
      bool canRecover = false;
      for (uint32_t failedIdx : failedLinks)
        {
          if (failedIdx >= startIdx && failedIdx < endIdx)
            {
              canRecover = true;
              break;
            }
        }

      if (canRecover)
        {
          QCastRecoveryRing ring;
          ring.pathId = pathId;
          ring.startIdx = startIdx;
          ring.endIdx = endIdx;
          ring.recoveryPath = recoveryPath;
          ring.isActive = true;
          
          // Get channels for recovery path
          if (m_networkLayer && recoveryPath.size () > 1)
            {
              for (size_t i = 0; i < recoveryPath.size () - 1; ++i)
                {
                  const auto &neighbors = m_networkLayer->GetNeighbors ();
                  auto it = neighbors.find (recoveryPath[i + 1]);
                  if (it != neighbors.end ())
                    {
                      ring.recoveryChannels.push_back (it->second.channel);
                    }
                }
            }
          
          activeRings.push_back (ring);
          
          NS_LOG_INFO ("Activated recovery ring for path " << pathId << " ["
                                                            << startIdx << ", " << endIdx << "]");
        }
    }

  return activeRings;
}

std::vector<std::pair<uint32_t, uint32_t>>
QCastRoutingProtocol::GenerateSwapSchedule (const std::vector<std::string> &pathNodes)
{
  NS_LOG_FUNCTION (this << pathNodes.size ());

  std::vector<std::pair<uint32_t, uint32_t>> schedule;

  if (pathNodes.size () <= 2)
    {
      // No swaps needed for direct connection
      return schedule;
    }

  // Logarithmic-time swap scheduling
  // Round 1: All odd-indexed nodes (1, 3, 5, ...) swap
  // Round 2: Nodes at positions 2, 6, 10, ... (every 4th starting from 2)
  // etc.

  uint32_t numNodes = static_cast<uint32_t> (pathNodes.size ());
  uint32_t round = 1;
  uint32_t step = 2;

  while (step / 2 < numNodes - 1)
    {
      for (uint32_t i = step / 2; i < numNodes - 1; i += step)
        {
          // Node at index i performs swap
          schedule.push_back ({round, i});
          NS_LOG_LOGIC ("Round " << round << ": Node " << pathNodes[i] << " (index " << i
                                  << ") performs swap");
        }
      
      round++;
      step *= 2;
    }

  NS_LOG_INFO ("Generated swap schedule with " << round - 1 << " rounds for " << numNodes
                                                << " nodes");

  return schedule;
}

} // namespace ns3
