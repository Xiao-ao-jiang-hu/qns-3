/* 
  Q-CAST Routing Protocol Example
  
  This example demonstrates the Q-CAST quantum routing protocol:
  - G-EDA (Greedy Extended Dijkstra Algorithm) for path selection
  - Recovery path discovery
  - XOR-based recovery strategy
  - Logarithmic-time swap scheduling
  
  Topology: Linear chain of 6 nodes
  
  To run this example:
  NS_LOG="QCastRoutingProtocol=info:QuantumNetworkLayer=info" ./ns3 run q-cast-example
*/

#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/distribute-epr-helper.h"
#include "ns3/quantum-net-stack-helper.h"

#include <iostream>
#include <vector>

NS_LOG_COMPONENT_DEFINE ("QCastExample");

using namespace ns3;

#define NUM_NODES 6

/**
 * \brief Application that uses Q-CAST for routing
 */
class QCastApplication : public Application
{
public:
  QCastApplication (Ptr<QCastRoutingProtocol> routing, const std::string &nodeName)
      : m_routing (routing), m_nodeName (nodeName)
  {
  }

  void SendRequests (void)
  {
    NS_LOG_INFO (m_nodeName << ": Starting Q-CAST path computation");
    
    // Create concurrent requests
    std::vector<QCastRequest> requests;
    
    // Request 1: Node0 -> Node5
    QCastRequest req1;
    req1.srcNode = "Node0";
    req1.dstNode = "Node5";
    req1.minFidelity = 0.85;
    req1.requestId = 1;
    requests.push_back (req1);
    
    // Request 2: Node1 -> Node4
    QCastRequest req2;
    req2.srcNode = "Node1";
    req2.dstNode = "Node4";
    req2.minFidelity = 0.85;
    req2.requestId = 2;
    requests.push_back (req2);
    
    NS_LOG_INFO ("Submitting " << requests.size () << " concurrent requests");
    
    // Run G-EDA to calculate routes for all requests
    auto paths = m_routing->CalculateRoutesGEDA (requests);
    
    NS_LOG_INFO ("G-EDA selected " << paths.size () << " paths");
    
    // Display results
    for (const auto &entry : paths)
      {
        uint32_t pathId = entry.first;
        const QCastPath &path = entry.second;
        
        NS_LOG_INFO (GREEN_CODE << "Path " << pathId << ":"
                     << " Route=" << m_routing->RouteToString (path.primaryPath)
                     << " E_t=" << path.primaryEt
                     << " Recovery paths=" << path.recoveryPaths.size ()
                     << END_CODE);
        
        // Generate swap schedule
        auto schedule = m_routing->GenerateSwapSchedule (path.primaryPath);
        NS_LOG_INFO ("  Swap schedule: " << schedule.size () << " swaps");
        for (const auto &swap : schedule)
          {
            NS_LOG_LOGIC ("    Round " << swap.first << ": Node at index " << swap.second);
          }
        
        // Test XOR recovery with simulated failures
        std::set<uint32_t> failedLinks = {1}; // Simulate link 1 failure
        auto recoveryRings = m_routing->ExecuteXORRecovery (pathId, failedLinks);
        
        if (!recoveryRings.empty ())
          {
            NS_LOG_INFO ("  Activated " << recoveryRings.size ()
                                         << " recovery rings for failure recovery");
          }
      }
  }

private:
  Ptr<QCastRoutingProtocol> m_routing;
  std::string m_nodeName;
};

int
main (int argc, char *argv[])
{
  NS_LOG_INFO ("Starting Q-CAST Routing Example");

  // Parse command line
  CommandLine cmd;
  uint32_t kHop = 3;
  double alpha = 0.1;
  uint32_t nodeCapacity = 5;
  
  cmd.AddValue ("kHop", "k-hop neighborhood for recovery", kHop);
  cmd.AddValue ("alpha", "Swap failure rate constant", alpha);
  cmd.AddValue ("capacity", "Node capacity", nodeCapacity);
  cmd.Parse (argc, argv);

  NS_LOG_INFO ("Q-CAST Parameters: k=" << kHop << ", alpha=" << alpha
                                        << ", capacity=" << nodeCapacity);

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
  // Create and configure network layers with Q-CAST routing
  //
  std::vector<Ptr<QuantumNetworkLayer>> netLayers;
  std::vector<Ptr<QCastRoutingProtocol>> qcastProtocols;
  
  for (int i = 0; i < NUM_NODES; ++i)
    {
      std::string nodeName = "Node" + std::to_string (i);
      
      // Create network layer
      Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer> ();
      netLayer->SetOwner (nodeName);
      netLayer->SetPhyEntity (qphyent);
      
      // Create and configure Q-CAST routing protocol
      Ptr<QCastRoutingProtocol> qcastRouting = CreateObject<QCastRoutingProtocol> ();
      qcastRouting->SetKHop (kHop);
      qcastRouting->SetAlpha (alpha);
      qcastRouting->SetNodeCapacity (nodeCapacity);
      
      // Set Q-CAST as the routing protocol
      netLayer->SetRoutingProtocol (qcastRouting);
      
      // Add neighbors (linear topology)
      if (i > 0)
        {
          std::string prevNode = "Node" + std::to_string (i - 1);
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (prevNode, nodeName);
          // Set different link success rates for diversity
          double linkProb = 0.9 + 0.08 * (i % 2); // Alternate between 0.9 and 0.98
          netLayer->AddNeighbor (prevNode, channel, 0.95, linkProb);
        }
      
      if (i < NUM_NODES - 1)
        {
          std::string nextNode = "Node" + std::to_string (i + 1);
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (nodeName, nextNode);
          double linkProb = 0.9 + 0.08 * ((i + 1) % 2);
          netLayer->AddNeighbor (nextNode, channel, 0.95, linkProb);
        }
      
      // Initialize network layer (this will initialize Q-CAST)
      netLayer->Initialize ();
      
      netLayers.push_back (netLayer);
      qcastProtocols.push_back (qcastRouting);
      // QuantumNetworkLayer is not an Application - do not AddApplication
    }

  //
  // Create Q-CAST application on Node0
  //
  Ptr<QCastApplication> app0 = CreateObject<QCastApplication> (qcastProtocols[0], "Node0");
  nodes.Get (0)->AddApplication (app0);
  app0->SetStartTime (Seconds (0.5));

  //
  // Run simulation
  //
  Simulator::Stop (Seconds (2.0));
  
  NS_LOG_INFO ("Running simulation...");
  auto start = std::chrono::high_resolution_clock::now ();
  Simulator::Run ();
  auto end = std::chrono::high_resolution_clock::now ();
  
  printf ("\n=== Q-CAST Simulation Complete ===\n");
  printf ("Total time cost: %ld ms\n",
          std::chrono::duration_cast<std::chrono::milliseconds> (end - start).count ());
  
  Simulator::Destroy ();

  return 0;
}
