/*
 * Q-CAST route types implementation
 *
 * This file implements the data structures used by the Q-CAST protocol.
 */

#include "qcast-route-types.h"
#include "ns3/simulator.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-route-types.h"

namespace ns3 {



// KHopLinkState implementation
KHopLinkState::KHopLinkState()
  : lastUpdate(Time(0))
{
}

bool KHopLinkState::IsExpired(Time expirationTime) const
{
  return (Simulator::Now() - lastUpdate) > expirationTime;
}

// QCastRouteInfo implementation
QCastRouteInfo::QCastRouteInfo()
  : successProbability(0.0),
    creationTime(Time(0))
{
}

bool QCastRouteInfo::IsValid() const
{
  return mainRoute.IsValid() && !recoveryPaths.empty();
}

// ResidualNodeInfo implementation
ResidualNodeInfo::ResidualNodeInfo()
  : availableQubits(0)
{
}

// ResidualChannelInfo implementation
ResidualChannelInfo::ResidualChannelInfo()
  : availableEPRCapacity(0),
    cost(0.0)
{
}

// ResidualNetworkGraph implementation
bool ResidualNetworkGraph::IsPathConflictFree(const QuantumRoute& route,
                                             const QuantumRouteRequirements& requirements) const
{
  // Check node resources
  for (const auto& channel : route.path)
  {
    std::string src = channel->GetSrcOwner();
    std::string dst = channel->GetDstOwner();
    
    auto srcIt = nodes.find(src);
    auto dstIt = nodes.find(dst);
    
    if (srcIt == nodes.end() || dstIt == nodes.end())
      return false;
    
    if (srcIt->second.availableQubits < requirements.numQubits ||
        dstIt->second.availableQubits < requirements.numQubits)
      return false;
  }
  
  // Check channel resources
  for (const auto& channel : route.path)
  {
    std::string src = channel->GetSrcOwner();
    std::string dst = channel->GetDstOwner();
    auto key = std::make_pair(src, dst);
    
    auto channelIt = channels.find(key);
    if (channelIt == channels.end())
      return false;
    
    if (channelIt->second.availableEPRCapacity < requirements.numQubits)
      return false;
  }
  
  return true;
}

// TopologyLSA implementation
TopologyLSA::TopologyLSA()
  : nodeId(""),
    sequenceNumber(0),
    timestamp(Time(0))
{
}

bool TopologyLSA::IsNewerThan(const TopologyLSA& other) const
{
  if (nodeId != other.nodeId)
    return false;  // Different nodes, not comparable
  
  // Higher sequence number means newer
  return sequenceNumber > other.sequenceNumber;
}

std::string TopologyLSA::GetLSAId() const
{
  return nodeId + ":" + std::to_string(sequenceNumber);
}

// GlobalTopology implementation
GlobalTopology::GlobalTopology()
  : lastUpdate(Time(0))
{
}

void GlobalTopology::UpdateFromLSA(const TopologyLSA& lsa)
{
  // Update LSA database
  lsaDatabase[lsa.nodeId] = lsa;
  
  // Add node to allNodes set
  allNodes.insert(lsa.nodeId);
  
  // Update connections from neighbor list
  for (const auto& neighbor : lsa.neighbors)
  {
    // Add neighbor to allNodes set
    allNodes.insert(neighbor);
    
    // Create bidirectional connection
    connections[{lsa.nodeId, neighbor}] = true;
    connections[{neighbor, lsa.nodeId}] = true;
  }
  
  lastUpdate = Simulator::Now();
}

void GlobalTopology::RebuildTopologyGraph()
{
  // Clear existing connections
  connections.clear();
  
  // Rebuild from LSA database
  for (const auto& lsaPair : lsaDatabase)
  {
    const TopologyLSA& lsa = lsaPair.second;
    
    // Add node to allNodes set
    allNodes.insert(lsa.nodeId);
    
    // Add connections for each neighbor
    for (const auto& neighbor : lsa.neighbors)
    {
      allNodes.insert(neighbor);
      connections[{lsa.nodeId, neighbor}] = true;
      connections[{neighbor, lsa.nodeId}] = true;
    }
  }
}

bool GlobalTopology::IsComplete() const
{
  // If we know about no nodes, topology is not complete
  if (allNodes.empty())
    return false;
    
  // Check if we have LSAs from all nodes we know about
  for (const auto& node : allNodes)
  {
    if (lsaDatabase.find(node) == lsaDatabase.end())
      return false;
  }
  return true;
}

std::vector<std::string> GlobalTopology::GetNeighbors(const std::string& nodeId) const
{
  std::vector<std::string> neighborList;
  
  for (const auto& node : allNodes)
  {
    if (node != nodeId)
    {
      auto key = std::make_pair(nodeId, node);
      if (connections.find(key) != connections.end() && connections.at(key))
      {
        neighborList.push_back(node);
      }
    }
  }
  
  return neighborList;
}

} // namespace ns3