/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Q-CAST routing protocol implementation
 *
 * This file implements the Q-CAST routing protocol for quantum networks.
 * The protocol uses Greedy Extended Dijkstra Algorithm (G-EDA) for online
 * path selection with k-hop link state information exchange (k=3).
 */

#include "qcast-routing-protocol.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include "../quantum-channel.h"
#include "../quantum-packet.h"
#include "../quantum-node.h"
#include "../quantum-phy-entity.h"

#include <algorithm>
#include <cmath>

NS_LOG_COMPONENT_DEFINE ("QCastRoutingProtocol");

namespace ns3 {
using namespace std;

// ===========================================================================
// QCastRoutingProtocol implementation
// ===========================================================================

QCastRoutingProtocol::QCastRoutingProtocol ()
  : m_routeExpirationTimeout (Seconds (30.0)),
    m_linkStateExchangeInterval (Seconds (1.0)),  // Exchange link state every second
    m_kHopDistance (3),  // k=3
    m_topologyDiscoveryInterval (Seconds (5.0)),  // Topology discovery interval (5 seconds)
    m_topologyConverged (false),
    m_myLsaSequenceNumber (0)
{
  NS_LOG_LOGIC ("Creating QCastRoutingProtocol with k=" << m_kHopDistance);
  ResetStatistics ();
}

QCastRoutingProtocol::~QCastRoutingProtocol ()
{
  NS_LOG_LOGIC ("Destroying QCastRoutingProtocol");
}

TypeId
QCastRoutingProtocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::QCastRoutingProtocol")
    .SetParent<QuantumRoutingProtocol> ()
    .AddConstructor<QCastRoutingProtocol> ();
  return tid;
}

// ===========================================================================
// Private helper methods
// ===========================================================================

void
QCastRoutingProtocol::BuildResidualNetworkGraph ()
{
  NS_LOG_LOGIC ("Building residual network graph");
  
  m_residualGraph.nodes.clear ();
  m_residualGraph.channels.clear ();
  
  // Determine which nodes to include in the graph
  std::set<std::string> nodesToInclude;
  
  if (m_topologyConverged && !m_globalTopology.IsEmpty ())
  {
    // Use global topology when converged
    NS_LOG_LOGIC ("Using global topology with " << m_globalTopology.GetNodeCount () << " nodes");
    
    // Include all nodes from global topology
    for (const auto& node : m_globalTopology.allNodes)
    {
      nodesToInclude.insert (node);
    }
  }
  else
  {
    // Fall back to neighbor information only
    NS_LOG_LOGIC ("Using neighbor-only topology (global topology not converged)");
    
    // Include current node
    if (m_networkLayer)
    {
      nodesToInclude.insert (m_networkLayer->GetAddress ());
    }
    
    // Include direct neighbors
    for (const auto& neighbor : m_neighbors)
    {
      nodesToInclude.insert (neighbor.first);
    }
  }
  
  // Build node information
  for (const auto& nodeAddress : nodesToInclude)
  {
    ResidualNodeInfo nodeInfo;
    nodeInfo.address = nodeAddress;
    nodeInfo.availableQubits = m_resourceManager->GetAvailableQubits (nodeAddress);
    
    // Get neighbor list based on topology knowledge
    if (m_topologyConverged && !m_globalTopology.IsEmpty ())
    {
      // Get neighbors from global topology
      nodeInfo.neighborAddresses = m_globalTopology.GetNeighbors (nodeAddress);
    }
    else
    {
      // Get neighbors from local knowledge
      // For current node, use m_neighbors
      if (m_networkLayer && nodeAddress == m_networkLayer->GetAddress ())
      {
        for (const auto& neighbor : m_neighbors)
        {
          nodeInfo.neighborAddresses.push_back (neighbor.first);
        }
      }
      else
      {
        // For other nodes, we don't know their neighbors in neighbor-only mode
        // Leave neighbor list empty
      }
    }
    
    m_residualGraph.nodes[nodeAddress] = nodeInfo;
    
    NS_LOG_LOGIC ("Added node " << nodeAddress << " with " 
                  << nodeInfo.neighborAddresses.size () << " neighbors, "
                  << nodeInfo.availableQubits << " available qubits");
  }
  
  // Build channel information from global topology
  if (m_topologyConverged && !m_globalTopology.IsEmpty ())
  {
    // Add channels for ALL connections in global topology
    for (const auto& conn : m_globalTopology.connections)
    {
      if (!conn.second)  // Skip if connection doesn't exist
        continue;
        
      std::string src = conn.first.first;
      std::string dst = conn.first.second;
      
      // Check if both nodes are in the graph
      if (m_residualGraph.nodes.find (src) == m_residualGraph.nodes.end () ||
          m_residualGraph.nodes.find (dst) == m_residualGraph.nodes.end ())
        continue;
      
      std::pair<std::string, std::string> key = std::make_pair (src, dst);
      
      // Skip if already added
      if (m_residualGraph.channels.find (key) != m_residualGraph.channels.end ())
        continue;
      
      ResidualChannelInfo channelInfo;
      channelInfo.src = src;
      channelInfo.dst = dst;
      
      // Check if this is a direct neighbor connection (we have actual channel object)
      bool isDirectNeighbor = false;
      std::string myAddress = m_networkLayer ? m_networkLayer->GetAddress () : "";
      
      if (src == myAddress)
      {
        auto neighborIt = m_neighbors.find (dst);
        if (neighborIt != m_neighbors.end () && neighborIt->second.channel)
        {
          isDirectNeighbor = true;
          channelInfo.availableEPRCapacity = m_resourceManager->GetAvailableEPRCapacity (neighborIt->second.channel);
          channelInfo.cost = m_metric ? m_metric->CalculateChannelCost (neighborIt->second.channel) : 1.0;
        }
      }
      
      if (!isDirectNeighbor)
      {
        // Use default/estimated values for non-direct connections
        channelInfo.availableEPRCapacity = 10;  // Default EPR capacity
        channelInfo.cost = 1.05;  // Default cost (slightly higher than direct)
      }
      
      m_residualGraph.channels[key] = channelInfo;
      
      NS_LOG_LOGIC ("Added channel " << src << " -> " << dst
                    << " with capacity=" << channelInfo.availableEPRCapacity
                    << ", cost=" << channelInfo.cost
                    << (isDirectNeighbor ? " (direct)" : " (global)"));
    }
  }
  else
  {
    // Fall back to direct neighbor channels only
    for (const auto& neighbor : m_neighbors)
    {
      if (m_networkLayer)
      {
        std::string myAddress = m_networkLayer->GetAddress ();
        std::pair<std::string, std::string> key = std::make_pair (myAddress, neighbor.first);
        
        // Check if both nodes are in the graph
        if (m_residualGraph.nodes.find (myAddress) != m_residualGraph.nodes.end () &&
            m_residualGraph.nodes.find (neighbor.first) != m_residualGraph.nodes.end ())
        {
          ResidualChannelInfo channelInfo;
          channelInfo.src = myAddress;
          channelInfo.dst = neighbor.first;
          channelInfo.availableEPRCapacity = m_resourceManager->GetAvailableEPRCapacity (neighbor.second.channel);
          channelInfo.cost = m_metric ? m_metric->CalculateChannelCost (neighbor.second.channel) : 1.0;
          
          m_residualGraph.channels[key] = channelInfo;
          
          NS_LOG_LOGIC ("Added channel " << myAddress << " -> " << neighbor.first
                        << " with capacity=" << channelInfo.availableEPRCapacity
                        << ", cost=" << channelInfo.cost);
        }
      }
    }
  }
  
  NS_LOG_LOGIC ("Residual network graph built with " 
                << m_residualGraph.nodes.size () << " nodes and "
                << m_residualGraph.channels.size () << " channels");
}

