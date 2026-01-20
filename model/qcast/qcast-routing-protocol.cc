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
#include "../quantum-channel.h"
#include "../quantum-packet.h"
#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QCastRoutingProtocol");

// ===========================================================================
// QCastRoutingProtocol implementation
// ===========================================================================

QCastRoutingProtocol::QCastRoutingProtocol ()
  : m_kHopDistance (3),  // k=3
    m_routeExpirationTimeout (Seconds (30.0)),
    m_linkStateExchangeInterval (Seconds (1.0))  // Exchange link state every second
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
  
  // Build node information
  for (const auto& neighbor : m_neighbors)
  {
    ResidualNodeInfo nodeInfo;
    nodeInfo.address = neighbor.first;
    nodeInfo.availableQubits = m_resourceManager->GetAvailableQubits (neighbor.first);
    
    // Add neighbor addresses
    for (const auto& otherNeighbor : m_neighbors)
    {
      if (otherNeighbor.first != neighbor.first)
      {
        nodeInfo.neighborAddresses.push_back (otherNeighbor.first);
      }
    }
    
    m_residualGraph.nodes[neighbor.first] = nodeInfo;
  }
  
  // Add current node
  if (m_networkLayer)
  {
    std::string myAddress = m_networkLayer->GetAddress ();
    ResidualNodeInfo myNodeInfo;
    myNodeInfo.address = myAddress;
    myNodeInfo.availableQubits = m_resourceManager->GetAvailableQubits (myAddress);
    
    for (const auto& neighbor : m_neighbors)
    {
      myNodeInfo.neighborAddresses.push_back (neighbor.first);
    }
    
    m_residualGraph.nodes[myAddress] = myNodeInfo;
  }
  
  // Build channel information
  for (const auto& neighbor : m_neighbors)
  {
    std::string myAddress = m_networkLayer ? m_networkLayer->GetAddress () : "";
    std::pair<std::string, std::string> key = std::make_pair (myAddress, neighbor.first);
    
    ResidualChannelInfo channelInfo;
    channelInfo.src = myAddress;
    channelInfo.dst = neighbor.first;
    channelInfo.availableEPRCapacity = m_resourceManager->GetAvailableEPRCapacity (neighbor.second.channel);
    channelInfo.cost = m_metric ? m_metric->CalculateChannelCost (neighbor.second.channel) : 1.0;
    
    m_residualGraph.channels[key] = channelInfo;
  }
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
        
        // Find actual quantum channel object
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
  
  // Backtrack to build path
  std::string current = dst;
  while (current != src)
  {
    auto channelIt = prevChannel.find (current);
    if (channelIt != prevChannel.end ())
    {
      route.path.insert (route.path.begin (), channelIt->second);
    }
    
    auto prevIt = prev.find (current);
    if (prevIt == prev.end ())
      break;
    
    current = prevIt->second;
  }
  
  // Estimate fidelity and delay
  route.estimatedFidelity = 1.0;
  route.estimatedDelay = Seconds (0.0);
  for (const auto& channel : route.path)
  {
    // Simplified estimation: path fidelity = product(channel fidelity)
    route.estimatedFidelity *= 0.95;  // Assume each channel has fidelity 0.95
    route.estimatedDelay += Seconds (0.01);  // Assume each channel has 10ms delay
  }
  
  route.strategy = requirements.strategy;
  route.expirationTime = Simulator::Now () + requirements.duration;
  route.routeId = m_stats.routeRequests + 1;
  
  NS_LOG_LOGIC ("G-EDA found path with " << route.path.size () << " hops, cost=" << route.totalCost);
  
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
}

void
QCastRoutingProtocol::SendPacket (Ptr<QuantumPacket> packet, const std::string& dst)
{
  NS_LOG_LOGIC ("QCastRoutingProtocol::SendPacket");
  
  if (!packet || !m_networkLayer)
  {
    NS_LOG_WARN ("Cannot send packet: null packet or no network layer");
    return;
  }
  
  m_stats.packetsSent++;
  
  // Set protocol type
  packet->SetProtocol (QuantumPacket::PROTO_QUANTUM_ROUTING);
  
  // Send through network layer
  m_networkLayer->SendPacket (packet);
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

} // namespace ns3