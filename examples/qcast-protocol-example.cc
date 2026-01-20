/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Q-CAST协议示例
 * 
 * 本示例实现Q-CAST (Contention-Free pAth Selection at runTime) 量子网络路由协议。
 * 协议基于论文提出的四阶段时隙模型，实现完全在线、无冲突的路径选择。
 * 
 * 四阶段模型：
 * P1: 通过经典互联网获知当前需要建立长距离纠缠的源-目的地对
 * P2: 运行一致的路由算法，为S-D对选择路径，并预留资源
 * P3: 交换k跳邻居的链路状态信息（k=3）
 * P4: 基于局部链路状态进行异或恢复决策和对数时间交换调度
 * 
 * 关键特性：
 * 1. 贪婪扩展Dijkstra算法 (G-EDA) 在线路径选择
 * 2. 异或恢复机制提高容错能力
 * 3. 对数时间交换调度减少纠缠保持时间
 * 4. 完全基于现有抽象接口，不修改基类
 */

#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-net-stack-helper.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-packet.h"
#include "ns3/quantum-metric.h"
#include "ns3/quantum-resource-manager.h"
#include "ns3/quantum-forwarding-engine.h"
#include "ns3/quantum-routing-protocol.h"
#include "../model/qcast/expected-throughput-metric.h"
#include "../model/qcast/qcast-route-types.h"
#include "../model/qcast/qcast-routing-protocol.h"
#include "../model/qcast/qcast-forwarding-engine.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/node-container.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "ns3/csma-module.h" // class CsmaHelper, NetDeviceContainer
#include "ns3/internet-module.h" // class InternetStackHelper, Ipv6AddressHelper, Ipv6InterfaceContainer

// 量子操作组件头文件
#include "ns3/distribute-epr-helper.h"
#include "ns3/distribute-epr-protocol.h"
#include "ns3/ent-swap-helper.h"
#include "ns3/ent-swap-app.h"

#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <algorithm>
#include <cmath>

NS_LOG_COMPONENT_DEFINE ("QCastProtocolExample");

using namespace ns3;

// ===========================================================================
// 主函数：Q-CAST协议演示
// ===========================================================================