QuantumRoute
QCastRoutingProtocol::GreedyExtendedDijkstra (const std::string& src, 
                                              const std::string& dst,
                                              const QuantumRouteRequirements& requirements)
{
  NS_LOG_LOGIC ("Running G-EDA for " << src << " -> " << dst);
  
  if (!m_metric)
  {
    NS_LOG_WARN ("No metric set for G-EDA");
    return QuantumRoute ();
  }
  
  // Dijkstra algorithm data structures
  std::map<std::string, double> dist;
  std::map<std::string, std::string> prev;
  std::map<std::string, Ptr<QuantumChannel>> prevChannel;
  std::set<std::string> unvisited;
  
  // Initialize
  for (const auto& node : m_residualGraph.nodes)
  {
    dist[node.first] = std::numeric_limits<double>::max ();
    unvisited.insert (node.first);
  }
  dist[src] = 0.0;
  
  while (!unvisited.empty ())
  {
    // Find unvisited node with minimum distance
    std::string current;
    double minDist = std::numeric_limits<double>::max ();
    
    for (const auto& node : unvisited)
    {
      if (dist[node] < minDist)
      {
        minDist = dist[node];
        current = node;
      }
    }
    
    if (minDist == std::numeric_limits<double>::max ())
    {
      break;  // All reachable nodes have been visited
    }
    
    unvisited.erase (current);
    
    // If we reached destination, stop early
    if (current == dst)
    {
      break;
    }
    
    // Update neighbor distances
    auto nodeIt = m_residualGraph.nodes.find (current);
    if (nodeIt == m_residualGraph.nodes.end ())
      continue;
    
    for (const auto& neighborAddr : nodeIt->second.neighborAddresses)
    {
      // Check if neighbor is in unvisited set
      if (unvisited.find (neighborAddr) == unvisited.end ())
        continue;
      
      // Get channel information
      auto channelKey = std::make_pair (current, neighborAddr);
      auto channelIt = m_residualGraph.channels.find (channelKey);
      if (channelIt == m_residualGraph.channels.end ())
        continue;
      
      // Check if resources are sufficient
      ResidualChannelInfo channelInfo = channelIt->second;
      if (channelInfo.availableEPRCapacity < requirements.numQubits)
        continue;
      
      // Get neighbor node resources
      auto neighborNodeIt = m_residualGraph.nodes.find (neighborAddr);
      if (neighborNodeIt == m_residualGraph.nodes.end ())
        continue;
      
      if (neighborNodeIt->second.availableQubits < requirements.numQubits)
        continue;
      
      // Calculate new distance
      double alt = dist[current] + channelInfo.cost;
      
      if (alt < dist[neighborAddr])
      {
        dist[neighborAddr] = alt;
        prev[neighborAddr] = current;
        
        // Find actual quantum channel object if this is a direct neighbor
        for (const auto& neighbor : m_neighbors)
        {
          if (neighbor.first == neighborAddr)
          {
            prevChannel[neighborAddr] = neighbor.second.channel;
            break;
          }
        }
      }
    }
  }
  
  // Build path
  if (dist[dst] == std::numeric_limits<double>::max ())
  {
    NS_LOG_WARN ("No path found from " << src << " to " << dst);
    return QuantumRoute ();
  }
  
  QuantumRoute route;
  route.source = src;
  route.destination = dst;
  route.totalCost = dist[dst];
  
  // Backtrack to build node sequence
  std::vector<std::string> nodeSeq;
  std::string current = dst;
  
  while (current != src)
  {
    nodeSeq.insert (nodeSeq.begin (), current);
    
    auto prevIt = prev.find (current);
    if (prevIt == prev.end ())
      break;
    
    current = prevIt->second;
  }
  nodeSeq.insert (nodeSeq.begin (), src);
  route.nodeSequence = nodeSeq;
  
  NS_LOG_INFO ("G-EDA path: ");
  for (size_t i = 0; i < nodeSeq.size (); ++i)
  {
    NS_LOG_INFO ("  " << i << ": " << nodeSeq[i]);
  }
  
  // Build channel path for direct neighbors
  // For multi-hop routes, we only add channels we have direct access to
  for (size_t i = 0; i < nodeSeq.size () - 1; ++i)
  {
    std::string fromNode = nodeSeq[i];
    std::string toNode = nodeSeq[i + 1];
    
    // Check if we have a direct channel to this node
    auto channelIt = prevChannel.find (toNode);
    if (channelIt != prevChannel.end () && channelIt->second)
    {
      route.path.push_back (channelIt->second);
    }
  }
  
  // Calculate fidelity using actual channel fidelities from physics layer
  size_t hopCount = nodeSeq.size () - 1;
  route.estimatedFidelity = 1.0;
  route.estimatedDelay = Seconds (0.0);
  
  // Get QuantumPhyEntity from network layer if available
  Ptr<QuantumPhyEntity> qphyent = nullptr;
  if (m_networkLayer)
  {
    Ptr<QuantumNode> qnode = m_networkLayer->GetQuantumNode ();
    if (qnode)
    {
      qphyent = qnode->GetQuantumPhyEntity ();
    }
  }
  
  // Calculate path fidelity as product of channel fidelities
  for (size_t i = 0; i < nodeSeq.size () - 1; ++i)
  {
    std::string fromNode = nodeSeq[i];
    std::string toNode = nodeSeq[i + 1];
    
    double channelFidelity = 0.95;  // Default fidelity
    
    // Try to get actual fidelity from direct neighbor channel
    auto neighborIt = m_neighbors.find (toNode);
    if (neighborIt != m_neighbors.end () && neighborIt->second.channel && qphyent)
    {
      // Use actual channel fidelity from physics layer
      channelFidelity = neighborIt->second.channel->GetFidelity (qphyent);
      NS_LOG_LOGIC ("Using actual fidelity " << channelFidelity << " for channel " << fromNode << " -> " << toNode);
    }
    else
    {
      // Use estimated fidelity from global topology link metrics
      auto lsaIt = m_globalTopology.lsaDatabase.find (fromNode);
      if (lsaIt != m_globalTopology.lsaDatabase.end ())
      {
        const TopologyLSA& lsa = lsaIt->second;
        for (size_t j = 0; j < lsa.neighbors.size (); ++j)
        {
          if (lsa.neighbors[j] == toNode && j < lsa.linkMetrics.size ())
          {
            channelFidelity = lsa.linkMetrics[j];
            NS_LOG_LOGIC ("Using LSA fidelity " << channelFidelity << " for channel " << fromNode << " -> " << toNode);
            break;
          }
        }
      }
    }
    
    route.estimatedFidelity *= channelFidelity;
    route.estimatedDelay += Seconds (0.01);  // 10ms per hop
  }
  
  route.strategy = requirements.strategy;
  route.expirationTime = Simulator::Now () + requirements.duration;
  route.routeId = m_stats.routeRequests + 1;
  
  NS_LOG_INFO ("G-EDA found path with " << hopCount << " hops, cost=" << route.totalCost
               << ", fidelity=" << route.estimatedFidelity << " (calculated from channel fidelities)");
  
  return route;
}

