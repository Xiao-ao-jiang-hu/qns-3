/*
  Example demonstrating quantum network layer functionality with route discovery.
  To run this example:
  NS_LOG="QuantumNetworkLayer=info:QuantumNetworkSimulator=info" ./ns3 run quantum-network-layer-example
*/
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h" // class QuantumNetworkSimulator
#include "ns3/quantum-phy-entity.h" // class QuantumPhyEntity
#include "ns3/quantum-node.h" // class QuantumNode
#include "ns3/quantum-channel.h" // class QuantumChannel
#include "ns3/quantum-net-stack-helper.h" // class QuantumNetStackHelper
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-packet.h"
#include "ns3/quantum-metric.h"
#include "ns3/quantum-resource-manager.h"
#include "ns3/quantum-forwarding-engine.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/node-container.h"

#include <iostream>

NS_LOG_COMPONENT_DEFINE ("QuantumNetworkLayerExample");

using namespace ns3;

// ===========================================================================
// 路由发现函数
// ===========================================================================

/**
 * \brief 执行路由发现并添加路由
 * 
 * 模拟完整的路由发现过程：
 * 1. 发送路由请求包
 * 2. 中间节点转发
 * 3. 目的地回复
 * 4. 源节点添加路由
 */
void PerformRouteDiscovery(Ptr<QuantumRoutingProtocol> srcRouting,
                          Ptr<QuantumChannel> firstHopChannel,
                          Ptr<QuantumChannel> secondHopChannel,
                          const std::string& srcAddress,
                          const std::string& dstAddress)
{
  NS_LOG_INFO("Performing route discovery from " << srcAddress << " to " << dstAddress);
  
  // 模拟路由请求发送
  NS_LOG_INFO("Sending ROUTE_REQUEST from " << srcAddress << " to " << dstAddress);
  
  // 模拟中间节点转发（Bob）
  NS_LOG_INFO("Node Bob forwards ROUTE_REQUEST to " << dstAddress);
  
  // 模拟目的地回复（Charlie）
  NS_LOG_INFO("Node Charlie sends ROUTE_REPLY back to " << srcAddress);
  
  // 创建路由条目（模拟从路由回复中提取的信息）
  QuantumRouteEntry entry;
  entry.destination = dstAddress;
  entry.nextHop = "Bob"; // 通过Bob到达Charlie
  entry.channel = firstHopChannel;
  entry.cost = 0.1; // Alice->Bob (0.05) + Bob->Charlie (0.05)
  entry.timestamp = Simulator::Now();
  entry.sequenceNumber = 100;
  
  // 添加到源节点的路由表
  if (srcRouting->AddRoute(entry))
  {
    NS_LOG_INFO("Route to " << dstAddress << " added via route discovery");
    NS_LOG_INFO("  Next hop: " << entry.nextHop);
    NS_LOG_INFO("  Cost: " << entry.cost);
  }
  else
  {
    NS_LOG_WARN("Failed to add route via route discovery");
  }
}

