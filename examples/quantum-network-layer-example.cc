/* 
  Quantum Network Layer Example
  
  This example demonstrates the quantum network layer functionality:
  - Path setup and routing
  - Entanglement distribution across multiple hops
  - Entanglement swapping coordination
  
  To run this example:
  NS_LOG="QuantumNetworkLayer=info:QuantumBasis=info" ./ns3 run quantum-network-layer-example
*/

#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/distribute-epr-helper.h"
#include "ns3/distribute-epr-protocol.h"
#include "ns3/quantum-net-stack-helper.h"

#include <iostream>
#include <vector>

NS_LOG_COMPONENT_DEFINE ("QuantumNetworkLayerExample");

using namespace ns3;

// Number of nodes in the linear chain
#define NUM_NODES 5

/**
 * \brief Application that uses the quantum network layer to establish paths
 */
class QuantumPathUserApp : public Application
{
public:
  QuantumPathUserApp (Ptr<QuantumNetworkLayer> netLayer, const std::string &myName)
      : m_netLayer (netLayer), m_myName (myName), m_pathId (INVALID_PATH_ID)
  {
  }

  void SetupPathTo (const std::string &dstNode, double minFidelity)
  {
    NS_LOG_INFO (m_myName << ": Requesting path to " << dstNode 
                 << " with fidelity " << minFidelity);
    
    PathReadyCallback callback = MakeCallback (&QuantumPathUserApp::OnPathReady, this);
    m_pathId = m_netLayer->SetupPath (m_myName, dstNode, minFidelity, callback);
    
    NS_LOG_INFO (m_myName << ": Path request submitted, ID=" << m_pathId);
  }

  void OnPathReady (PathId pathId, bool success)
  {
    if (success)
      {
        NS_LOG_INFO (GREEN_CODE << m_myName << ": Path " << pathId 
                     << " is ready for quantum communication!" << END_CODE);
        
        // Get path info
        PathInfo info = m_netLayer->GetPathInfo (pathId);
        NS_LOG_INFO (m_myName << ": Route: " << RouteToString (info.route));
        NS_LOG_INFO (m_myName << ": End-to-end fidelity meets requirement: " << info.minFidelity);
      }
    else
      {
        NS_LOG_WARN (RED_CODE << m_myName << ": Path " << pathId << " setup failed!" << END_CODE);
      }
  }

  std::string RouteToString (const std::vector<std::string> &route)
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

private:
  Ptr<QuantumNetworkLayer> m_netLayer;
  std::string m_myName;
  PathId m_pathId;
};

int
main ()
{
  NS_LOG_INFO ("Starting Quantum Network Layer Example");

  //
  // Create quantum physical entity with N nodes
  //
  std::vector<std::string> owners;
  for (int i = 0; i < NUM_NODES; ++i)
    {
      owners.push_back ("Node" + std::to_string (i));
    }
  
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  NodeContainer nodes;
  for (int i = 0; i < NUM_NODES; ++i)
    {
      Ptr<QuantumNode> node = qphyent->GetNode ("Node" + std::to_string (i));
      nodes.Add (node);
    }

  //
  // Create classical connections (linear topology)
  //
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("1000kbps")));
  csmaHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (CLASSICAL_DELAY)));
  NetDeviceContainer devices = csmaHelper.Install (nodes);

  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv6AddressHelper address;
  address.SetBase ("2001:1::", Ipv6Prefix (64));
  Ipv6InterfaceContainer interfaces = address.Assign (devices);

  unsigned rank = 0;
  for (const std::string &owner : owners)
    {
      qphyent->SetOwnerAddress (owner, interfaces.GetAddress (rank, 1));
      qphyent->SetOwnerRank (owner, rank);
      ++rank;
    }

  //
  // Install quantum network stack
  //
  QuantumNetStackHelper qstack;
  qstack.Install (nodes);

  //
  // Create and configure network layers for each node
  //
  std::vector<Ptr<QuantumNetworkLayer>> netLayers;
  
  for (int i = 0; i < NUM_NODES; ++i)
    {
      std::string nodeName = "Node" + std::to_string (i);
      Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer> ();
      netLayer->SetOwner (nodeName);
      netLayer->SetPhyEntity (qphyent);
      
      // Add neighbors (linear topology)
      if (i > 0)
        {
          std::string prevNode = "Node" + std::to_string (i - 1);
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (prevNode, nodeName);
          netLayer->AddNeighbor (prevNode, channel, 0.95, 0.9);
          
          // Also add to previous node's network layer
          if (i - 1 >= 0)
            {
              netLayers[i - 1]->AddNeighbor (nodeName, channel, 0.95, 0.9);
            }
        }
      
      if (i < NUM_NODES - 1)
        {
          std::string nextNode = "Node" + std::to_string (i + 1);
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (nodeName, nextNode);
          // Will be added when processing next node
        }
      
      netLayers.push_back (netLayer);
      // QuantumNetworkLayer is not an Application - do not AddApplication
    }

  //
  // Set up link layer services (simplified - using a mock)
  // In a real implementation, this would be actual link layer protocols
  //
  
  //
  // Create path user applications
  //
  
  // Node0 will establish a path to Node4
  Ptr<QuantumPathUserApp> app0 = CreateObject<QuantumPathUserApp> (netLayers[0], "Node0");
  nodes.Get (0)->AddApplication (app0);
  app0->SetStartTime (Seconds (0.1));
  
  // Schedule path setup
  Simulator::Schedule (Seconds (0.5), &QuantumPathUserApp::SetupPathTo, app0, "Node4", 0.85);

  //
  // Also set up some intermediate entanglements for testing
  //
  for (int i = 0; i < NUM_NODES - 1; ++i)
    {
      std::string srcOwner = "Node" + std::to_string (i);
      std::string dstOwner = "Node" + std::to_string (i + 1);
      Ptr<QuantumChannel> qconn = CreateObject<QuantumChannel> (srcOwner, dstOwner);
      
      // Set depolarization model for the channel
      qconn->SetDepolarModel (0.95, qphyent);
      
      // Generate and distribute EPR pairs
      Ptr<DistributeEPRSrcProtocol> distEprSrc =
          qphyent->GetConn2Apps (qconn, APP_DIST_EPR).first->GetObject<DistributeEPRSrcProtocol> ();
      
      Simulator::Schedule (Seconds (CLASSICAL_DELAY * (i + 1)),
                           &DistributeEPRSrcProtocol::GenerateAndDistributeEPR, distEprSrc,
                           std::pair<std::string, std::string>{
                               srcOwner + "_EPR_" + dstOwner, dstOwner + "_EPR_" + srcOwner});
    }

  //
  // Run simulation
  //
  Simulator::Stop (Seconds (10.0));
  
  NS_LOG_INFO ("Running simulation...");
  auto start = std::chrono::high_resolution_clock::now ();
  Simulator::Run ();
  auto end = std::chrono::high_resolution_clock::now ();
  
  printf ("\n=== Simulation Complete ===\n");
  printf ("Total time cost: %ld ms\n",
          std::chrono::duration_cast<std::chrono::milliseconds> (end - start).count ());
  
  Simulator::Destroy ();

  return 0;
}