std::vector<QuantumRoute>
QCastRoutingProtocol::FindRecoveryPaths (const QuantumRoute& mainRoute,
                                         const QuantumRouteRequirements& requirements)
{
  std::vector<QuantumRoute> recoveryPaths;
  
  if (mainRoute.path.empty ())
    return recoveryPaths;
  
  // Get sequence of nodes along main path
  std::vector<std::string> pathNodes;
  pathNodes.push_back (mainRoute.source);
  
  for (size_t i = 0; i < mainRoute.path.size (); ++i)
  {
    std::string src = mainRoute.path[i]->GetSrcOwner ();
    std::string dst = mainRoute.path[i]->GetDstOwner ();
    
    // Avoid duplicate nodes
    if (i == 0 || src != pathNodes.back ())
      pathNodes.push_back (src);
    pathNodes.push_back (dst);
  }
  
  // Find recovery paths for each node
  for (size_t i = 0; i < pathNodes.size (); ++i)
  {
    std::string currentNode = pathNodes[i];
    
    // Find nodes within k hops ahead (k=3)
    for (size_t j = i + 1; j < pathNodes.size () && (j - i) <= m_kHopDistance; ++j)
    {
      std::string targetNode = pathNodes[j];
      
      // Use G-EDA to find recovery path from current node to target node
      QuantumRoute recoveryRoute = GreedyExtendedDijkstra (currentNode, targetNode, requirements);
      
      if (recoveryRoute.IsValid ())
      {
        // Check for conflicts with main path
        bool hasConflict = false;
        for (const auto& channel : recoveryRoute.path)
        {
          for (const auto& mainChannel : mainRoute.path)
          {
            if (channel == mainChannel)
            {
              hasConflict = true;
              break;
            }
          }
          
          if (hasConflict)
            break;
        }
        
        if (!hasConflict)
        {
          recoveryPaths.push_back (recoveryRoute);
          NS_LOG_LOGIC ("Found recovery path from " << currentNode << " to " << targetNode 
                       << " with " << recoveryRoute.path.size () << " hops");
        }
      }
    }
  }
  
  return recoveryPaths;
}

void
QCastRoutingProtocol::UpdateLinkState ()
{
  NS_LOG_LOGIC ("Updating k-hop link state (k=" << m_kHopDistance << ")");
  
  m_linkState.linkQuality.clear ();
  
  // Update direct neighbor link states
  for (const auto& neighbor : m_neighbors)
  {
    std::string linkId = m_networkLayer->GetAddress () + "-" + neighbor.first;
    
    LinkQuality quality;
    quality.fidelity = 0.95;  // Should be obtained from actual channel
    quality.latency = Seconds (0.01);
    quality.errorRate = 0.01;
    quality.isEstablished = true;
    quality.timestamp = Simulator::Now ();
    
    m_linkState.linkQuality[linkId] = quality;
  }
  
  m_linkState.lastUpdate = Simulator::Now ();
  
  // Schedule next update
  Simulator::Schedule (m_linkStateExchangeInterval, &QCastRoutingProtocol::UpdateLinkState, this);
}

