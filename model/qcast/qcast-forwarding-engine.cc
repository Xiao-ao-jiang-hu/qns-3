/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Q-CAST forwarding engine implementation
 *
 * This file implements the Q-CAST forwarding engine for quantum networks.
 * The engine provides XOR recovery decision and log-time entanglement
 * swap scheduling as described in the Q-CAST protocol.
 */

#include "qcast-forwarding-engine.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-packet.h"
#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QCastForwardingEngine");

// ===========================================================================
// QCastForwardingEngine implementation
// ===========================================================================

// Static member initialization
std::map<std::string, Ptr<QuantumNetworkLayer>> QCastForwardingEngine::s_networkLayerRegistry;

QCastForwardingEngine::QCastForwardingEngine ()
  : m_strategy (QFS_ON_DEMAND),
    m_nextRouteId (1)
{
  NS_LOG_LOGIC ("Creating QCastForwardingEngine");
  ResetStatistics ();
}

QCastForwardingEngine::~QCastForwardingEngine ()
{
  NS_LOG_LOGIC ("Destroying QCastForwardingEngine");
}

TypeId
QCastForwardingEngine::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::QCastForwardingEngine")
    .SetParent<QuantumForwardingEngine> ()
    .AddConstructor<QCastForwardingEngine> ();
  return tid;
}

// ===========================================================================
// Private helper methods
// ===========================================================================

bool
QCastForwardingEngine::XORRecoveryDecision (const QuantumRoute& mainRoute,
                                           const std::vector<QuantumRoute>& recoveryPaths,
                                           const std::map<uint32_t, std::vector<QuantumRoute>>& recoveryRings)
{
  NS_LOG_LOGIC ("Making XOR recovery decision for route " << mainRoute.routeId);
  
  // Simplified implementation: use any available recovery ring
  if (!recoveryRings.empty ())
  {
    NS_LOG_LOGIC ("Using recovery rings for XOR recovery");
    return true;
  }
  
  // If no recovery rings, check for available recovery paths
  if (!recoveryPaths.empty ())
  {
    NS_LOG_LOGIC ("Using recovery paths for backup");
    return true;
  }
  
  NS_LOG_WARN ("No recovery options available");
  return false;
}

void
QCastForwardingEngine::LogTimeSwapScheduling (const QuantumRoute& route)
{
  NS_LOG_LOGIC ("Performing log-time swap scheduling for " << route.GetHopCount () << "-hop route");
  
  // Organize path as binary tree
  std::vector<Ptr<QuantumChannel>> channels = route.path;
  
  if (channels.empty ())
    return;
  
  // Calculate tree height
  size_t hopCount = channels.size ();
  size_t treeHeight = static_cast<size_t>(std::ceil(std::log2(hopCount + 1)));
  
  NS_LOG_LOGIC ("Route with " << hopCount << " hops organized into tree of height " << treeHeight);
  
  // Leaf nodes: directly adjacent entanglement pairs
  for (size_t i = 0; i < hopCount; ++i)
  {
    NS_LOG_LOGIC ("Leaf swap at channel " << i << ": " 
                 << channels[i]->GetSrcOwner () << " -> " << channels[i]->GetDstOwner ());
    
    // Actual entanglement swap should be performed here
    m_stats.entanglementSwaps++;
  }
  
  // Internal nodes: wait for child nodes to complete
  for (size_t level = 1; level < treeHeight; ++level)
  {
    size_t swapsAtLevel = hopCount / (1 << level);
    NS_LOG_LOGIC ("Level " << level << " swaps: " << swapsAtLevel);
    
    // Simulate swap delay
    Simulator::Schedule (Seconds (level * 0.01), [this]() {
      m_stats.entanglementSwaps++;
    });
  }
  
  NS_LOG_LOGIC ("Log-time scheduling completed in " << treeHeight << " levels "
               << "(vs " << hopCount << " levels in linear scheduling)");
}

// ===========================================================================
// QuantumForwardingEngine interface implementation
// ===========================================================================