int 
main (int argc, char *argv[])
{
  // 设置日志级别
  LogComponentEnable ("QCastProtocolExample", LOG_LEVEL_INFO);
  LogComponentEnable ("QCastRoutingProtocol", LOG_LEVEL_INFO);
  LogComponentEnable ("ExpectedThroughputMetric", LOG_LEVEL_INFO);
  LogComponentEnable ("QCastForwardingEngine", LOG_LEVEL_INFO);
  // 添加量子操作相关日志
  LogComponentEnable ("DistributeEPRProtocol", LOG_LEVEL_ALL);
  LogComponentEnable ("QuantumNetworkSimulator", LOG_LEVEL_ALL);
  LogComponentEnable ("QuantumPhyEntity", LOG_LEVEL_INFO);
  
  NS_LOG_INFO ("Starting Q-CAST Protocol Example");
  NS_LOG_INFO ("======================================");
  NS_LOG_INFO ("This example demonstrates the Q-CAST quantum routing protocol.");
  NS_LOG_INFO ("Key features:");
  NS_LOG_INFO ("  1. Greedy Extended Dijkstra Algorithm (G-EDA)");
  NS_LOG_INFO ("  2. 3-hop link state information exchange");
  NS_LOG_INFO ("  3. XOR recovery mechanism");
  NS_LOG_INFO ("  4. Log-time entanglement swap scheduling");
  NS_LOG_INFO ("======================================");
  
  // 创建量子物理实体
  std::vector<std::string> owners = {"NodeA", "NodeB", "NodeC", "NodeD", "NodeE"};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  // 创建量子节点（通过量子物理实体获取）
  NS_LOG_INFO ("Getting quantum nodes from quantum physical entity...");
  Ptr<QuantumNode> nodeA = qphyent->GetNode ("NodeA");
  Ptr<QuantumNode> nodeB = qphyent->GetNode ("NodeB");
  Ptr<QuantumNode> nodeC = qphyent->GetNode ("NodeC");
  Ptr<QuantumNode> nodeD = qphyent->GetNode ("NodeD");
  Ptr<QuantumNode> nodeE = qphyent->GetNode ("NodeE");
  
  if (!nodeA || !nodeB || !nodeC || !nodeD || !nodeE)
  {
    NS_LOG_ERROR ("Failed to get quantum nodes from quantum physical entity");
    return 1;
  }
  
  NS_LOG_INFO ("Quantum nodes obtained successfully");
  
   // 创建经典网络连接（为DistributeEPRProtocol提供socket支持）
   NS_LOG_INFO ("Creating classical network connections...");
   NodeContainer nodes;
   nodes.Add (nodeA);
   nodes.Add (nodeB);
   nodes.Add (nodeC);
   nodes.Add (nodeD);
   nodes.Add (nodeE);
   
   // 创建CSMA经典信道
   CsmaHelper csmaHelper;
   csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("1000kbps")));
   csmaHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (CLASSICAL_DELAY)));
   NetDeviceContainer devices = csmaHelper.Install (nodes);
   
   // 安装Internet栈并分配IPv6地址
   NS_LOG_INFO ("Installing Internet stack and IPv6 addresses...");
   InternetStackHelper stack;
   stack.Install (nodes);
   Ipv6AddressHelper address;
   address.SetBase ("2001:1::", Ipv6Prefix (64));
   Ipv6InterfaceContainer interfaces = address.Assign (devices);
   
   // 为量子物理实体设置IPv6地址
   unsigned rank = 0;
   for (const auto& owner : owners)
   {
     qphyent->SetOwnerAddress (owner, interfaces.GetAddress (rank, 1));
     qphyent->SetOwnerRank (owner, rank);
     ++rank;
   }
  
  // 创建量子通道（星型拓扑）
  NS_LOG_INFO ("Creating quantum channels...");
   Ptr<QuantumChannel> channelAB = CreateObject<QuantumChannel> ("NodeA", "NodeB");
   Ptr<QuantumChannel> channelBC = CreateObject<QuantumChannel> ("NodeB", "NodeC");
   Ptr<QuantumChannel> channelCD = CreateObject<QuantumChannel> ("NodeC", "NodeD");
   Ptr<QuantumChannel> channelDE = CreateObject<QuantumChannel> ("NodeD", "NodeE");
   // Ptr<QuantumChannel> channelAE = CreateObject<QuantumChannel> ("NodeA", "NodeE"); // 备用路径
   
   // 设置通道去极化模型（保真度）
   channelAB->SetDepolarModel (0.95, qphyent);  // 95% 保真度
   channelBC->SetDepolarModel (0.95, qphyent);
   channelCD->SetDepolarModel (0.95, qphyent);
   channelDE->SetDepolarModel (0.95, qphyent);
  
   // 安装量子网络栈
   NS_LOG_INFO ("Installing quantum network stack...");
   QuantumNetStackHelper qstack;
   qstack.Install (nodes);
  
  // =======================================================================
  // P1阶段：获取网络信息
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Phase P1: Network Discovery");
  NS_LOG_INFO ("---------------------------");
  
  // 获取节点A的网络层
  Ptr<QuantumNetworkLayer> networkLayerA = nodeA->GetQuantumNetworkLayer ();
  if (!networkLayerA)
  {
    NS_LOG_ERROR ("Failed to get quantum network layer for NodeA");
    return 1;
  }
  
  // Get network layers for all nodes
  Ptr<QuantumNetworkLayer> networkLayerB = nodeB->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> networkLayerC = nodeC->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> networkLayerD = nodeD->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> networkLayerE = nodeE->GetQuantumNetworkLayer ();
  
  if (!networkLayerB || !networkLayerC || !networkLayerD || !networkLayerE)
  {
    NS_LOG_ERROR ("Failed to get quantum network layer for all nodes");
    return 1;
  }
  
  // =======================================================================
  // 创建和配置Q-CAST组件
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Configuring Q-CAST Components");
  NS_LOG_INFO ("-----------------------------");
  
  // 创建期望吞吐量度量
  Ptr<ExpectedThroughputMetric> etMetric = CreateObject<ExpectedThroughputMetric> ();
  NS_LOG_INFO ("Created ExpectedThroughputMetric");
  
  // 创建Q-CAST路由协议（k=3）
  Ptr<QCastRoutingProtocol> qcastRouting = CreateObject<QCastRoutingProtocol> ();
  qcastRouting->SetMetric (etMetric);
  qcastRouting->SetKHopDistance (3);
  NS_LOG_INFO ("Created QCastRoutingProtocol with k=3");
  
  // 创建Q-CAST转发引擎
  Ptr<QCastForwardingEngine> qcastForwarding = CreateObject<QCastForwardingEngine> ();
  qcastForwarding->SetForwardingStrategy (QFS_ON_DEMAND);
  NS_LOG_INFO ("Created QCastForwardingEngine with on-demand strategy");
  
  // 获取默认资源管理器
  Ptr<QuantumResourceManager> resourceManager = QuantumResourceManager::GetDefaultResourceManager ();
  
  // 配置网络层
  networkLayerA->SetRoutingProtocol (qcastRouting);
  networkLayerA->SetForwardingEngine (qcastForwarding);
  networkLayerA->SetResourceManager (resourceManager);
  
  qcastRouting->SetNetworkLayer (networkLayerA);
  qcastRouting->SetResourceManager (resourceManager);
  
  qcastForwarding->SetResourceManager (resourceManager);
  
  NS_LOG_INFO ("Q-CAST components configured successfully");
  
  // Configure routing protocol for all nodes
  // NodeA already configured above
  // Create separate routing protocol instances for other nodes
  Ptr<QCastRoutingProtocol> qcastRoutingB = CreateObject<QCastRoutingProtocol> ();
  qcastRoutingB->SetMetric (etMetric);
  qcastRoutingB->SetKHopDistance (3);
  networkLayerB->SetRoutingProtocol (qcastRoutingB);
  networkLayerB->SetForwardingEngine (qcastForwarding); // Can share forwarding engine
  networkLayerB->SetResourceManager (resourceManager);
  qcastRoutingB->SetNetworkLayer (networkLayerB);
  qcastRoutingB->SetResourceManager (resourceManager);
  
  Ptr<QCastRoutingProtocol> qcastRoutingC = CreateObject<QCastRoutingProtocol> ();
  qcastRoutingC->SetMetric (etMetric);
  qcastRoutingC->SetKHopDistance (3);
  networkLayerC->SetRoutingProtocol (qcastRoutingC);
  networkLayerC->SetForwardingEngine (qcastForwarding);
  networkLayerC->SetResourceManager (resourceManager);
  qcastRoutingC->SetNetworkLayer (networkLayerC);
  qcastRoutingC->SetResourceManager (resourceManager);
  
  Ptr<QCastRoutingProtocol> qcastRoutingD = CreateObject<QCastRoutingProtocol> ();
  qcastRoutingD->SetMetric (etMetric);
  qcastRoutingD->SetKHopDistance (3);
  networkLayerD->SetRoutingProtocol (qcastRoutingD);
  networkLayerD->SetForwardingEngine (qcastForwarding);
  networkLayerD->SetResourceManager (resourceManager);
  qcastRoutingD->SetNetworkLayer (networkLayerD);
  qcastRoutingD->SetResourceManager (resourceManager);
  
  Ptr<QCastRoutingProtocol> qcastRoutingE = CreateObject<QCastRoutingProtocol> ();
  qcastRoutingE->SetMetric (etMetric);
  qcastRoutingE->SetKHopDistance (3);
  networkLayerE->SetRoutingProtocol (qcastRoutingE);
  networkLayerE->SetForwardingEngine (qcastForwarding);
  networkLayerE->SetResourceManager (resourceManager);
  qcastRoutingE->SetNetworkLayer (networkLayerE);
  qcastRoutingE->SetResourceManager (resourceManager);
  
  NS_LOG_INFO ("Routing protocols configured for all nodes");
  
  // =======================================================================
  // P2+P3阶段：邻居发现和链路状态交换
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Phase P2+P3: Neighbor Discovery and Link State Exchange");
  NS_LOG_INFO ("--------------------------------------------------------");
  
  // 执行邻居发现（同时进行链路状态交换）
  qcastRouting->DiscoverNeighbors ();
  qcastRoutingB->DiscoverNeighbors ();
  qcastRoutingC->DiscoverNeighbors ();
  qcastRoutingD->DiscoverNeighbors ();
  qcastRoutingE->DiscoverNeighbors ();
  
  // 获取邻居信息
  std::vector<Ptr<QuantumChannel>> neighbors = networkLayerA->GetNeighbors ();
  NS_LOG_INFO ("NodeA discovered " << neighbors.size () << " neighbors");
  for (const auto& neighbor : neighbors)
  {
    NS_LOG_INFO ("  Neighbor: " << (neighbor->GetSrcOwner () != "NodeA" ? 
                 neighbor->GetSrcOwner () : neighbor->GetDstOwner ()));
  }
  
  // =======================================================================
  // P2阶段：路由请求和资源预留
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Phase P2: Route Request and Resource Reservation");
  NS_LOG_INFO ("-------------------------------------------------");
  
  // 创建路由需求
  QuantumRouteRequirements requirements;
  requirements.minFidelity = 0.5;  // 降低保真度要求
  requirements.maxDelay = Seconds (10.0);  // 增加延迟容忍
  requirements.numQubits = 2;
  requirements.duration = Seconds (30.0);
  requirements.strategy = QFS_ON_DEMAND;
  
  NS_LOG_INFO ("Route requirements:");
  NS_LOG_INFO ("  Min fidelity: " << requirements.minFidelity);
  NS_LOG_INFO ("  Max delay: " << requirements.maxDelay.GetSeconds () << "s");
  NS_LOG_INFO ("  Num qubits: " << requirements.numQubits);
  NS_LOG_INFO ("  Duration: " << requirements.duration.GetSeconds () << "s");
  
   // 执行路由请求（从NodeA到NodeE）
   NS_LOG_INFO ("Requesting route from NodeA to NodeE...");
   QuantumRoute route = qcastRouting->RouteRequest ("NodeA", "NodeE", requirements);
   
   NS_LOG_INFO ("Route path size after request: " << route.path.size ());
   NS_LOG_INFO ("Route IsValid: " << (route.IsValid () ? "true" : "false"));
   
   if (route.IsValid ())
  {
    NS_LOG_INFO ("Route found successfully!");
    NS_LOG_INFO ("  Path: " << route.ToString ());
    NS_LOG_INFO ("  Hops: " << route.GetHopCount ());
    NS_LOG_INFO ("  Cost: " << route.totalCost);
    NS_LOG_INFO ("  Estimated fidelity: " << route.estimatedFidelity);
    NS_LOG_INFO ("  Estimated delay: " << route.estimatedDelay.GetSeconds () << "s");
    
    // 获取Q-CAST路由详细信息
    QCastRouteInfo qcastInfo = qcastRouting->GetQCastRouteInfo (route.routeId);
    if (qcastInfo.IsValid ())
    {
      NS_LOG_INFO ("  Recovery paths: " << qcastInfo.recoveryPaths.size ());
      NS_LOG_INFO ("  Success probability: " << qcastInfo.successProbability);
    }
  }
  else
  {
    NS_LOG_WARN ("No route found from NodeA to NodeE");
  }
  
  // =======================================================================
  // P4阶段：纠缠建立和交换
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Phase P4: Entanglement Establishment and Swapping");
  NS_LOG_INFO ("--------------------------------------------------");
  
  // 始终使用手动路径进行量子操作演示（A->B->C->D->E）
  QuantumRoute effectiveRoute;
  effectiveRoute.source = "NodeA";
  effectiveRoute.destination = "NodeE";
  effectiveRoute.path.push_back (channelAB);
  effectiveRoute.path.push_back (channelBC);
  effectiveRoute.path.push_back (channelCD);
  effectiveRoute.path.push_back (channelDE);
  effectiveRoute.totalCost = 0.5;
  effectiveRoute.estimatedFidelity = 0.9;
  effectiveRoute.estimatedDelay = Seconds (0.1);
  effectiveRoute.strategy = QFS_ON_DEMAND;
  effectiveRoute.expirationTime = Simulator::Now () + Seconds (30);
  effectiveRoute.routeId = 999;
  NS_LOG_INFO ("Using manual path for quantum operations demo with " << effectiveRoute.path.size () << " hops");
  for (size_t i = 0; i < effectiveRoute.path.size (); ++i)
  {
    NS_LOG_INFO ("  Channel " << i << ": " << effectiveRoute.path[i]->GetSrcOwner () 
                 << " -> " << effectiveRoute.path[i]->GetDstOwner ());
  }
  
   if (effectiveRoute.IsValid ())
   {
     NS_LOG_INFO ("Effective route info: " << effectiveRoute.ToString ());
     NS_LOG_INFO ("Effective route path size: " << effectiveRoute.path.size ());
     // 创建量子数据包
    Ptr<QuantumPacket> packet = CreateObject<QuantumPacket> ("NodeA", "NodeE");
    packet->SetType (QuantumPacket::DATA);
    packet->SetProtocol (QuantumPacket::PROTO_QUANTUM_FORWARDING);
    packet->SetRoute (effectiveRoute);
    
    // 添加量子比特引用
    packet->AddQubitReference ("QubitA1");
    packet->AddQubitReference ("QubitA2");
    
    NS_LOG_INFO ("Created quantum packet:");
    NS_LOG_INFO ("  Source: " << packet->GetSourceAddress ());
    NS_LOG_INFO ("  Destination: " << packet->GetDestinationAddress ());
    NS_LOG_INFO ("  Qubits: " << packet->GetQubitReferences ().size ());
    
    // 转发数据包（触发P4阶段）
    NS_LOG_INFO ("Forwarding packet (triggers P4 phase)...");
    bool sent = networkLayerA->SendPacket (packet);
    
    if (sent)
    {
      NS_LOG_INFO ("Packet forwarded successfully!");
    }
    else
    {
      NS_LOG_WARN ("Failed to forward packet");
    }
    
    // =======================================================================
    // 集成实际量子操作：EPR分发和纠缠交换
    // =======================================================================
    NS_LOG_INFO ("");
    NS_LOG_INFO ("Installing actual quantum operations...");
    NS_LOG_INFO ("Path: " << effectiveRoute.ToString ());
    
    // 提取路径节点序列
    std::vector<std::string> nodeSequence;
    if (!effectiveRoute.path.empty ())
    {
      // 添加源节点
      nodeSequence.push_back (effectiveRoute.source);
      
      // 对于每个通道，添加目标节点
      for (const auto& channel : effectiveRoute.path)
      {
        nodeSequence.push_back (channel->GetDstOwner ());
      }
    }
    
    NS_LOG_INFO ("Node sequence: ");
    for (size_t i = 0; i < nodeSequence.size (); ++i)
    {
      NS_LOG_INFO ("  " << i << ": " << nodeSequence[i]);
    }
    
    // 检查节点序列长度
    if (nodeSequence.size () >= 2)
    {
      NS_LOG_INFO ("Setting up EPR distribution and entanglement swapping...");
      
      // 创建节点对到现有通道的映射
      std::map<std::pair<std::string, std::string>, Ptr<QuantumChannel>> channelMap;
      channelMap[{"NodeA", "NodeB"}] = channelAB;
      channelMap[{"NodeB", "NodeC"}] = channelBC;
      channelMap[{"NodeC", "NodeD"}] = channelCD;
      channelMap[{"NodeD", "NodeE"}] = channelDE;
      // 反向映射
      channelMap[{"NodeB", "NodeA"}] = channelAB;
      channelMap[{"NodeC", "NodeB"}] = channelBC;
      channelMap[{"NodeD", "NodeC"}] = channelCD;
      channelMap[{"NodeE", "NodeD"}] = channelDE;
      
      // 为每一对相邻节点分发EPR对
      for (size_t i = 0; i < nodeSequence.size () - 1; ++i)
      {
        std::string srcOwner = nodeSequence[i];
        std::string dstOwner = nodeSequence[i + 1];
        
        NS_LOG_INFO ("  EPR pair between " << srcOwner << " and " << dstOwner);
        
        // 获取现有量子通道
        Ptr<QuantumChannel> qconn = nullptr;
        auto it = channelMap.find ({srcOwner, dstOwner});
        if (it != channelMap.end ())
        {
          qconn = it->second;
          NS_LOG_INFO ("    Using existing channel: " << qconn->GetSrcOwner () << " -> " << qconn->GetDstOwner ());
        }
        else
        {
          NS_LOG_WARN ("    No existing channel found for " << srcOwner << " -> " << dstOwner);
          continue;
        }
        
        // 获取源节点和目标节点
        Ptr<QuantumNode> srcNode = qphyent->GetNode (srcOwner);
        Ptr<QuantumNode> dstNode = qphyent->GetNode (dstOwner);
        
        if (!srcNode || !dstNode)
        {
          NS_LOG_WARN ("    Could not find nodes for " << srcOwner << " or " << dstOwner);
          continue;
        }
        
        // 安装DistributeEPR协议
        // 源端应用
        NS_LOG_INFO ("    Getting DistributeEPR apps for channel " << qconn->GetSrcOwner () << " -> " << qconn->GetDstOwner ());
        auto apps = qphyent->GetConn2Apps (qconn, APP_DIST_EPR);
        NS_LOG_INFO ("    Got apps pair, first: " << apps.first << ", second: " << apps.second);
        
        if (!apps.first)
        {
          NS_LOG_WARN ("    apps.first is nullptr! Cannot get DistributeEPRSrcProtocol");
          // 尝试列出所有可用的应用类型
          NS_LOG_WARN ("    Available app types for this connection:");
          // 这里可以添加更多调试信息
          continue;
        }
        
        NS_LOG_INFO ("    apps.first type: " << apps.first->GetInstanceTypeId ().GetName ());
        NS_LOG_INFO ("    apps.second type: " << (apps.second ? apps.second->GetInstanceTypeId ().GetName () : "nullptr"));
        
        Ptr<DistributeEPRSrcProtocol> dist_epr_src_app = apps.first->GetObject<DistributeEPRSrcProtocol> ();
        
        if (!dist_epr_src_app)
        {
          NS_LOG_WARN ("    Failed to cast apps.first to DistributeEPRSrcProtocol. Type: " << apps.first->GetInstanceTypeId ().GetName ());
          continue;
        }
        
        NS_LOG_INFO ("    Successfully got DistributeEPRSrcProtocol at " << dist_epr_src_app);
        NS_LOG_INFO ("    Scheduling EPR distribution at time " << CLASSICAL_DELAY << "s");
        NS_LOG_INFO ("    Qubit names: " << srcOwner + "_QubitEntTo" + dstOwner << " and " << dstOwner + "_QubitEntFrom" + srcOwner);
        
        // 安排EPR分发
        Simulator::Schedule (Seconds (CLASSICAL_DELAY), 
                             &DistributeEPRSrcProtocol::GenerateAndDistributeEPR,
                             dist_epr_src_app, 
                             std::pair<std::string, std::string>{
                               srcOwner + "_QubitEntTo" + dstOwner,
                               dstOwner + "_QubitEntFrom" + srcOwner});
        NS_LOG_INFO ("    EPR distribution scheduled");
      }
      
      // 设置纠缠交换（如果路径长度>2）
      if (nodeSequence.size () > 2)
      {
        std::string dstOwner = nodeSequence.back (); // 目的节点
        NS_LOG_INFO ("Setting up entanglement swapping for destination: " << dstOwner);
        
        // 为中间节点（除了第一个和最后一个）创建纠缠交换
        // 只测试第一个中间节点（NodeB）
        size_t rank = 1;
        std::string srcOwner = nodeSequence[rank]; // NodeB
        
        NS_LOG_INFO ("  Installing entanglement swap at node: " << srcOwner);
        
        // 创建量子通道（用于通信）
        Ptr<QuantumChannel> qconn = CreateObject<QuantumChannel> (srcOwner, dstOwner);
        
        // 安装纠缠交换源应用
        EntSwapSrcHelper srcHelper (qphyent, qconn);
        srcHelper.SetAttribute ("Qubits", PairValue<StringValue, StringValue> ({
                                  srcOwner + "_QubitEntFrom" + nodeSequence[rank - 1],
                                  srcOwner + "_QubitEntTo" + nodeSequence[rank + 1]
                                  }));
        
        ApplicationContainer srcApps = srcHelper.Install (qphyent->GetNode (srcOwner));
        Ptr<EntSwapSrcApp> telepSrcApp = srcApps.Get (0)->GetObject<EntSwapSrcApp> ();
        
        // 安排纠缠交换
        telepSrcApp->SetStartTime (Seconds (TELEP_DELAY * rank));
        telepSrcApp->SetStopTime (Seconds (TELEP_DELAY * (rank + 1)));
        
        NS_LOG_INFO ("    Entanglement swap scheduled at time " << (TELEP_DELAY * rank) << "s");
        
        // 安装目的节点应用
        std::string lastIntermediate = nodeSequence[nodeSequence.size () - 2];
        NS_LOG_INFO ("  Installing EntSwapDstApp for destination " << dstOwner << " with qubit " << dstOwner + "_QubitEntFrom" + lastIntermediate);
        EntSwapDstHelper dstHelper (qphyent, qphyent->GetNode (dstOwner));
        dstHelper.SetAttribute ("Qubit", StringValue (dstOwner + "_QubitEntFrom" + lastIntermediate));
        dstHelper.SetAttribute ("Count", UintegerValue (nodeSequence.size () - 2));
        
        ApplicationContainer dstApps = dstHelper.Install (qphyent->GetNode (dstOwner));
        Ptr<EntSwapDstApp> dstApp = dstApps.Get (0)->GetObject<EntSwapDstApp> ();
        
        dstApp->SetStartTime (Seconds (CLASSICAL_DELAY));
        dstApp->SetStopTime (Seconds (TELEP_DELAY * (nodeSequence.size () - 1)));
        
        NS_LOG_INFO ("  Destination app installed for " << dstOwner);
      }
      else
      {
        NS_LOG_INFO ("  Direct link, no entanglement swapping needed");
      }
    }
    else
    {
      NS_LOG_WARN ("  Invalid node sequence for quantum operations");
    }
    
    NS_LOG_INFO ("Quantum operations installation completed");
  }
  
  // =======================================================================
  // 性能统计
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Performance Statistics");
  NS_LOG_INFO ("----------------------");
  
  // 获取路由协议统计
  QuantumNetworkStats routingStats = qcastRouting->GetStatistics ();
  NS_LOG_INFO ("Routing Protocol Statistics:");
  NS_LOG_INFO ("  Route requests: " << routingStats.routeRequests);
  NS_LOG_INFO ("  Route replies: " << routingStats.routeReplies);
  NS_LOG_INFO ("  Packets sent: " << routingStats.packetsSent);
  NS_LOG_INFO ("  Packets received: " << routingStats.packetsReceived);
  
  // 获取转发引擎统计
  QuantumNetworkStats forwardingStats = qcastForwarding->GetStatistics ();
  NS_LOG_INFO ("Forwarding Engine Statistics:");
  NS_LOG_INFO ("  Packets forwarded: " << forwardingStats.packetsForwarded);
  NS_LOG_INFO ("  Entanglement swaps: " << forwardingStats.entanglementSwaps);
  NS_LOG_INFO ("  EPR pairs distributed: " << forwardingStats.eprPairsDistributed);
  
  // =======================================================================
  // 资源使用情况
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Resource Utilization");
  NS_LOG_INFO ("--------------------");
  
  // 检查节点资源
  NS_LOG_INFO ("Node resource availability:");
  for (const auto& nodeName : owners)
  {
    unsigned availableQubits = resourceManager->GetAvailableQubits (nodeName);
    unsigned totalQubits = resourceManager->GetTotalQubitCapacity (nodeName);
    double utilization = resourceManager->GetMemoryUtilization (nodeName);
    
    NS_LOG_INFO ("  " << nodeName << ": " << availableQubits << "/" << totalQubits 
                 << " qubits available (" << (utilization * 100) << "% utilized)");
  }
  
  // =======================================================================
  // 运行模拟
  // =======================================================================
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Running simulation...");
  
  // 运行模拟30秒以确保量子操作完成
  Simulator::Stop (Seconds (30.0));
  Simulator::Run ();
  Simulator::Destroy ();
  
  NS_LOG_INFO ("");
  NS_LOG_INFO ("======================================");
  NS_LOG_INFO ("Q-CAST Protocol Example Completed");
  NS_LOG_INFO ("======================================");
  
  return 0;
}