double
QCastRoutingProtocol::CalculateSuccessProbability (const QuantumRoute& mainRoute,
                                                   const std::vector<QuantumRoute>& recoveryPaths) const
{
  // Simplified calculation: based on path length and number of recovery paths
  double baseSuccess = 0.9;  // Base success probability
  
  // Longer paths have lower success probability
  double lengthPenalty = std::pow (0.95, mainRoute.GetHopCount ());
  
  // More recovery paths increase success probability
  double recoveryBonus = 1.0 - std::pow (0.8, recoveryPaths.size ());
  
  return baseSuccess * lengthPenalty * (1.0 + 0.1 * recoveryBonus);
}

void
QCastRoutingProtocol::BuildRecoveryRings (QCastRouteInfo& qcastInfo)
{
  // Simplified implementation: form rings from each recovery path and its connecting main path segment
  for (size_t i = 0; i < qcastInfo.recoveryPaths.size (); ++i)
  {
    const auto& recoveryPath = qcastInfo.recoveryPaths[i];
    
    // Find start and end nodes of recovery path on main path
    // Simplified: assume recovery path connects i-th and (i+2)-th nodes on main path
    if (i + 2 < qcastInfo.mainRoute.path.size ())
    {
      std::vector<QuantumRoute> ring;
      ring.push_back (recoveryPath);
      
      // Add main path segment
      QuantumRoute mainSegment;
      mainSegment.source = qcastInfo.mainRoute.path[i]->GetSrcOwner ();
      mainSegment.destination = qcastInfo.mainRoute.path[i+2]->GetDstOwner ();
      
      for (size_t j = i; j <= i + 2 && j < qcastInfo.mainRoute.path.size (); ++j)
      {
        mainSegment.path.push_back (qcastInfo.mainRoute.path[j]);
      }
      
      ring.push_back (mainSegment);
      qcastInfo.recoveryRings[i] = ring;
    }
  }
}

// ===========================================================================
// QuantumRoutingProtocol interface implementation
// ===========================================================================

void
QCastRoutingProtocol::DiscoverNeighbors ()
{
  NS_LOG_LOGIC ("QCastRoutingProtocol::DiscoverNeighbors (P1+P3 stages)");
  
  m_stats.routeRequests++;
  
  if (!m_networkLayer)
  {
    NS_LOG_WARN ("No network layer set for neighbor discovery");
    return;
  }
  
  // Get current neighbor channels
  std::vector<Ptr<QuantumChannel>> channels = m_networkLayer->GetNeighbors ();
  
  // Update neighbor information
  for (const auto& channel : channels)
  {
    std::string neighborAddr = (channel->GetSrcOwner () != m_networkLayer->GetAddress ()) ?
                              channel->GetSrcOwner () : channel->GetDstOwner ();
    
    NeighborInfo info;
    info.channel = channel;
    info.lastSeen = Simulator::Now ();
    info.cost = m_metric ? m_metric->CalculateChannelCost (channel) : 1.0;
    
    // Initialize link quality
    info.linkQuality.fidelity = 0.95;
    info.linkQuality.latency = Seconds (0.01);
    info.linkQuality.errorRate = 0.01;
    info.linkQuality.isEstablished = false;
    info.linkQuality.timestamp = Simulator::Now ();
    
    m_neighbors[neighborAddr] = info;
    
    NS_LOG_LOGIC ("Discovered neighbor: " << neighborAddr << ", cost=" << info.cost);
  }
  
  // Build residual network graph
  BuildResidualNetworkGraph ();
  
  // Start link state updates
  UpdateLinkState ();
  
  // Initiate topology discovery
  InitiateTopologyDiscovery ();
  
  // Notify route available
  if (m_routeAvailableCallback.IsNull () == false)
  {
    // Create dummy route for notification
    QuantumRoute dummyRoute;
    dummyRoute.source = m_networkLayer->GetAddress ();
    dummyRoute.destination = "broadcast";
    m_routeAvailableCallback (dummyRoute);
  }
}

void
QCastRoutingProtocol::UpdateRoutingTable ()
{
  NS_LOG_LOGIC ("QCastRoutingProtocol::UpdateRoutingTable");
  
  // Clean up expired routes
  auto it = m_routingTable.begin ();
  while (it != m_routingTable.end ())
  {
    if (it->IsExpired (m_routeExpirationTimeout))
    {
      NS_LOG_LOGIC ("Removing expired route to " << it->destination);
      it = m_routingTable.erase (it);
      
      if (m_routeExpiredCallback.IsNull () == false)
      {
        m_routeExpiredCallback (it->destination);
      }
    }
    else
    {
      ++it;
    }
  }
  
  // Update Q-CAST route information
  auto qcastIt = m_qcastRoutes.begin ();
  while (qcastIt != m_qcastRoutes.end ())
  {
    if (qcastIt->second.mainRoute.expirationTime < Simulator::Now ())
    {
      NS_LOG_LOGIC ("Removing expired Q-CAST route " << qcastIt->first);
      qcastIt = m_qcastRoutes.erase (qcastIt);
    }
    else
    {
      ++qcastIt;
    }
  }
  
  // Rebuild residual network graph
  BuildResidualNetworkGraph ();
}