bool
QCastForwardingEngine::ForwardPacket (Ptr<QuantumPacket> packet)
{
  NS_LOG_INFO ("[FORWARD] QCastForwardingEngine::ForwardPacket");
  
  if (!packet)
  {
    NS_LOG_WARN ("Attempt to forward null packet");
    return false;
  }
  
  NS_LOG_INFO ("[FORWARD] Packet source: " << packet->GetSourceAddress() << " dest: " << packet->GetDestinationAddress());
  NS_LOG_INFO ("[FORWARD] Packet type: " << (int)packet->GetType() << " protocol: " << (int)packet->GetProtocol());
  
  m_stats.packetsForwarded++;
  
  // Special handling for routing protocol packets - deliver directly via registry
  if (packet->GetProtocol () == QuantumPacket::PROTO_QUANTUM_ROUTING)
  {
    NS_LOG_INFO ("[FORWARD] Routing protocol packet detected, using registry delivery");
    NS_LOG_INFO ("  Packet type: " << (int)packet->GetType ());
    NS_LOG_INFO ("  Source: " << packet->GetSourceAddress ());
    NS_LOG_INFO ("  Destination: " << packet->GetDestinationAddress ());
    
    // Use network layer registry to deliver packet to destination
    std::string dstAddr = packet->GetDestinationAddress ();
    NS_LOG_INFO ("[FORWARD] Routing packet destination address: " << dstAddr);
    Ptr<QuantumNetworkLayer> dstNetworkLayer = GetNetworkLayer (dstAddr);
    
    if (dstNetworkLayer)
    {
      NS_LOG_INFO ("[FORWARD] Found network layer for destination: " << dstAddr);
      NS_LOG_INFO ("[FORWARD] Delivering packet to destination network layer");
      
      // Deliver packet to destination's network layer
      // This will trigger ProcessPacket -> ReceivePacket on the routing protocol
      dstNetworkLayer->ReceivePacket (packet);
      NS_LOG_INFO ("[FORWARD] ReceivePacket called successfully");
      
      NS_LOG_INFO ("[FORWARD] Packet delivered successfully to " << dstAddr);
      return true;
    }
    else
    {
      NS_LOG_WARN ("[FORWARD] No network layer found for destination: " << dstAddr);
      NS_LOG_WARN ("[FORWARD] Packet delivery failed");
      return false;
    }
  }
  
  // Check if packet has valid route
  if (!packet->GetRoute ().IsValid ())
  {
    // For routing protocol packets (like topology LSA), allow forwarding even without route
    if (packet->GetProtocol () == QuantumPacket::PROTO_QUANTUM_ROUTING)
    {
      NS_LOG_INFO ("QCastForwardingEngine::ForwardPacket - Routing protocol packet without route");
      NS_LOG_INFO ("  Packet type: " << (int)packet->GetType ());
      NS_LOG_INFO ("  Source: " << packet->GetSourceAddress ());
      NS_LOG_INFO ("  Destination: " << packet->GetDestinationAddress ());
      
      // Use network layer registry to deliver packet to destination
      std::string dstAddr = packet->GetDestinationAddress ();
      NS_LOG_INFO ("[FORWARD] Routing packet destination address: " << dstAddr);
      Ptr<QuantumNetworkLayer> dstNetworkLayer = GetNetworkLayer (dstAddr);
      
      if (dstNetworkLayer)
      {
        NS_LOG_INFO ("[FORWARD] Found network layer for destination: " << dstAddr);
        NS_LOG_INFO ("[FORWARD] Delivering packet to destination network layer");
        
        // Deliver packet to destination's network layer
        // This will trigger ProcessPacket -> ReceivePacket on the routing protocol
        dstNetworkLayer->ReceivePacket (packet);
        NS_LOG_INFO ("[FORWARD] ReceivePacket called successfully");
        
        NS_LOG_INFO ("[FORWARD] Packet delivered successfully to " << dstAddr);
        return true;
      }
      else
      {
        NS_LOG_WARN ("[FORWARD] No network layer found for destination: " << dstAddr);
        NS_LOG_WARN ("[FORWARD] Packet delivery failed");
        return false;
      }
    }
    else
    {
      NS_LOG_WARN ("Packet has no valid route");
      return false;
    }
  }
  
  const QuantumRoute& route = packet->GetRoute ();
  
  // Handle based on strategy
  if (m_strategy == QFS_PRE_ESTABLISHED)
  {
    if (!IsRouteReady (route))
    {
      NS_LOG_WARN ("Route not ready for pre-established forwarding");
      return false;
    }
  }
  else if (m_strategy == QFS_ON_DEMAND)
  {
    if (!EstablishEntanglement (route))
    {
      NS_LOG_WARN ("Failed to establish entanglement for on-demand forwarding");
      return false;
    }
  }
  
  // Perform log-time swap scheduling
  LogTimeSwapScheduling (route);
  
  NS_LOG_LOGIC ("Packet forwarded successfully along route: " << route.ToString ());
  return true;
}

