/*
 * Q-CAST协议规模扩展测试（极简版）
 * 
 * 本测试评估Q-CAST量子网络路由协议在不同规模网络下的性能。
 * 基于qcast-performance-test.cc简化
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
#include "ns3/command-line.h"
#include "ns3/nstime.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE ("QCastScalingTestSimple");

using namespace ns3;

// ===========================================================================
// 测试运行函数
// ===========================================================================

void RunSingleTest(const std::string& testName,
                  const std::string& topologyType,
                  int numNodes,
                  double linkFidelity,
                  int numRequests,
                  std::ofstream& outputStream)
{
  std::cout << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "测试: " << testName << std::endl;
  std::cout << "拓扑: " << topologyType << std::endl;
  std::cout << "节点: " << numNodes << std::endl;
  std::cout << "保真度: " << linkFidelity << std::endl;
  std::cout << "请求数: " << numRequests << std::endl;
  std::cout << "========================================" << std::endl;
  
  // 创建owner列表
  std::vector<std::string> owners;
  if (topologyType == "chain" || topologyType == "random")
  {
    for (int i = 0; i < numNodes; ++i)
    {
      owners.push_back("Node" + std::to_string(i));
    }
  }
  else if (topologyType == "grid")
  {
    int rows = (int)std::sqrt(numNodes);
    int cols = (numNodes + rows - 1) / rows;
    for (int r = 0; r < rows; ++r)
    {
      for (int c = 0; c < cols; ++c)
      {
        if (owners.size() < (size_t)numNodes)
        {
          owners.push_back("Node" + std::to_string(r) + "_" + std::to_string(c));
        }
      }
    }
  }
  
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
  
  // 创建节点
  std::vector<Ptr<QuantumNode>> nodes;
  for (const auto& owner : owners)
  {
    nodes.push_back(qphyent->GetNode(owner));
  }
  
  // 创建经典网络连接
  NodeContainer nodeContainer;
  for (const auto& node : nodes)
  {
    nodeContainer.Add(node);
  }
  
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("1000kbps")));
  csmaHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(CLASSICAL_DELAY)));
  NetDeviceContainer devices = csmaHelper.Install(nodeContainer);
  
  InternetStackHelper internet;
  internet.Install(nodeContainer);
  
  Ipv6AddressHelper address;
  address.SetBase("2001:1::", Ipv6Prefix(64));
  Ipv6InterfaceContainer interfaces = address.Assign(devices);
  
  // 为量子物理实体设置IPv6地址
  unsigned rank = 0;
  for (const auto& owner : owners)
  {
    qphyent->SetOwnerAddress(owner, interfaces.GetAddress(rank, 1));
    qphyent->SetOwnerRank(owner, rank);
    ++rank;
  }
  
  // 安装量子网络栈
  QuantumNetStackHelper qstack;
  qstack.Install(nodeContainer);
  
  // 创建量子信道
  if (topologyType == "chain")
  {
    for (size_t i = 0; i < owners.size() - 1; ++i)
    {
      Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[i], owners[i+1]);
      channel->SetDepolarModel(linkFidelity, qphyent);
      std::cout << "创建量子信道: " << owners[i] << " <-> " << owners[i+1] << std::endl;
    }
  }
  else if (topologyType == "grid")
  {
    int rows = (int)std::sqrt(numNodes);
    int cols = (numNodes + rows - 1) / rows;
    for (int r = 0; r < rows; ++r)
    {
      for (int c = 0; c < cols; ++c)
      {
        int idx = r * cols + c;
        if (idx >= (int)owners.size()) break;
        // 右邻居
        if (c + 1 < cols)
        {
          int rightIdx = r * cols + (c + 1);
          if (rightIdx < (int)owners.size())
          {
            Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[idx], owners[rightIdx]);
            channel->SetDepolarModel(linkFidelity, qphyent);
          }
        }
        // 下邻居
        if (r + 1 < rows)
        {
          int downIdx = (r + 1) * cols + c;
          if (downIdx < (int)owners.size())
          {
            Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[idx], owners[downIdx]);
            channel->SetDepolarModel(linkFidelity, qphyent);
          }
        }
      }
    }
  }
  else if (topologyType == "random")
  {
    // 随机拓扑：每个节点随机连接2-4个其他节点
    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    for (size_t i = 0; i < owners.size(); ++i)
    {
      int numConnections = rand->GetInteger(2, 4);
      for (int conn = 0; conn < numConnections; ++conn)
      {
        size_t j = rand->GetInteger(0, owners.size() - 1);
        if (j != i)
        {
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[i], owners[j]);
          channel->SetDepolarModel(linkFidelity, qphyent);
        }
      }
    }
  }
  
  // 创建期望吞吐量度量
  Ptr<ExpectedThroughputMetric> etMetric = CreateObject<ExpectedThroughputMetric>();
  etMetric->SetLinkSuccessRate(linkFidelity);
  
  // 创建Q-CAST路由协议
  Ptr<QCastRoutingProtocol> qcastRouting = CreateObject<QCastRoutingProtocol>();
  qcastRouting->SetMetric(etMetric);
  qcastRouting->SetKHopDistance(3);
  
  // 创建Q-CAST转发引擎
  Ptr<QCastForwardingEngine> qcastForwarding = CreateObject<QCastForwardingEngine>();
  qcastForwarding->SetForwardingStrategy(QFS_ON_DEMAND);
  
  // 获取资源管理器
  Ptr<QuantumResourceManager> resourceManager = QuantumResourceManager::GetDefaultResourceManager();
  
  // 为所有节点配置网络层
  for (auto& node : nodes)
  {
    Ptr<QuantumNetworkLayer> networkLayer = node->GetQuantumNetworkLayer();
    if (networkLayer)
    {
      networkLayer->SetRoutingProtocol(qcastRouting);
      networkLayer->SetForwardingEngine(qcastForwarding);
      networkLayer->SetResourceManager(resourceManager);
    }
  }
  
  qcastRouting->SetResourceManager(resourceManager);
  qcastForwarding->SetResourceManager(resourceManager);
  
  // 执行邻居发现
  std::cout << "执行邻居发现..." << std::endl;
  qcastRouting->DiscoverNeighbors();
   std::cout << "邻居发现完成" << std::endl;
   
   // 调试：打印每个节点的邻居信息
   std::cout << "节点邻居信息：" << std::endl;
   for (size_t i = 0; i < nodes.size(); ++i)
   {
     Ptr<QuantumNetworkLayer> networkLayer = nodes[i]->GetQuantumNetworkLayer();
     if (networkLayer)
     {
       std::vector<Ptr<QuantumChannel>> neighbors = networkLayer->GetNeighbors();
       std::cout << "  Node" << i << " 有 " << neighbors.size() << " 个邻居" << std::endl;
     }
   }
   
   // 创建路由需求
  QuantumRouteRequirements requirements;
  requirements.minFidelity = 0.7;
  requirements.maxDelay = Seconds(1.0);
  requirements.numQubits = 2;
  requirements.duration = Seconds(30.0);
  requirements.strategy = QFS_ON_DEMAND;
  
  // 性能统计
  int successfulRequests = 0;
  double totalRouteTime = 0.0;
  double totalPathLength = 0.0;
  
  Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable>();
  
  // 安排请求
  for (int reqId = 0; reqId < numRequests; ++reqId)
  {
    // 确定请求时间
    double requestTime = 0.1 + (reqId * 4.9) / numRequests;
    
    // 随机选择源和目的节点
    int srcIdx = randVar->GetInteger(0, nodes.size() - 1);
    int dstIdx;
    do {
      dstIdx = randVar->GetInteger(0, nodes.size() - 1);
    } while (dstIdx == srcIdx);
    
    Ptr<QuantumNode> srcNode = nodes[srcIdx];
    Ptr<QuantumNode> dstNode = nodes[dstIdx];
    
    Simulator::Schedule(Seconds(requestTime), [=, &successfulRequests, &totalRouteTime, &totalPathLength]() {
      
       Ptr<QuantumNetworkLayer> networkLayer = srcNode->GetQuantumNetworkLayer();
       if (!networkLayer)
       {
         return;
       }
       
       // 调试：打印路由请求信息
       std::cout << "路由请求: 节点" << srcIdx << " -> 节点" << dstIdx;
       
       // 执行路由请求
      Time startTime = Simulator::Now();
      QuantumRoute route = qcastRouting->RouteRequest(
        srcNode->GetQuantumNetworkLayer()->GetAddress(),
        dstNode->GetQuantumNetworkLayer()->GetAddress(),
        requirements);
      Time routeTime = Simulator::Now() - startTime;
      
       // 调试：打印路由结果
       bool valid = route.IsValid();
       std::cout << " -> " << (valid ? "成功" : "失败") << " (跳数: " << route.GetHopCount() << ")" << std::endl;
       
       if (valid)
       {
         successfulRequests++;
         totalRouteTime += routeTime.GetSeconds() * 1000;
         totalPathLength += route.GetHopCount();
        
        // 发送测试包
        Ptr<QuantumPacket> packet = CreateObject<QuantumPacket>(
          srcNode->GetQuantumNetworkLayer()->GetAddress(),
          dstNode->GetQuantumNetworkLayer()->GetAddress());
        packet->SetType(QuantumPacket::DATA);
        packet->SetProtocol(QuantumPacket::PROTO_QUANTUM_FORWARDING);
        packet->SetRoute(route);
        packet->AddQubitReference("TestQubit1");
        packet->AddQubitReference("TestQubit2");
        
        networkLayer->SendPacket(packet);
      }
    });
  }
  
  // 运行模拟
  Simulator::Stop(Seconds(7.0));
  Simulator::Run();
  
  // 收集统计信息
  QuantumNetworkStats routingStats = qcastRouting->GetStatistics();
  QuantumNetworkStats forwardingStats = qcastForwarding->GetStatistics();
  
  // 计算性能指标
  double successRate = numRequests > 0 ? (double)successfulRequests / numRequests : 0.0;
  double avgRouteTimeMs = successfulRequests > 0 ? totalRouteTime / successfulRequests : 0.0;
  double avgPathLength = successfulRequests > 0 ? totalPathLength / successfulRequests : 0.0;
  
  // 输出结果
  std::cout << "测试完成: " << testName << std::endl;
  std::cout << "成功请求: " << successfulRequests << "/" << numRequests 
            << " (" << successRate * 100 << "%)" << std::endl;
  std::cout << "平均路由时间: " << avgRouteTimeMs << " ms" << std::endl;
  std::cout << "平均路径长度: " << avgPathLength << " hops" << std::endl;
  std::cout << "路由包数: " << routingStats.packetsSent + routingStats.packetsReceived << std::endl;
  std::cout << "转发包数: " << forwardingStats.packetsForwarded << std::endl;
  
  // 写入CSV
  outputStream << testName << ","
               << topologyType << ","
               << numNodes << ","
               << linkFidelity << ","
               << numRequests << ","
               << successfulRequests << ","
               << successRate << ","
               << avgRouteTimeMs << ","
               << avgPathLength << ","
               << (routingStats.packetsSent + routingStats.packetsReceived) << ","
               << forwardingStats.packetsForwarded << std::endl;
  
  Simulator::Destroy();
}

// ===========================================================================
// 主函数
// ===========================================================================

int main(int argc, char *argv[])
{
  CommandLine cmd;
  std::string outputFile = "qcast_scaling_simple.csv";
  
  cmd.AddValue("output", "输出文件路径", outputFile);
  cmd.Parse(argc, argv);
  
  std::cout << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Q-CAST协议规模扩展测试（极简版）" << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  std::cout << "========================================" << std::endl;
  
  // 打开输出文件
  std::ofstream outputStream(outputFile);
  if (!outputStream.is_open())
  {
    std::cout << "错误: 无法打开输出文件: " << outputFile << std::endl;
    return 1;
  }
  
  // 写入CSV标题
  outputStream << "test_name,topology_type,num_nodes,link_fidelity,num_requests,successful_requests,success_rate,avg_route_time_ms,avg_path_length,total_routing_packets,total_forwarding_packets" << std::endl;
  
  // 定义测试套件
  
   // 测试套件1：不同网络规模（链式拓扑）- 只运行10节点测试
   std::vector<int> chainSizes = {10};
   for (int size : chainSizes)
   {
     std::string testName = "chain_" + std::to_string(size) + "nodes";
     RunSingleTest(testName, "chain", size, 0.95, 10, outputStream);
   }
   
   // 跳过其他测试套件以便快速验证
   std::cout << "跳过其他测试套件，仅验证chain_10nodes" << std::endl;
   outputStream.close();
   std::cout << std::endl;
   std::cout << "========================================" << std::endl;
   std::cout << "验证测试完成" << std::endl;
   std::cout << "输出文件: " << outputFile << std::endl;
   std::cout << "========================================" << std::endl;
   return 0;
  
  // 测试套件2：不同拓扑结构（固定30节点）
  std::vector<std::string> topologies = {"chain", "grid", "random"};
  for (const auto& topology : topologies)
  {
    std::string testName = topology + "_30nodes";
    RunSingleTest(testName, topology, 30, 0.95, 15, outputStream);
  }
  
  // 测试套件3：不同链路质量（网格拓扑，25节点）
  std::vector<double> fidelities = {0.7, 0.8, 0.9, 0.95, 0.99};
  for (double fidelity : fidelities)
  {
    std::string testName = "grid_25nodes_fidelity" + std::to_string((int)(fidelity * 100));
    RunSingleTest(testName, "grid", 25, fidelity, 20, outputStream);
  }
  
  // 测试套件4：大规模随机网络（100节点）
  RunSingleTest("random_100nodes", "random", 100, 0.95, 30, outputStream);
  
  // 关闭输出文件
  outputStream.close();
  
  std::cout << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "测试完成" << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  std::cout << "========================================" << std::endl;
  
  return 0;
}