QuantumRoute
QCastRoutingProtocol::RouteRequest (const std::string& src, 
                                    const std::string& dst,
                                    const QuantumRouteRequirements& requirements)
{
  NS_LOG_LOGIC ("QCastRoutingProtocol::RouteRequest (P2 stage) for " << src << " -> " << dst);
  
  m_stats.routeRequests++;
  
  if (!m_metric || !m_resourceManager)
  {
    NS_LOG_WARN ("Metric or resource manager not set");
    return QuantumRoute ();
  }
  
  // 1. Find main path using G-EDA
  QuantumRoute mainRoute = GreedyExtendedDijkstra (src, dst, requirements);
  
  if (!mainRoute.IsValid ())
  {
    NS_LOG_WARN ("No main route found");
    return QuantumRoute ();
  }
  
  // 2. Reserve resources
  if (!m_resourceManager->ReserveRouteResources (mainRoute, requirements))
  {
    NS_LOG_WARN ("Failed to reserve resources for main route");
    return QuantumRoute ();
  }
  
  // 3. Find recovery paths
  std::vector<QuantumRoute> recoveryPaths = FindRecoveryPaths (mainRoute, requirements);
  
  // 4. Reserve resources for recovery paths
  for (const auto& recoveryPath : recoveryPaths)
  {
    if (!m_resourceManager->ReserveRouteResources (recoveryPath, requirements))
    {
      NS_LOG_WARN ("Failed to reserve resources for recovery path");
      // Continue with other recovery paths
    }
  }
  
  // 5. Create Q-CAST route information
  QCastRouteInfo qcastInfo;
  qcastInfo.mainRoute = mainRoute;
  qcastInfo.recoveryPaths = recoveryPaths;
  qcastInfo.successProbability = CalculateSuccessProbability (mainRoute, recoveryPaths);
  
  // Build recovery rings
  BuildRecoveryRings (qcastInfo);
  
  // 6. Store route information
  uint32_t routeId = mainRoute.routeId;
  m_qcastRoutes[routeId] = qcastInfo;
  
  // 7. Add to routing table
  QuantumRouteEntry entry;
  entry.destination = dst;
  entry.nextHop = mainRoute.path.empty () ? "" : mainRoute.path[0]->GetDstOwner ();
  entry.channel = mainRoute.path.empty () ? nullptr : mainRoute.path[0];
  entry.cost = mainRoute.totalCost;
  entry.timestamp = Simulator::Now ();
  entry.sequenceNumber = routeId;
  
  m_routingTable.push_back (entry);
  
  NS_LOG_INFO ("Q-CAST route created: " << src << " -> " << dst 
               << ", main hops=" << mainRoute.GetHopCount ()
               << ", recovery paths=" << recoveryPaths.size ()
               << ", success probability=" << qcastInfo.successProbability);
  
  return mainRoute;
}

QuantumRoute
QCastRoutingProtocol::GetRoute (const std::string& dst) const
{
  // Find best route
  QuantumRoute bestRoute;
  double bestCost = std::numeric_limits<double>::max ();
  
  for (const auto& entry : m_routingTable)
  {
    if (entry.destination == dst && entry.cost < bestCost)
    {
      bestCost = entry.cost;
      
      // Find corresponding Q-CAST route information
      for (const auto& qcastRoute : m_qcastRoutes)
      {
        if (qcastRoute.second.mainRoute.destination == dst &&
            qcastRoute.second.mainRoute.routeId == entry.sequenceNumber)
        {
          bestRoute = qcastRoute.second.mainRoute;
          break;
        }
      }
    }
  }
  
  return bestRoute;
}

std::vector<QuantumRouteEntry>
QCastRoutingProtocol::GetAllRoutes () const
{
  return m_routingTable;
}

bool
QCastRoutingProtocol::AddRoute (const QuantumRouteEntry& entry)
{
  m_routingTable.push_back (entry);
  return true;
}

bool
QCastRoutingProtocol::RemoveRoute (const std::string& dst)
{
  bool removed = false;
  auto it = m_routingTable.begin ();
  
  while (it != m_routingTable.end ())
  {
    if (it->destination == dst)
    {
      it = m_routingTable.erase (it);
      removed = true;
    }
    else
    {
      ++it;
    }
  }
  
  return removed;
}

bool
QCastRoutingProtocol::HasRoute (const std::string& dst) const
{
  for (const auto& entry : m_routingTable)
  {
    if (entry.destination == dst)
      return true;
  }
  
  return false;
}

void
QCastRoutingProtocol::SetMetric (Ptr<QuantumMetric> metric)
{
  m_metric = metric;
}

Ptr<QuantumMetric>
QCastRoutingProtocol::GetMetric () const
{
  return m_metric;
}

void
QCastRoutingProtocol::SetResourceManager (Ptr<QuantumResourceManager> manager)
{
  m_resourceManager = manager;
}

Ptr<QuantumResourceManager>
QCastRoutingProtocol::GetResourceManager () const
{
  return m_resourceManager;
}

void
QCastRoutingProtocol::SetNetworkLayer (Ptr<QuantumNetworkLayer> layer)
{
  m_networkLayer = layer;
}

Ptr<QuantumNetworkLayer>
QCastRoutingProtocol::GetNetworkLayer () const
{
  return m_networkLayer;
}

void
QCastRoutingProtocol::ReceivePacket (Ptr<QuantumPacket> packet)
{
  NS_LOG_LOGIC ("QCastRoutingProtocol::ReceivePacket");
  
  if (!packet)
  {
    NS_LOG_WARN ("Received null packet");
    return;
  }
  
  m_stats.packetsReceived++;
  
  // Process link state update packets (Phase P3)
  if (packet->GetProtocol () == QuantumPacket::PROTO_QUANTUM_ROUTING)
  {
    NS_LOG_LOGIC ("Processing routing packet from " << packet->GetSourceAddress ());
    
    // Should parse link state information from packet
    // Simplified implementation: just record reception
  }
  
  // Process topology LSA packets
  if (packet->GetType () == QuantumPacket::TOPOLOGY_LSA)
  {
    NS_LOG_INFO ("QCastRoutingProtocol::ReceivePacket - Processing topology LSA packet from " << packet->GetSourceAddress () << 
                 " to " << packet->GetDestinationAddress () << 
                 " type=" << (int)packet->GetType () << 
                 " protocol=" << (int)packet->GetProtocol ());
    ProcessTopologyLSA (packet);
  }
}

void
QCastRoutingProtocol::InitiateTopologyDiscovery ()
{
  NS_LOG_LOGIC ("Initiating topology discovery");
  
  if (!m_networkLayer)
  {
    NS_LOG_WARN ("No network layer set for topology discovery");
    return;
  }
  
  // Generate initial LSA
  GenerateTopologyLSA ();
  
  // Schedule periodic LSA flooding
  // For now, just do one-time flooding
  NS_LOG_LOGIC ("Topology discovery initiated");
  
  // Schedule convergence check
  Simulator::Schedule (m_topologyDiscoveryInterval, 
                      &QCastRoutingProtocol::CheckTopologyConvergence, 
                      this);
}

