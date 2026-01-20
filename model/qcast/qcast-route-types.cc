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
  : lastUpdate(Simulator::Now())
{
}

bool KHopLinkState::IsExpired(Time expirationTime) const
{
  return (Simulator::Now() - lastUpdate) > expirationTime;
}

// QCastRouteInfo implementation
QCastRouteInfo::QCastRouteInfo()
  : successProbability(0.0),
    creationTime(Simulator::Now())
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

} // namespace ns3