int 
main (int argc, char *argv[])
{
  // 设置日志级别
  LogComponentEnable ("QuantumNetworkLayer", LOG_LEVEL_INFO);
  LogComponentEnable ("QuantumNetworkLayerExample", LOG_LEVEL_INFO);
  
  NS_LOG_INFO ("Starting quantum network layer example with route discovery");
  
  // 创建量子物理实体
  std::vector<std::string> owners = {"Alice", "Bob", "Charlie"};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  // 创建量子节点
  Ptr<QuantumNode> alice = CreateObject<QuantumNode> (qphyent, "Alice");
  Ptr<QuantumNode> bob = CreateObject<QuantumNode> (qphyent, "Bob");
  Ptr<QuantumNode> charlie = CreateObject<QuantumNode> (qphyent, "Charlie");

  // 安装Internet栈（为DistributeEPRProtocol提供socket支持）
  InternetStackHelper internet;
  NodeContainer nodes;
  nodes.Add (alice);
  nodes.Add (bob);
  nodes.Add (charlie);
  internet.Install (nodes);

  // 创建量子通道
  Ptr<QuantumChannel> aliceBobChannel = CreateObject<QuantumChannel> ("Alice", "Bob");
  Ptr<QuantumChannel> bobCharlieChannel = CreateObject<QuantumChannel> ("Bob", "Charlie");
  
  // 安装量子网络栈（包括网络层）
  QuantumNetStackHelper netStackHelper;
  netStackHelper.Install (alice, bob);
  netStackHelper.Install (bob, charlie);
  
  // 获取网络层
  Ptr<QuantumNetworkLayer> aliceLayer = alice->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> bobLayer = bob->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> charlieLayer = charlie->GetQuantumNetworkLayer ();
  
  if (!aliceLayer || !bobLayer || !charlieLayer)
  {
    NS_LOG_ERROR ("Failed to install quantum network layer");
    return 1;
  }
  
  NS_LOG_INFO ("Quantum network layers installed successfully");
  
  // 演示路由协议邻居发现
  Ptr<QuantumRoutingProtocol> aliceRouting = aliceLayer->GetRoutingProtocol ();
  Ptr<QuantumRoutingProtocol> bobRouting = bobLayer->GetRoutingProtocol ();
  Ptr<QuantumRoutingProtocol> charlieRouting = charlieLayer->GetRoutingProtocol ();
  
  if (aliceRouting)
  {
    aliceRouting->DiscoverNeighbors ();
    NS_LOG_INFO ("Alice discovered neighbors");
    
    // 获取路由表
    std::vector<QuantumRouteEntry> routes = aliceRouting->GetAllRoutes ();
    NS_LOG_INFO ("Alice routing table size: " << routes.size ());
    for (const auto& route : routes)
    {
      NS_LOG_INFO ("  Destination: " << route.destination << ", Next hop: " << route.nextHop 
                    << ", Cost: " << route.cost);
    }
  }
  
  // =======================================================================
  // 路由发现阶段
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Route Discovery Phase");
  NS_LOG_INFO ("=====================");
  
  // 执行路由发现（模拟完整过程）
  if (aliceRouting && aliceBobChannel && bobCharlieChannel)
  {
    PerformRouteDiscovery(aliceRouting, aliceBobChannel, bobCharlieChannel, "Alice", "Charlie");
  }
  
  // 检查路由发现结果
  NS_LOG_INFO("");
  NS_LOG_INFO("Route Discovery Complete");
  NS_LOG_INFO("========================");
  
  if (aliceRouting)
  {
    std::vector<QuantumRouteEntry> routes = aliceRouting->GetAllRoutes ();
    NS_LOG_INFO ("Alice routing table size after discovery: " << routes.size ());
    for (const auto& route : routes)
    {
      NS_LOG_INFO ("  Destination: " << route.destination << ", Next hop: " << route.nextHop 
                    << ", Cost: " << route.cost);
    }
    
    // 检查是否找到了到Charlie的路由
    if (aliceRouting->HasRoute("Charlie"))
    {
      NS_LOG_INFO("Successfully discovered route to Charlie!");
      
      // 演示创建和发送量子包
      Ptr<QuantumPacket> packet = CreateObject<QuantumPacket> ();
      packet->SetSourceAddress ("Alice");
      packet->SetDestinationAddress ("Charlie");
      packet->SetType (QuantumPacket::DATA);
      packet->SetProtocol (QuantumPacket::PROTO_QUANTUM_FORWARDING);
      
      // 添加虚拟量子比特引用
      packet->AddQubitReference ("Qubit1");
      packet->AddQubitReference ("Qubit2");
      
      NS_LOG_INFO ("Created packet: " << packet->ToString ());
      NS_LOG_INFO ("  Source: " << packet->GetSourceAddress () << ", Destination: " << packet->GetDestinationAddress ());
      
      // 使用路由协议获取路由（从路由表）
      QuantumRoute route = aliceRouting->GetRoute ("Charlie");
      if (route.IsValid ())
      {
        NS_LOG_INFO ("Found route from Alice to Charlie via routing table:");
        NS_LOG_INFO ("  Hops: " << route.GetHopCount ());
        NS_LOG_INFO ("  Cost: " << route.totalCost);
        NS_LOG_INFO ("  Estimated fidelity: " << route.estimatedFidelity);
        NS_LOG_INFO ("  Estimated delay: " << route.estimatedDelay.GetSeconds () << "s");
        
        // 设置包的路由
        packet->SetRoute (route);
        NS_LOG_INFO ("Packet route set successfully");
        
        // 演示发送包
        bool sent = aliceLayer->SendPacket (packet);
        if (sent)
        {
          NS_LOG_INFO ("Packet sent successfully from Alice");
        }
        else
        {
          NS_LOG_WARN ("Failed to send packet from Alice");
        }
      }
      else
      {
        NS_LOG_WARN ("GetRoute failed even though route exists in table");
      }
    }
    else
    {
      NS_LOG_WARN ("Route discovery failed - no route to Charlie found");
    }
  }
  
  // 演示资源管理
  Simulator::Schedule(Seconds(0.3), [=]() {
    NS_LOG_INFO("");
    NS_LOG_INFO("Resource Management Demo");
    NS_LOG_INFO("========================");
    
    Ptr<QuantumResourceManager> resourceManager = aliceLayer->GetResourceManager ();
    if (resourceManager)
    {
      unsigned availableQubits = resourceManager->GetAvailableQubits ("Alice");
      NS_LOG_INFO ("Alice available qubits: " << availableQubits);
      
      // 预约资源
      bool reserved = resourceManager->ReserveQubits ("Alice", 2, Seconds (5.0));
      if (reserved)
      {
        NS_LOG_INFO ("Reserved 2 qubits on Alice for 5 seconds");
        availableQubits = resourceManager->GetAvailableQubits ("Alice");
        NS_LOG_INFO ("Alice available qubits after reservation: " << availableQubits);
      }
    }
  });
  
  // 演示度量系统
  Simulator::Schedule(Seconds(0.4), [=]() {
    NS_LOG_INFO("");
    NS_LOG_INFO("Metric System Demo");
    NS_LOG_INFO("==================");
    
    Ptr<QuantumMetric> fidelityMetric = QuantumMetric::CreateFidelityMetric ();
    Ptr<QuantumMetric> delayMetric = QuantumMetric::CreateDelayMetric ();
    
    std::vector<Ptr<QuantumMetric>> metrics = {fidelityMetric, delayMetric};
    std::vector<double> weights = {0.7, 0.3};
    Ptr<QuantumMetric> compositeMetric = QuantumMetric::CreateCompositeMetric (metrics, weights);
    
    NS_LOG_INFO ("Created composite metric: " << compositeMetric->GetName ());
    NS_LOG_INFO ("  Is additive: " << (compositeMetric->IsAdditive () ? "yes" : "no"));
    NS_LOG_INFO ("  Is monotonic: " << (compositeMetric->IsMonotonic () ? "yes" : "no"));
  });
  
  // 模拟运行
  Simulator::Stop (Seconds (1.0));
  Simulator::Run ();
  Simulator::Destroy ();
  
  NS_LOG_INFO ("Quantum network layer example completed successfully");
  
  return 0;
}