void
QCastRoutingProtocol::SendPacket(Ptr<QuantumPacket> packet, const std::string& dst)
{
  m_stats.packetsSent++;
  
  // Set protocol type
  packet->SetProtocol (QuantumPacket::PROTO_QUANTUM_ROUTING);
  
  NS_LOG_INFO ("QCastRoutingProtocol::SendPacket - Sending packet to " << dst << 
               ", type=" << (int)packet->GetType () << 
               ", src=" << packet->GetSourceAddress () << 
               ", dst=" << packet->GetDestinationAddress ());
  
  // Send through network layer
  bool sent = m_networkLayer->SendPacket (packet);
  
  if (sent)
  {
    NS_LOG_INFO ("QCastRoutingProtocol::SendPacket - Packet sent successfully");
  }
  else
  {
    NS_LOG_WARN ("QCastRoutingProtocol::SendPacket - Failed to send packet");
  }
}

QuantumNetworkStats
QCastRoutingProtocol::GetStatistics () const
{
  return m_stats;
}

void
QCastRoutingProtocol::ResetStatistics ()
{
  m_stats = QuantumNetworkStats ();
}

void
QCastRoutingProtocol::SetRouteExpirationTimeout (Time timeout)
{
  m_routeExpirationTimeout = timeout;
}

Time
QCastRoutingProtocol::GetRouteExpirationTimeout () const
{
  return m_routeExpirationTimeout;
}

void
QCastRoutingProtocol::SetRouteAvailableCallback (RouteAvailableCallback cb)
{
  m_routeAvailableCallback = cb;
}

void
QCastRoutingProtocol::SetRouteExpiredCallback (RouteExpiredCallback cb)
{
  m_routeExpiredCallback = cb;
}

void
QCastRoutingProtocol::SetRouteUpdatedCallback (RouteUpdatedCallback cb)
{
  m_routeUpdatedCallback = cb;
}

// ===========================================================================
// Q-CAST specific methods
// ===========================================================================

void
QCastRoutingProtocol::SetKHopDistance (unsigned k)
{
  m_kHopDistance = k;
  NS_LOG_LOGIC ("Set k-hop distance to " << k);
}

unsigned
QCastRoutingProtocol::GetKHopDistance () const
{
  return m_kHopDistance;
}

QCastRouteInfo
QCastRoutingProtocol::GetQCastRouteInfo (uint32_t routeId) const
{
  auto it = m_qcastRoutes.find (routeId);
  if (it != m_qcastRoutes.end ())
  {
    return it->second;
  }
  
  return QCastRouteInfo ();
}

// ===========================================================================
// Topology discovery implementation
// ===========================================================================



void
QCastRoutingProtocol::GenerateTopologyLSA ()
{
  if (!m_networkLayer)
  {
    NS_LOG_WARN ("No network layer set for LSA generation");
    return;
  }
  
  std::string myAddress = m_networkLayer->GetAddress ();
  
  // Get QuantumPhyEntity for fidelity queries
  Ptr<QuantumPhyEntity> qphyent = nullptr;
  if (m_networkLayer)
  {
    Ptr<QuantumNode> qnode = m_networkLayer->GetQuantumNode ();
    if (qnode)
    {
      qphyent = qnode->GetQuantumPhyEntity ();
    }
  }
  
  // Create LSA
  TopologyLSA lsa;
  lsa.nodeId = myAddress;
  lsa.sequenceNumber = ++m_myLsaSequenceNumber;
  lsa.timestamp = Simulator::Now ();
  
  // Add neighbor information
  for (const auto& neighbor : m_neighbors)
  {
    lsa.neighbors.push_back (neighbor.first);
    
    // Get link metric (fidelity from channel or default)
    double linkFidelity = 0.95; // Default
    if (neighbor.second.channel && qphyent)
    {
      // Get actual fidelity from physics layer via channel
      linkFidelity = neighbor.second.channel->GetFidelity (qphyent);
      NS_LOG_LOGIC ("LSA: Using actual fidelity " << linkFidelity 
                    << " for link " << myAddress << " -> " << neighbor.first);
    }
    lsa.linkMetrics.push_back (linkFidelity);
  }
  
  NS_LOG_LOGIC ("Generated LSA for node " << myAddress 
                << " with " << lsa.neighbors.size () << " neighbors, seq=" << lsa.sequenceNumber);
  
  // Flood the LSA
  FloodTopologyLSA (lsa);
  
  // Store my own LSA in global topology
  m_globalTopology.UpdateFromLSA (lsa);
  m_receivedSequenceNumbers[myAddress] = lsa.sequenceNumber;
}

void
QCastRoutingProtocol::FloodTopologyLSA (const TopologyLSA& lsa)
{
  if (!m_networkLayer)
  {
    NS_LOG_WARN ("No network layer set for LSA flooding");
    return;
  }
  
  std::string myAddress = m_networkLayer->GetAddress ();
  NS_LOG_INFO ("Flooding LSA from " << lsa.nodeId << " (seq=" << lsa.sequenceNumber << ") to " << m_neighbors.size () << " neighbors");
  NS_LOG_INFO ("  LSA has " << lsa.neighbors.size () << " neighbors:");
  for (const auto& n : lsa.neighbors)
  {
    NS_LOG_INFO ("    - " << n);
  }
  
  // Create a packet for each neighbor
  for (const auto& neighbor : m_neighbors)
  {
    std::string neighborAddr = neighbor.first;
    const NeighborInfo& info = neighbor.second;
    
    // Check if we have a valid channel to this neighbor
    if (!info.channel)
    {
      NS_LOG_WARN ("No channel available for neighbor " << neighborAddr);
      continue;
    }
    
    // Create quantum packet with source = myAddress, destination = neighbor address
    Ptr<QuantumPacket> packet = CreateObject<QuantumPacket> (myAddress, neighborAddr);
    
    // Explicitly set addresses
    packet->SetSourceAddress (myAddress);
    packet->SetDestinationAddress (neighborAddr);
    
    // Set packet type and protocol
    packet->SetType (QuantumPacket::TOPOLOGY_LSA);
    packet->SetProtocol (QuantumPacket::PROTO_QUANTUM_ROUTING);
    
    // Set sequence number from LSA
    packet->SetSequenceNumber (lsa.sequenceNumber);
    
    // Store LSA data in qubit references:
    // - First entry: original LSA node ID (the node that generated this LSA)
    // - Remaining entries: neighbor list of the original node
    std::vector<std::string> lsaData;
    lsaData.push_back (lsa.nodeId);  // Original LSA source node
    for (const auto& n : lsa.neighbors)
    {
      lsaData.push_back (n);
    }
    packet->SetQubitReferences (lsaData);
    
    // Create a simple route with just this channel
    QuantumRoute route;
    route.source = myAddress;
    route.destination = neighborAddr;
    route.path.push_back (info.channel);
    route.totalCost = info.cost;
    route.estimatedFidelity = 0.95;
    route.estimatedDelay = MilliSeconds (10);
    route.strategy = QFS_ON_DEMAND;
    route.expirationTime = Simulator::Now () + Seconds (30);
    
    packet->SetRoute (route);
    
    NS_LOG_INFO ("Sending topology LSA to neighbor: " << neighborAddr << 
                 ", original LSA node: " << lsa.nodeId << 
                 ", seq=" << lsa.sequenceNumber);
    SendPacket (packet, neighborAddr);
  }
}

