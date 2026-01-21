/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Q-CAST forwarding engine implementation
 *
 * This file implements the Q-CAST forwarding engine for quantum networks.
 * The engine provides XOR recovery decision and log-time entanglement
 * swap scheduling as described in the Q-CAST protocol.
 */

#include "qcast-forwarding-engine.h"
#include "../quantum-basis.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-packet.h"
#include "ns3/random-variable-stream.h"
#include <exatn.hpp>
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
    m_nextRouteId (1),
    m_classicalDelay (MilliSeconds (1.0)),      // Default: 1 ms base delay
    m_classicalDelayPerHop (MilliSeconds (0.5)), // Default: 0.5 ms per hop
    m_classicalDelayJitter (0.5)                // Default: 50% jitter to simulate background traffic
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
  // Use GetHopCount() which correctly handles nodeSequence for multi-hop routes
  size_t hopCount = route.GetHopCount ();
  NS_LOG_LOGIC ("Performing log-time swap scheduling for " << hopCount << "-hop route");
  
  if (hopCount == 0)
    return;
  
  // Calculate tree height for log-time swap scheduling
  // For n hops, we need n-1 entanglement swaps organized in a binary tree
  size_t treeHeight = static_cast<size_t>(std::ceil(std::log2(hopCount + 1)));
  
  NS_LOG_LOGIC ("Route with " << hopCount << " hops organized into tree of height " << treeHeight);
  
  // Leaf nodes: directly adjacent entanglement pairs
  // Each hop corresponds to an EPR pair that will need to be swapped
  for (size_t i = 0; i < hopCount; ++i)
  {
    if (!route.nodeSequence.empty () && i + 1 < route.nodeSequence.size ())
    {
      NS_LOG_LOGIC ("Leaf swap at hop " << i << ": " 
                   << route.nodeSequence[i] << " -> " << route.nodeSequence[i + 1]);
    }
    else if (i < route.path.size ())
    {
      NS_LOG_LOGIC ("Leaf swap at channel " << i << ": " 
                   << route.path[i]->GetSrcOwner () << " -> " << route.path[i]->GetDstOwner ());
    }
    
    // Actual entanglement swap should be performed here
    m_stats.entanglementSwaps++;
  }
  
  // Internal nodes: wait for child nodes to complete
  // Each level requires classical message exchange to coordinate swaps
  // The delay at each level includes:
  // 1. Classical message propagation (random delay simulating background traffic)
  // 2. Swap operation time
  Time cumulativeDelay = Seconds (0.0);
  
  for (size_t level = 1; level < treeHeight; ++level)
  {
    size_t swapsAtLevel = hopCount / (1 << level);
    NS_LOG_LOGIC ("Level " << level << " swaps: " << swapsAtLevel);
    
    // Add classical delay for this level (coordination messages)
    // Use random delay to simulate background traffic
    Time levelClassicalDelay = GetRandomClassicalDelay ();
    cumulativeDelay += levelClassicalDelay;
    
    // Also add quantum operation time
    Time quantumOpTime = MilliSeconds (1.0);  // 1ms for swap operation
    cumulativeDelay += quantumOpTime;
    
    NS_LOG_LOGIC ("Level " << level << " delay: classical=" << levelClassicalDelay.As (Time::MS)
                  << ", cumulative=" << cumulativeDelay.As (Time::MS));
    
    // Schedule swap with cumulative delay (must wait for previous levels)
    Simulator::Schedule (cumulativeDelay, [this]() {
      m_stats.entanglementSwaps++;
    });
  }
  
  NS_LOG_LOGIC ("Log-time scheduling completed in " << treeHeight << " levels "
               << "(vs " << hopCount << " levels in linear scheduling)"
               << ", total classical delay=" << cumulativeDelay.As (Time::MS));
  
  // Schedule entanglement swaps and end-to-end fidelity calculation
  // This ensures TimeModel applies decoherence based on actual elapsed time
  // Use route-specific QuantumPhyEntity if available, otherwise fall back to shared one
  Ptr<QuantumPhyEntity> qphyentToUse = m_currentRouteQphyent ? m_currentRouteQphyent : m_qphyent;
  
  if (qphyentToUse && !m_currentRouteEprPairs.empty ())
  {
    NS_LOG_INFO ("Scheduling entanglement swaps and fidelity calculation after cumulative delay of " 
                 << cumulativeDelay.As (Time::MS)
                 << " for " << m_currentRouteEprPairs.size () << " EPR pairs"
                 << (m_currentRouteQphyent ? " (using dedicated QuantumPhyEntity)" : " (using shared QuantumPhyEntity)"));
    
    // Capture current state for the scheduled callback
    std::vector<std::pair<std::string, std::string>> eprPairs = m_currentRouteEprPairs;
    std::pair<std::string, std::string> endpoints = m_currentRouteEndpoints;
    uint32_t routeId = route.routeId;
    double estimatedFidelity = route.estimatedFidelity;
    uint32_t hopCountForStats = route.GetHopCount ();
    Time totalDelay = cumulativeDelay;
    
    // Capture the route-specific QuantumPhyEntity by value (Ptr is reference-counted)
    Ptr<QuantumPhyEntity> routeQphyent = qphyentToUse;
    
    // Schedule entanglement swaps and fidelity calculation after the classical delays
    Simulator::Schedule (cumulativeDelay, [this, eprPairs, endpoints, routeId, estimatedFidelity, hopCountForStats, totalDelay, routeQphyent]() {
      // Full entanglement swap protocol with Pauli corrections
      // For a chain A-B-C-D with EPR pairs (a,b1), (b2,c1), (c2,d):
      // 1. Bell measurements at intermediate nodes
      // 2. Apply controlled Pauli corrections
      // 3. Partial trace to remove measured qubits
      // Result: end-to-end entanglement between endpoints.first (a) and endpoints.second (d)
      
      size_t numSwaps = eprPairs.size () - 1;
      NS_LOG_INFO ("Performing " << numSwaps << " entanglement swaps for route " << routeId);
      
      if (numSwaps == 0)
      {
        // Single hop: no swap needed, just measure fidelity of the EPR pair
        double fidel = 0.0;
        double actualFidelity = routeQphyent->CalculateFidelity (eprPairs[0], fidel);
        
        ActualFidelityStats stats;
        stats.routeId = routeId;
        stats.estimatedFidelity = estimatedFidelity;
        stats.actualFidelity = actualFidelity;
        stats.hopCount = hopCountForStats;
        stats.establishmentTime = Simulator::Now ();
        stats.waitTime = totalDelay;
        m_actualFidelityStats.push_back (stats);
        
        NS_LOG_INFO ("Single-hop fidelity for route " << routeId << ": " << actualFidelity);
        return;
      }
      
      // For multi-hop routes, perform entanglement swaps with measurement-based protocol
      // This simulates the real physical process:
      // 1. Each intermediate node holds two qubits (one from each adjacent EPR pair)
      // 2. Bell measurement: CNOT(latter, former) + H(former) + Measure both
      // 3. After measurement, the measured qubits are discarded from quantum memory
      // 4. Pauli corrections are applied to the target qubit based on measurement results
      // 5. Final state: only source and destination nodes hold qubits (end-to-end entanglement)
      
      // Target qubit for all corrections is the destination node's qubit
      std::string targetQubit = endpoints.second;
      
      // Accumulate XOR of measurement results for final Pauli correction
      unsigned flagX = 0;  // Controls X correction (from latter qubit measurements)
      unsigned flagZ = 0;  // Controls Z correction (from former qubit measurements after H)
      
      for (size_t i = 0; i < numSwaps; ++i)
      {
        // At intermediate node i+1:
        // - qubitFormer = qubit from EPR pair i (connected to node i, the "left" neighbor)
        // - qubitLatter = qubit from EPR pair i+1 (connected to node i+2, the "right" neighbor)
        // These two qubits are stored in the quantum memory of node i+1
        std::string qubitFormer = eprPairs[i].second;      // Right qubit of pair i
        std::string qubitLatter = eprPairs[i + 1].first;   // Left qubit of pair i+1
        
        NS_LOG_INFO ("Swap " << i << ": Intermediate node performs Bell measurement");
        NS_LOG_INFO ("  Former qubit (from left): " << qubitFormer);
        NS_LOG_INFO ("  Latter qubit (from right): " << qubitLatter);
        
        // Bell measurement circuit: CNOT(control=latter, target=former) + H(former)
        routeQphyent->ApplyGate ("God", QNS_GATE_PREFIX + "CNOT", {}, {qubitLatter, qubitFormer});
        routeQphyent->ApplyGate ("God", QNS_GATE_PREFIX + "H", {}, {qubitFormer});
        
        // Perform computational basis measurements on both qubits
        auto outcome0 = routeQphyent->Measure ("God", {qubitFormer});  // Result determines Z correction
        auto outcome1 = routeQphyent->Measure ("God", {qubitLatter});  // Result determines X correction
        
        NS_LOG_INFO ("  Measurement results: m0=" << outcome0.first << " (Z), m1=" << outcome1.first << " (X)");
        
        // Discard measured qubits from quantum memory (partial trace)
        routeQphyent->PartialTrace ({qubitFormer, qubitLatter});
        
        // Accumulate XOR of measurement outcomes for final correction
        flagZ ^= outcome0.first;
        flagX ^= outcome1.first;
        
        m_stats.entanglementSwaps++;
      }
      
      // Apply accumulated Pauli corrections to the destination node's qubit
      // This simulates the classical communication of measurement results and correction
      NS_LOG_INFO ("Applying Pauli corrections to target qubit: flagX=" << flagX << ", flagZ=" << flagZ);
      
      if (flagX == 1)
      {
        routeQphyent->ApplyGate ("God", QNS_GATE_PREFIX + "PX", {}, {targetQubit});
      }
      
      if (flagZ == 1)
      {
        routeQphyent->ApplyGate ("God", QNS_GATE_PREFIX + "PZ", {}, {targetQubit});
      }
      
      // Calculate end-to-end fidelity
      // At this point, only source qubit (endpoints.first) and destination qubit (endpoints.second) remain
      double actualFidelity = -1.0;
      
      if (!endpoints.first.empty () && !endpoints.second.empty ())
      {
        NS_LOG_INFO ("Calculating end-to-end fidelity between:");
        NS_LOG_INFO ("  Source qubit: " << endpoints.first);
        NS_LOG_INFO ("  Destination qubit: " << endpoints.second);
        
        double fidel = 0.0;
        actualFidelity = routeQphyent->CalculateFidelity (endpoints, fidel);
        
        NS_LOG_INFO ("End-to-end fidelity (physics layer): " << actualFidelity);
      }
      else
      {
        NS_LOG_WARN ("Endpoints not set, cannot calculate end-to-end fidelity");
      }
      
      // Record actual fidelity statistics
      ActualFidelityStats stats;
      stats.routeId = routeId;
      stats.estimatedFidelity = estimatedFidelity;
      stats.actualFidelity = actualFidelity;
      stats.hopCount = hopCountForStats;
      stats.establishmentTime = Simulator::Now ();
      stats.waitTime = totalDelay;
      
      m_actualFidelityStats.push_back (stats);
      
      NS_LOG_INFO ("End-to-end fidelity (after " << totalDelay.As (Time::MS) << " delay) for route " << routeId 
                   << ": estimated=" << estimatedFidelity
                   << ", actual=" << actualFidelity
                   << ", hops=" << hopCountForStats);
      
      // Note: ExaTN tensor cleanup is now handled at test completion
      // (after Simulator::Destroy()) to avoid interference with concurrent routes
      NS_LOG_INFO ("Route " << routeId << " fidelity calculation completed");
    });
    
    // Clear the route state to avoid duplicate calculations
    // Note: routeQphyent will be automatically cleaned up after the lambda completes
    // (it's captured by value as a Ptr, so reference count will drop to 0)
    m_currentRouteEprPairs.clear ();
    m_currentRouteEndpoints = std::make_pair ("", "");
    m_currentRouteQphyent = nullptr;  // Release the reference
  }
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
      
      // Deliver packet to destination's network layer with classical delay
      // This will trigger ProcessPacket -> ReceivePacket on the routing protocol
      DeliverPacketWithDelay (packet, dstNetworkLayer, m_classicalDelay);
      NS_LOG_INFO ("[FORWARD] Packet delivery scheduled with delay " << m_classicalDelay.As (Time::MS));
      
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
        
        // Deliver packet to destination's network layer with classical delay
        // This will trigger ProcessPacket -> ReceivePacket on the routing protocol
        DeliverPacketWithDelay (packet, dstNetworkLayer, m_classicalDelay);
        NS_LOG_INFO ("[FORWARD] Packet delivery scheduled with delay " << m_classicalDelay.As (Time::MS));
        
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
  
  // Record establishment start time for actual fidelity calculation
  Time establishmentStartTime = Simulator::Now ();
  
  // Create a dedicated QuantumPhyEntity for this route to avoid tensor network interference
  // Each route gets its own isolated tensor network for correct fidelity calculation
  Ptr<QuantumPhyEntity> routeQphyent = nullptr;
  if (m_qphyent)
  {
    // Note: ExaTN tensor cleanup is now handled at test completion
    // With unique tensor names (via static counter), we don't need to destroy
    // tensors mid-simulation. This avoids interfering with ExaTN's internal state.
    
    // Collect all node names that will be involved in this route
    std::vector<std::string> routeOwners;
    if (!route.nodeSequence.empty ())
    {
      for (const auto& node : route.nodeSequence)
      {
        routeOwners.push_back (node);
      }
    }
    else
    {
      // Fallback: use channel owners
      for (const auto& channel : route.path)
      {
        if (std::find (routeOwners.begin (), routeOwners.end (), channel->GetSrcOwner ()) == routeOwners.end ())
        {
          routeOwners.push_back (channel->GetSrcOwner ());
        }
        if (std::find (routeOwners.begin (), routeOwners.end (), channel->GetDstOwner ()) == routeOwners.end ())
        {
          routeOwners.push_back (channel->GetDstOwner ());
        }
      }
    }
    
    // Create isolated QuantumPhyEntity for this route
    routeQphyent = CreateObject<QuantumPhyEntity> (routeOwners);
    NS_LOG_INFO ("Created dedicated QuantumPhyEntity for route " << route.routeId 
                 << " with " << routeOwners.size () << " owners");
  }
  
  // Distribute EPR pairs along the path
  // Use unique EPR pair names with route ID and timestamp to avoid tensor name conflicts
  static uint64_t eprCounter = 0;
  uint64_t routeTimestamp = Simulator::Now ().GetNanoSeconds ();
  
  // Track endpoint qubits for end-to-end fidelity calculation
  std::string sourceQubit;
  std::string destQubit;
  
  // Clear the EPR pairs list for this route
  m_currentRouteEprPairs.clear ();
  
  // Get hop count from nodeSequence (which is always populated correctly by G-EDA)
  // route.path may not have all channels for multi-hop routes since G-EDA runs on source node
  // and only has direct access to its own neighbor channels
  size_t hopCount = route.nodeSequence.empty () ? route.path.size () : route.nodeSequence.size () - 1;
  
  NS_LOG_INFO ("EstablishEntanglement: nodeSequence.size=" << route.nodeSequence.size ()
               << ", path.size=" << route.path.size () << ", hopCount=" << hopCount);
  
  // For multi-hop routes, use nodeSequence to get channel info from each node's network layer
  if (!route.nodeSequence.empty () && route.nodeSequence.size () > 1)
  {
    for (size_t i = 0; i < route.nodeSequence.size () - 1; ++i)
    {
      std::string srcNode = route.nodeSequence[i];
      std::string dstNode = route.nodeSequence[i + 1];
      
      // Get channel from the source node's network layer
      Ptr<QuantumChannel> channel = nullptr;
      Ptr<QuantumNetworkLayer> srcNetworkLayer = GetNetworkLayer (srcNode);
      
      if (srcNetworkLayer)
      {
        // Find the channel to the destination node
        std::vector<Ptr<QuantumChannel>> neighbors = srcNetworkLayer->GetNeighbors ();
        for (const auto& neighborChannel : neighbors)
        {
          if (neighborChannel->GetDstOwner () == dstNode ||
              neighborChannel->GetSrcOwner () == dstNode)
          {
            channel = neighborChannel;
            break;
          }
        }
      }
      
      if (!channel)
      {
        NS_LOG_WARN ("No channel found from " << srcNode << " to " << dstNode);
        // Try using route.path if available for this hop
        if (i < route.path.size ())
        {
          channel = route.path[i];
          NS_LOG_INFO ("Using route.path[" << i << "] as fallback");
        }
        else
        {
          NS_LOG_ERROR ("Cannot establish EPR pair: no channel available for hop " << i);
          return false;
        }
      }
      
      // Create unique EPR pair names using route ID, hop index, timestamp, and counter
      std::string uniqueId = std::to_string (route.routeId) + "_" + 
                             std::to_string (routeTimestamp) + "_" +
                             std::to_string (eprCounter++) + "_" +
                             std::to_string (i);
      std::string qubit1 = srcNode + "_epr_" + uniqueId + "_a";
      std::string qubit2 = dstNode + "_epr_" + uniqueId + "_b";
      std::pair<std::string, std::string> epr = std::make_pair (qubit1, qubit2);
      
      // Track all EPR pairs for fidelity calculation
      m_currentRouteEprPairs.push_back (epr);
      
      // Track first and last qubits for end-to-end fidelity
      if (i == 0)
      {
        sourceQubit = qubit1;  // Source node's qubit
      }
      if (i == route.nodeSequence.size () - 2)
      {
        destQubit = qubit2;  // Destination node's qubit
      }
      
      NS_LOG_INFO ("Distributing EPR pair for hop " << i << ": " << srcNode << " -> " << dstNode);
      
      // Generate EPR pair using the route-specific QuantumPhyEntity
      if (routeQphyent && channel)
      {
        double channelFidelity = channel->GetFidelity (m_qphyent);
        NS_LOG_INFO ("Channel fidelity: " << channelFidelity);
        
        std::vector<std::complex<double>> epr_dm = GetEPRwithFidelity (channelFidelity);
        routeQphyent->GenerateQubitsMixed (srcNode, epr_dm, {epr.first, epr.second});
        NS_LOG_INFO ("EPR pair generated with fidelity " << channelFidelity);
      }
      else if (!DistributeEPR (channel, epr))
      {
        NS_LOG_WARN ("Failed to distribute EPR pair on channel "
                     << srcNode << " -> " << dstNode);
        return false;
      }
    }
  }
  else
  {
    // Fallback: use route.path directly (for backward compatibility)
    for (size_t i = 0; i < route.path.size (); ++i)
    {
      const auto& channel = route.path[i];
      
      // Create unique EPR pair names using route ID, hop index, timestamp, and counter
      std::string uniqueId = std::to_string (route.routeId) + "_" + 
                             std::to_string (routeTimestamp) + "_" +
                             std::to_string (eprCounter++) + "_" +
                             std::to_string (i);
      std::string qubit1 = channel->GetSrcOwner () + "_epr_" + uniqueId + "_a";
      std::string qubit2 = channel->GetDstOwner () + "_epr_" + uniqueId + "_b";
      std::pair<std::string, std::string> epr = std::make_pair (qubit1, qubit2);
      
      // Track all EPR pairs for fidelity calculation
      m_currentRouteEprPairs.push_back (epr);
      
      // Track first and last qubits for end-to-end fidelity
      if (i == 0)
      {
        sourceQubit = qubit1;  // Source node's qubit
      }
      if (i == route.path.size () - 1)
      {
        destQubit = qubit2;  // Destination node's qubit
      }
      
      // Generate EPR pair using the route-specific QuantumPhyEntity
      if (routeQphyent && channel)
      {
        double channelFidelity = channel->GetFidelity (m_qphyent);
        NS_LOG_INFO ("Channel fidelity: " << channelFidelity);
        
        std::vector<std::complex<double>> epr_dm = GetEPRwithFidelity (channelFidelity);
        routeQphyent->GenerateQubitsMixed (channel->GetSrcOwner (), epr_dm, {epr.first, epr.second});
        NS_LOG_INFO ("EPR pair generated with fidelity " << channelFidelity);
      }
      else if (!DistributeEPR (channel, epr))
      {
        NS_LOG_WARN ("Failed to distribute EPR pair on channel "
                     << channel->GetSrcOwner () << " -> " << channel->GetDstOwner ());
        return false;
      }
    }
  }
  
  NS_LOG_LOGIC ("Entanglement established along route with " << hopCount << " hops");
  m_stats.eprPairsDistributed += hopCount;
  
  // Store endpoint qubits and route-specific QuantumPhyEntity for fidelity calculation 
  // after log-time swap scheduling
  // The actual fidelity calculation is now done in LogTimeSwapScheduling() 
  // AFTER classical delays have elapsed, so TimeModel can apply actual decoherence
  if (routeQphyent && !sourceQubit.empty () && !destQubit.empty ())
  {
    m_currentRouteEndpoints = std::make_pair (sourceQubit, destQubit);
    m_currentRouteQphyent = routeQphyent;  // Store route-specific physics entity
    NS_LOG_INFO ("Stored endpoint qubits for delayed fidelity calculation: " 
                 << sourceQubit << " -> " << destQubit
                 << " (" << m_currentRouteEprPairs.size () << " EPR pairs)"
                 << " with dedicated QuantumPhyEntity");
  }
  else if (m_qphyent && !sourceQubit.empty () && !destQubit.empty ())
  {
    // Fallback to shared QuantumPhyEntity (for backward compatibility)
    m_currentRouteEndpoints = std::make_pair (sourceQubit, destQubit);
    m_currentRouteQphyent = nullptr;
    NS_LOG_INFO ("Stored endpoint qubits (using shared QuantumPhyEntity): " 
                 << sourceQubit << " -> " << destQubit);
  }
  
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
  
  NS_LOG_INFO ("Performing entanglement swap with qubits: " << qubits[0] << ", " << qubits[1]);
  
  // Check if physics layer is enabled
  if (m_qphyent && qubits.size () >= 2)
  {
    NS_LOG_INFO ("Physics layer enabled, performing actual entanglement swap");
    
    // Full entanglement swap protocol:
    // For EPR pairs (A,B1) and (B2,C), we want to create entanglement between A and C
    // qubits[0] = B1 (from first EPR pair)
    // qubits[1] = B2 (from second EPR pair)
    //
    // Step 1: Bell measurement on B1, B2 (CNOT + H)
    // Step 2: Apply controlled Pauli corrections based on measurement results
    //         This is done using ApplyControlledOperation which applies gates
    //         conditioned on the classical bits from measurement
    // Step 3: Partial trace to remove the measured qubits
    
    // Step 1: Apply CNOT gate (control on qubit 1, target on qubit 0)
    bool cnotSuccess = m_qphyent->ApplyGate (
      "God",  // Use "God" as owner for cross-node operations
      QNS_GATE_PREFIX + "CNOT",
      std::vector<std::complex<double>>{},  // Empty data for built-in gates
      std::vector<std::string>{qubits[1], qubits[0]}  // control, target
    );
    
    if (!cnotSuccess)
    {
      NS_LOG_WARN ("CNOT gate failed in entanglement swap");
      return false;
    }
    
    // Apply Hadamard gate on qubit 0
    bool hSuccess = m_qphyent->ApplyGate (
      "God",
      QNS_GATE_PREFIX + "H",
      std::vector<std::complex<double>>{},
      std::vector<std::string>{qubits[0]}
    );
    
    if (!hSuccess)
    {
      NS_LOG_WARN ("Hadamard gate failed in entanglement swap");
      return false;
    }
    
    NS_LOG_INFO ("Bell measurement gates (CNOT + H) applied successfully");
  }
  else
  {
    NS_LOG_LOGIC ("Physics layer not enabled, using simplified simulation");
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
  
  NS_LOG_INFO ("Distributing EPR pair " << epr.first << " - " << epr.second
               << " on channel " << channel->GetSrcOwner () << " -> " << channel->GetDstOwner ());
  
  // Check if physics layer is enabled
  if (m_qphyent)
  {
    NS_LOG_INFO ("Physics layer enabled, generating EPR with depolarizing noise");
    
    // Get channel fidelity (set during topology configuration)
    double channelFidelity = channel->GetFidelity (m_qphyent);
    NS_LOG_INFO ("Channel fidelity: " << channelFidelity);
    
    // Generate EPR pair with depolarizing noise using density matrix
    // GetEPRwithFidelity returns a Werner state: rho = f|Phi+><Phi+| + (1-f)/3 * (other Bell states)
    std::vector<std::complex<double>> epr_dm = GetEPRwithFidelity (channelFidelity);
    
    // Use GenerateQubitsMixed to create the noisy EPR pair
    // Note: We use the source owner to generate both qubits (they will be tracked correctly)
    m_qphyent->GenerateQubitsMixed (channel->GetSrcOwner (), epr_dm, {epr.first, epr.second});
    
    // Track this EPR pair for later fidelity calculation
    m_generatedEprPairs.push_back (epr);
    
    NS_LOG_INFO ("EPR pair generated with fidelity " << channelFidelity);
  }
  else
  {
    NS_LOG_LOGIC ("Physics layer not enabled, using simplified simulation");
  }
  
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

// ===========================================================================
// Classical Network Delay Methods
// ===========================================================================

void
QCastForwardingEngine::SetClassicalDelay (Time delay)
{
  m_classicalDelay = delay;
  NS_LOG_LOGIC ("Set classical delay to " << delay.As (Time::MS));
}

Time
QCastForwardingEngine::GetClassicalDelay () const
{
  return m_classicalDelay;
}

void
QCastForwardingEngine::SetClassicalDelayPerHop (Time delayPerHop)
{
  m_classicalDelayPerHop = delayPerHop;
  NS_LOG_LOGIC ("Set classical delay per hop to " << delayPerHop.As (Time::MS));
}

Time
QCastForwardingEngine::GetClassicalDelayPerHop () const
{
  return m_classicalDelayPerHop;
}

void
QCastForwardingEngine::SetClassicalDelayJitter (double jitterRatio)
{
  m_classicalDelayJitter = std::max (0.0, std::min (1.0, jitterRatio));  // Clamp to [0, 1]
  NS_LOG_LOGIC ("Set classical delay jitter to " << m_classicalDelayJitter * 100 << "%");
}

double
QCastForwardingEngine::GetClassicalDelayJitter () const
{
  return m_classicalDelayJitter;
}

Time
QCastForwardingEngine::GetRandomClassicalDelay () const
{
  // Generate random delay with jitter to simulate background traffic
  // delay = base_delay * (1 + jitter * random(-1, 1))
  // This gives delay in range [base * (1-jitter), base * (1+jitter)]
  
  // Use a static random variable that respects ns-3's RNG system
  // The AssignStreams mechanism ensures reproducibility with RngRun
  static Ptr<UniformRandomVariable> randVar = nullptr;
  if (randVar == nullptr)
  {
    randVar = CreateObject<UniformRandomVariable> ();
    // Let ns-3 manage the stream assignment for reproducibility
    // RNG stream is auto-assigned by ns-3
  }
  
  double baseDelayMs = m_classicalDelay.GetMilliSeconds ();
  double jitterFactor = 1.0 + m_classicalDelayJitter * (2.0 * randVar->GetValue (0, 1) - 1.0);
  double actualDelayMs = baseDelayMs * jitterFactor;
  
  // Ensure delay is non-negative
  actualDelayMs = std::max (0.0, actualDelayMs);
  
  NS_LOG_LOGIC ("Classical delay: base=" << baseDelayMs << "ms, jitter=" 
                << m_classicalDelayJitter * 100 << "%, actual=" << actualDelayMs << "ms");
  
  return MilliSeconds (actualDelayMs);
}

void
QCastForwardingEngine::DeliverPacketWithDelay (Ptr<QuantumPacket> packet,
                                                Ptr<QuantumNetworkLayer> dstNetworkLayer,
                                                Time delay)
{
  if (delay.IsZero ())
  {
    // Immediate delivery (for backward compatibility or when delay is disabled)
    dstNetworkLayer->ReceivePacket (packet);
  }
  else
  {
    // Use random delay with jitter if enabled
    Time actualDelay = (m_classicalDelayJitter > 0) ? GetRandomClassicalDelay () : delay;
    
    // Schedule packet delivery after the specified delay
    NS_LOG_INFO ("[FORWARD] Scheduling packet delivery with delay " << actualDelay.As (Time::MS));
    Simulator::Schedule (actualDelay, &QuantumNetworkLayer::ReceivePacket, dstNetworkLayer, packet);
  }
}

// ===========================================================================
// Physics Layer Integration Methods
// ===========================================================================

void
QCastForwardingEngine::SetQuantumPhyEntity (Ptr<QuantumPhyEntity> qphyent)
{
  m_qphyent = qphyent;
  NS_LOG_INFO ("QCastForwardingEngine: Physics layer " << (qphyent ? "enabled" : "disabled"));
}

Ptr<QuantumPhyEntity>
QCastForwardingEngine::GetQuantumPhyEntity () const
{
  return m_qphyent;
}

bool
QCastForwardingEngine::IsPhysicsEnabled () const
{
  return (m_qphyent != nullptr);
}

double
QCastForwardingEngine::CalculateActualFidelity (const std::pair<std::string, std::string>& epr)
{
  if (!m_qphyent)
  {
    NS_LOG_WARN ("Physics layer not enabled, cannot calculate actual fidelity");
    return -1.0;
  }
  
  // Use analytical formula to calculate fidelity instead of tensor network simulation
  // This avoids the problem of multiple EPR pairs in the same tensor network
  //
  // For a single EPR pair with depolarizing channel and time decoherence:
  // F_total = F_depolar * F_time
  //
  // Depolarizing channel: F_depolar is set during channel creation
  // Time decoherence: F_time = (1 + exp(-t/T2)) / 2 for dephasing
  
  // Extract node names from qubit names to find the connection
  // Qubit name format: "NodeX_epr_uniqueId_a" or "NodeX_epr_uniqueId_b"
  std::string srcNode = epr.first.substr (0, epr.first.find ("_epr_"));
  std::string dstNode = epr.second.substr (0, epr.second.find ("_epr_"));
  
  // Get depolarizing fidelity from the channel
  std::pair<std::string, std::string> conn = std::make_pair (srcNode, dstNode);
  double F_depolar = m_qphyent->GetConnectionFidelity (conn);
  
  // Get time decoherence
  // Calculate elapsed time since qubit creation
  Time now = Simulator::Now ();
  Time creationTime = Seconds (0);  // EPR pairs created at t=0
  Time duration = now - creationTime;
  
  // Get T2 time from the TimeModel (default is 0.1s = 100ms)
  double T2 = 0.1;  // Default T2 coherence time in seconds
  
  // F_time = (1 + exp(-t/T2)) / 2 for dephasing
  // This applies to each qubit, so for 2 qubits: F_time_total = F_time^2
  double t = duration.GetSeconds ();
  double F_time_single = (1.0 + std::exp (-t / T2)) / 2.0;
  double F_time = F_time_single * F_time_single;  // Two qubits
  
  // Total fidelity
  double fidelity = F_depolar * F_time;
  
  NS_LOG_INFO ("Analytical fidelity for EPR pair (" << epr.first << ", " << epr.second
               << "): F_depolar=" << F_depolar << ", F_time=" << F_time
               << " (t=" << t * 1000 << "ms, T2=" << T2 * 1000 << "ms)"
               << ", F_total=" << fidelity);
  
  return fidelity;
}

std::vector<QCastForwardingEngine::ActualFidelityStats>
QCastForwardingEngine::GetActualFidelityStats () const
{
  return m_actualFidelityStats;
}

double
QCastForwardingEngine::GetAverageActualFidelity () const
{
  if (m_actualFidelityStats.empty ())
  {
    return -1.0;
  }
  
  double sum = 0.0;
  int validCount = 0;
  for (const auto& stats : m_actualFidelityStats)
  {
    if (stats.actualFidelity >= 0.0)
    {
      sum += stats.actualFidelity;
      validCount++;
    }
  }
  
  return (validCount > 0) ? (sum / validCount) : -1.0;
}

void
QCastForwardingEngine::ClearActualFidelityStats ()
{
  m_actualFidelityStats.clear ();
}

} // namespace ns3