bool
QCastForwardingEngine::ForwardQubits (const std::vector<std::string>& qubits,
                                     const std::string& nextHop)
{
  NS_LOG_LOGIC ("QCastForwardingEngine::ForwardQubits");
  
  if (qubits.empty () || nextHop.empty ())
  {
    NS_LOG_WARN ("Invalid qubits or next hop");
    return false;
  }
  
  NS_LOG_LOGIC ("Forwarding " << qubits.size () << " qubits to " << nextHop);
  return true;
}

bool
QCastForwardingEngine::EstablishEntanglement (const QuantumRoute& route)
{
  NS_LOG_LOGIC ("QCastForwardingEngine::EstablishEntanglement (P4 stage)");
  
  if (!route.IsValid ())
  {
    NS_LOG_WARN ("Invalid route for entanglement establishment");
    return false;
  }
  
  // Check resources
  if (m_resourceManager)
  {
    // Simplified check: assume route requirements
    QuantumRouteRequirements requirements;
    requirements.numQubits = 1;
    requirements.duration = Seconds (10.0);
    
    if (!m_resourceManager->CheckRouteResources (route, requirements))
    {
      NS_LOG_WARN ("Insufficient resources for entanglement establishment");
      return false;
    }
  }
  
  // Distribute EPR pairs along the path
  for (const auto& channel : route.path)
  {
    // Create virtual EPR pair names
    std::string qubit1 = channel->GetSrcOwner () + "-epr-1";
    std::string qubit2 = channel->GetDstOwner () + "-epr-2";
    std::pair<std::string, std::string> epr = std::make_pair (qubit1, qubit2);
    
    if (!DistributeEPR (channel, epr))
    {
      NS_LOG_WARN ("Failed to distribute EPR pair on channel "
                   << channel->GetSrcOwner () << " -> " << channel->GetDstOwner ());
      return false;
    }
  }
  
  NS_LOG_LOGIC ("Entanglement established along route with " << route.GetHopCount () << " hops");
  m_stats.eprPairsDistributed += route.GetHopCount ();
  
  return true;
}

bool
QCastForwardingEngine::PerformEntanglementSwap (const std::vector<std::string>& qubits)
{
  NS_LOG_LOGIC ("QCastForwardingEngine::PerformEntanglementSwap");
  
  if (qubits.size () < 2)
  {
    NS_LOG_WARN ("Need at least 2 qubits for entanglement swap");
    return false;
  }
  
  NS_LOG_LOGIC ("Performing entanglement swap with qubits: ");
  for (const auto& qubit : qubits)
  {
    NS_LOG_LOGIC ("  " << qubit);
  }
  
  m_stats.entanglementSwaps++;
  return true;
}