void
QCastRoutingProtocol::ProcessTopologyLSA (Ptr<QuantumPacket> packet)
{
  if (!packet)
  {
    NS_LOG_WARN ("Received null packet for topology LSA");
    return;
  }
  
  std::string senderAddr = packet->GetSourceAddress ();  // Who sent this packet
  std::string myAddress = m_networkLayer ? m_networkLayer->GetAddress () : "";
  
  // Extract LSA data from qubit references
  // Format: [originalNodeId, neighbor1, neighbor2, ...]
  std::vector<std::string> lsaData = packet->GetQubitReferences ();
  
  std::string originalLsaNode;
  std::vector<std::string> lsaNeighbors;
  
  if (!lsaData.empty ())
  {
    originalLsaNode = lsaData[0];
    for (size_t i = 1; i < lsaData.size (); ++i)
    {
      lsaNeighbors.push_back (lsaData[i]);
    }
  }
  else
  {
    // Fallback: use sender as the LSA source (old behavior)
    originalLsaNode = senderAddr;
    NS_LOG_WARN ("No LSA data in packet, using sender as LSA source");
  }
  
  NS_LOG_INFO ("Processing topology LSA: sender=" << senderAddr << 
               ", originalNode=" << originalLsaNode << 
               ", neighbors=" << lsaNeighbors.size () << 
               ", seq=" << packet->GetSequenceNumber ());
  
  // Get sequence number from packet
  uint32_t packetSeq = packet->GetSequenceNumber ();
  
  // Check if we have newer sequence number for the ORIGINAL LSA node
  auto seqIt = m_receivedSequenceNumbers.find (originalLsaNode);
  uint32_t currentSeq = (seqIt != m_receivedSequenceNumbers.end ()) ? seqIt->second : 0;
  
  // If this LSA is not newer than what we already have, ignore it
  if (packetSeq <= currentSeq)
  {
    NS_LOG_LOGIC ("Ignoring old LSA from " << originalLsaNode << " seq=" << packetSeq << " (current seq=" << currentSeq << ")");
    return;
  }
  
  // Create LSA from packet data
  TopologyLSA lsa;
  lsa.nodeId = originalLsaNode;
  lsa.sequenceNumber = packetSeq;
  lsa.timestamp = Simulator::Now ();
  lsa.neighbors = lsaNeighbors;
  lsa.linkMetrics = std::vector<double> (lsaNeighbors.size (), 0.95);
  
  NS_LOG_INFO ("LSA from " << originalLsaNode << " has neighbors:");
  for (const auto& n : lsa.neighbors)
  {
    NS_LOG_INFO ("  - " << n);
  }
  
  // Update global topology
  m_globalTopology.UpdateFromLSA (lsa);
  m_receivedSequenceNumbers[originalLsaNode] = packetSeq;
  
  NS_LOG_INFO ("Updated topology with LSA from " << originalLsaNode 
                << " (seq=" << packetSeq << "), total nodes: " << m_globalTopology.GetNodeCount ());
  
  // Check if topology has converged
  CheckTopologyConvergence ();
  
  // Re-flood the LSA to other neighbors (except the one we received it from)
  // Note: we keep the original LSA data intact
  NS_LOG_INFO ("Re-flooding LSA from " << originalLsaNode << " to other neighbors (except " << senderAddr << ")");
  
  for (const auto& neighbor : m_neighbors)
  {
    std::string neighborAddr = neighbor.first;
    
    // Don't send back to the node we received from
    if (neighborAddr == senderAddr)
      continue;
    
    // Don't send to the original LSA node
    if (neighborAddr == originalLsaNode)
      continue;
    
    // Create new packet with the same LSA data
    Ptr<QuantumPacket> floodPacket = CreateObject<QuantumPacket> (myAddress, neighborAddr);
    floodPacket->SetSourceAddress (myAddress);
    floodPacket->SetDestinationAddress (neighborAddr);
    floodPacket->SetType (QuantumPacket::TOPOLOGY_LSA);
    floodPacket->SetProtocol (QuantumPacket::PROTO_QUANTUM_ROUTING);
    floodPacket->SetSequenceNumber (packetSeq);
    
    // Keep the original LSA data (originalNodeId + neighbors)
    floodPacket->SetQubitReferences (lsaData);
    
    // Create a simple route
    QuantumRoute route;
    route.source = myAddress;
    route.destination = neighborAddr;
    route.path.push_back (neighbor.second.channel);
    route.totalCost = neighbor.second.cost;
    route.estimatedFidelity = 0.95;
    route.estimatedDelay = MilliSeconds (10);
    route.strategy = QFS_ON_DEMAND;
    route.expirationTime = Simulator::Now () + Seconds (30);
    
    floodPacket->SetRoute (route);
    
    // Send the packet
    NS_LOG_INFO ("Forwarding LSA (from " << originalLsaNode << ") to neighbor: " << neighborAddr);
    SendPacket (floodPacket, neighborAddr);
  }
}