bool
QCastForwardingEngine::DistributeEPR (Ptr<QuantumChannel> channel,
                                     const std::pair<std::string, std::string>& epr)
{
  NS_LOG_LOGIC ("QCastForwardingEngine::DistributeEPR");
  
  if (!channel)
  {
    NS_LOG_WARN ("Null channel for EPR distribution");
    return false;
  }
  
  NS_LOG_LOGIC ("Distributing EPR pair " << epr.first << " - " << epr.second
               << " on channel " << channel->GetSrcOwner () << " -> " << channel->GetDstOwner ());
  
  // Should call actual EPR distribution protocol
  // Simplified implementation: return success
  return true;
}

bool
QCastForwardingEngine::IsRouteReady (const QuantumRoute& route) const
{
  // Simplified implementation: check if there's active route state
  for (const auto& activeRoute : m_activeRoutes)
  {
    if (activeRoute.second.route.routeId == route.routeId &&
        activeRoute.second.isEstablished)
    {
      return true;
    }
  }
  
  return false;
}

Time
QCastForwardingEngine::GetEstimatedDelay (const QuantumRoute& route) const
{
  // Estimated delay = per-hop establishment time + swap time
  double perHopEstablishTime = 0.01;  // 10ms per hop
  double swapTime = 0.005;           // 5ms per swap
  
  size_t hopCount = route.GetHopCount ();
  double logHeight = std::log2 (hopCount + 1);
  
  double totalDelay = hopCount * perHopEstablishTime + logHeight * swapTime;
  
  return Seconds (totalDelay);
}

QuantumForwardingStrategy
QCastForwardingEngine::GetForwardingStrategy () const
{
  return m_strategy;
}

void
QCastForwardingEngine::SetForwardingStrategy (QuantumForwardingStrategy strategy)
{
  m_strategy = strategy;
  NS_LOG_LOGIC ("Set forwarding strategy to " << (int)strategy);
}

// ===========================================================================
// Network layer registry implementation
// ===========================================================================

void
QCastForwardingEngine::RegisterNetworkLayer (const std::string& address, Ptr<QuantumNetworkLayer> layer)
{
  NS_LOG_INFO ("[REGISTRY] Registering network layer for address: " << address << " (ptr: " << layer << ")");
  s_networkLayerRegistry[address] = layer;
  NS_LOG_INFO ("[REGISTRY] Total registered layers: " << s_networkLayerRegistry.size());
}

void
QCastForwardingEngine::UnregisterNetworkLayer (const std::string& address)
{
  NS_LOG_LOGIC ("Unregistering network layer for address: " << address);
  auto it = s_networkLayerRegistry.find (address);
  if (it != s_networkLayerRegistry.end ())
  {
    s_networkLayerRegistry.erase (it);
  }
}

Ptr<QuantumNetworkLayer>
QCastForwardingEngine::GetNetworkLayer (const std::string& address)
{
  NS_LOG_INFO ("[REGISTRY] Looking up network layer for address: " << address);
  NS_LOG_INFO ("[REGISTRY] Total registered layers: " << s_networkLayerRegistry.size());
  
  // Log all registered addresses for debugging
  for (const auto& entry : s_networkLayerRegistry)
  {
    NS_LOG_INFO ("[REGISTRY]   Registered: " << entry.first << " -> " << entry.second);
  }
  
  auto it = s_networkLayerRegistry.find (address);
  if (it != s_networkLayerRegistry.end ())
  {
    NS_LOG_INFO ("[REGISTRY] Found network layer for address: " << address);
    return it->second;
  }
  
  NS_LOG_WARN ("[REGISTRY] Network layer not found for address: " << address);
  return nullptr;
}

void
QCastForwardingEngine::SetResourceManager (Ptr<QuantumResourceManager> manager)
{
  m_resourceManager = manager;
}

Ptr<QuantumResourceManager>
QCastForwardingEngine::GetResourceManager () const
{
  return m_resourceManager;
}

QuantumNetworkStats
QCastForwardingEngine::GetStatistics () const
{
  return m_stats;
}

void
QCastForwardingEngine::ResetStatistics ()
{
  m_stats = QuantumNetworkStats ();
}

} // namespace ns3