void
QCastRoutingProtocol::CheckTopologyConvergence ()
{
  // Check if topology has converged
  bool wasConverged = m_topologyConverged;
  m_topologyConverged = m_globalTopology.IsComplete ();
  
  if (m_topologyConverged && !wasConverged)
  {
    NS_LOG_INFO ("Topology converged with " << m_globalTopology.GetNodeCount () << " nodes");
    HandleTopologyConverged ();
  }
  else if (!m_topologyConverged)
  {
    NS_LOG_LOGIC ("Topology not yet converged, waiting...");
    
    // Schedule another check
    Simulator::Schedule (m_topologyDiscoveryInterval, 
                        &QCastRoutingProtocol::CheckTopologyConvergence, this);
    
    // Generate another LSA to help convergence
    GenerateTopologyLSA ();
  }
}

void
QCastRoutingProtocol::HandleTopologyConverged ()
{
  NS_LOG_LOGIC ("Handling topology convergence");
  
  // Rebuild residual network graph with global topology
  BuildResidualNetworkGraph ();
  
  // Now we can perform routing with full topology knowledge
  // This enables multi-hop path finding
}

void
QCastRoutingProtocol::SimulateTopologyExchange (const std::vector<Ptr<QuantumNetworkLayer>>& allLayers)
{
  NS_LOG_INFO ("Simulating topology exchange between " << allLayers.size () << " nodes");
  
  // This is a simplified simulation approach
  // In a real implementation, this would use actual packet transmission
  
  // Collect all LSAs from all nodes
  std::vector<TopologyLSA> allLsas;
  
  for (const auto& layer : allLayers)
  {
    if (!layer)
      continue;
      
    std::string nodeId = layer->GetAddress ();
    
    // Generate LSA for this node based on its neighbors
    TopologyLSA lsa;
    lsa.nodeId = nodeId;
    lsa.sequenceNumber = 1;
    lsa.timestamp = Simulator::Now ();
    
    // Get neighbors from the network layer
    std::vector<Ptr<QuantumChannel>> channels = layer->GetNeighbors ();
    for (const auto& channel : channels)
    {
      std::string neighborId = (channel->GetSrcOwner () != nodeId) ? 
                               channel->GetSrcOwner () : channel->GetDstOwner ();
      lsa.neighbors.push_back (neighborId);
      lsa.linkMetrics.push_back (0.95); // Default link quality
    }
    
    allLsas.push_back (lsa);
    NS_LOG_INFO ("Generated LSA for node " << nodeId << " with " << lsa.neighbors.size () << " neighbors");
  }
  
  // Now distribute all LSAs to all nodes
  for (const auto& layer : allLayers)
  {
    if (!layer)
      continue;
      
    // Get the routing protocol for this layer
    Ptr<QuantumRoutingProtocol> routingProto = layer->GetRoutingProtocol ();
    if (!routingProto)
      continue;
      
    // Try to cast to QCastRoutingProtocol
    Ptr<QCastRoutingProtocol> qcastProto = DynamicCast<QCastRoutingProtocol> (routingProto);
    if (!qcastProto)
      continue;
      
    // Process all LSAs
    for (const auto& lsa : allLsas)
    {
      // Skip our own LSA (we already have it)
      if (lsa.nodeId == layer->GetAddress ())
        continue;
        
      // Update topology with this LSA
      qcastProto->m_globalTopology.UpdateFromLSA (lsa);
      qcastProto->m_receivedSequenceNumbers[lsa.nodeId] = lsa.sequenceNumber;
      
      NS_LOG_INFO ("Node " << layer->GetAddress () << " received LSA from " << lsa.nodeId);
    }
    
      // Check if topology has converged
      qcastProto->CheckTopologyConvergence ();
  }
}

void
QCastRoutingProtocol::PrintRoutingTable () const
{
  NS_LOG_INFO ("");
  NS_LOG_INFO ("=== Routing Table for Node " << (m_networkLayer ? m_networkLayer->GetAddress () : "unknown") << " ===");
  
  if (m_routingTable.empty ())
  {
    NS_LOG_INFO ("  No routes in routing table");
  }
  else
  {
    for (size_t i = 0; i < m_routingTable.size (); ++i)
    {
      const QuantumRouteEntry& entry = m_routingTable[i];
      NS_LOG_INFO ("  " << i << ". Destination: " << entry.destination);
      NS_LOG_INFO ("     Next hop: " << entry.nextHop);
      NS_LOG_INFO ("     Cost: " << entry.cost);
      NS_LOG_INFO ("     Timestamp: " << entry.timestamp.GetSeconds () << "s");
      NS_LOG_INFO ("     Sequence: " << entry.sequenceNumber);
      if (entry.channel)
      {
        NS_LOG_INFO ("     Channel: " << entry.channel->GetSrcOwner () << " -> " << entry.channel->GetDstOwner ());
      }
    }
  }
  
  NS_LOG_INFO ("=== End Routing Table ===");
  NS_LOG_INFO ("");
}

void
QCastRoutingProtocol::PrintGlobalTopology () const
{
  NS_LOG_INFO ("");
  NS_LOG_INFO ("=== Global Topology for Node " << (m_networkLayer ? m_networkLayer->GetAddress () : "unknown") << " ===");
  
  NS_LOG_INFO ("  Total nodes: " << m_globalTopology.GetNodeCount ());
  NS_LOG_INFO ("  Topology converged: " << (m_topologyConverged ? "Yes" : "No"));
  
  if (!m_globalTopology.allNodes.empty ())
  {
    NS_LOG_INFO ("  Nodes in topology:");
    for (const auto& node : m_globalTopology.allNodes)
    {
      NS_LOG_INFO ("    - " << node);
    }
    
    NS_LOG_INFO ("  Connections:");
    for (const auto& conn : m_globalTopology.connections)
    {
      if (conn.second)  // Connection exists
      {
        NS_LOG_INFO ("    " << conn.first.first << " <-> " << conn.first.second);
      }
    }
  }
  
  NS_LOG_INFO ("=== End Global Topology ===");
  NS_LOG_INFO ("");
}
} // namespace ns3
