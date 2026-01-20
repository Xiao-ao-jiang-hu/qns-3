/*
 * Q-CAST协议规模扩展测试（简化版）
 * 
 * 本测试评估Q-CAST量子网络路由协议在不同规模网络下的性能。
 * 测试包括：
 * 1. 不同网络规模（10、30、50、100节点）
 * 2. 不同拓扑结构（链式、网格、随机）
 * 3. 不同时间调度的纠缠请求
 * 
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

NS_LOG_COMPONENT_DEFINE ("QCastScalingTest");

using namespace ns3;

// ===========================================================================
// 测试结果结构
// ===========================================================================

struct TestResult
{
  std::string testName;
  std::string topologyType;
  int numNodes;
  int numLinks;
  double linkFidelity;
  int numRequests;
  int successfulRequests;
  double successRate;
  double avgRouteTimeMs;
  double avgPathLength;
  double avgEstimatedFidelity;
  double avgEstimatedDelayMs;
  int totalRoutingPackets;
  int totalForwardingPackets;
  
  TestResult()
    : numNodes(0),
      numLinks(0),
      linkFidelity(0.95),
      numRequests(0),
      successfulRequests(0),
      successRate(0.0),
      avgRouteTimeMs(0.0),
      avgPathLength(0.0),
      avgEstimatedFidelity(0.0),
      avgEstimatedDelayMs(0.0),
      totalRoutingPackets(0),
      totalForwardingPackets(0)
  {}
  
  static std::string GetCsvHeader()
  {
    return "test_name,topology_type,num_nodes,num_links,link_fidelity,num_requests,successful_requests,success_rate,avg_route_time_ms,avg_path_length,avg_estimated_fidelity,avg_estimated_delay_ms,total_routing_packets,total_forwarding_packets";
  }
  
  std::string ToCsvString() const
  {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << testName << ",";
    ss << topologyType << ",";
    ss << numNodes << ",";
    ss << numLinks << ",";
    ss << linkFidelity << ",";
    ss << numRequests << ",";
    ss << successfulRequests << ",";
    ss << successRate << ",";
    ss << avgRouteTimeMs << ",";
    ss << avgPathLength << ",";
    ss << avgEstimatedFidelity << ",";
    ss << avgEstimatedDelayMs << ",";
    ss << totalRoutingPackets << ",";
    ss << totalForwardingPackets;
    
    return ss.str();
  }
  
  void PrintReport() const
  {
    std::cout << "" << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "测试结果: " << testName << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "配置:" << std::endl;
    std::cout << "  拓扑类型: " << topologyType << std::endl;
    std::cout << "  节点数: " << numNodes << std::endl;
    std::cout << "  链路数: " << numLinks << std::endl;
    std::cout << "  链路保真度: " << linkFidelity << std::endl;
    std::cout << "  请求数: " << numRequests << std::endl;
    std::cout << "" << std::endl;
    std::cout << "性能:" << std::endl;
    std::cout << "  成功请求数: " << successfulRequests << std::endl;
    std::cout << "  成功率: " << successRate * 100 << "%" << std::endl;
    std::cout << "  平均路由时间: " << avgRouteTimeMs << " ms" << std::endl;
    std::cout << "  平均路径长度: " << avgPathLength << " hops" << std::endl;
    std::cout << "  平均估计保真度: " << avgEstimatedFidelity << std::endl;
    std::cout << "  平均估计延迟: " << avgEstimatedDelayMs << " ms" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "协议开销:" << std::endl;
    std::cout << "  总路由包数: " << totalRoutingPackets << std::endl;
    std::cout << "  总转发包数: " << totalForwardingPackets << std::endl;
    std::cout << "=================================================================" << std::endl;
  }
};

// ===========================================================================
// 拓扑生成函数
// ===========================================================================

std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateChainTopology(int numNodes, Ptr<QuantumPhyEntity> qphyent)
{
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  
  for (int i = 0; i < numNodes; ++i)
  {
    std::string owner = "Node" + std::to_string(i);
    nodes.push_back(qphyent->GetNode(owner));
  }
  
  for (int i = 0; i < numNodes - 1; ++i)
  {
    std::string src = "Node" + std::to_string(i);
    std::string dst = "Node" + std::to_string(i + 1);
    channels.push_back(CreateObject<QuantumChannel>(src, dst));
  }
  
  return std::make_pair(nodes, channels);
}

std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateGridTopology(int rows, int cols, Ptr<QuantumPhyEntity> qphyent)
{
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  
  // 创建节点
  for (int r = 0; r < rows; ++r)
  {
    for (int c = 0; c < cols; ++c)
    {
      std::string owner = "Node" + std::to_string(r) + "_" + std::to_string(c);
      nodes.push_back(qphyent->GetNode(owner));
    }
  }
  
  // 水平连接
  for (int r = 0; r < rows; ++r)
  {
    for (int c = 0; c < cols - 1; ++c)
    {
      std::string src = "Node" + std::to_string(r) + "_" + std::to_string(c);
      std::string dst = "Node" + std::to_string(r) + "_" + std::to_string(c + 1);
      channels.push_back(CreateObject<QuantumChannel>(src, dst));
    }
  }
  
  // 垂直连接
  for (int r = 0; r < rows - 1; ++r)
  {
    for (int c = 0; c < cols; ++c)
    {
      std::string src = "Node" + std::to_string(r) + "_" + std::to_string(c);
      std::string dst = "Node" + std::to_string(r + 1) + "_" + std::to_string(c);
      channels.push_back(CreateObject<QuantumChannel>(src, dst));
    }
  }
  
  return std::make_pair(nodes, channels);
}

std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateRandomTopology(int numNodes, double connectionProbability, Ptr<QuantumPhyEntity> qphyent)
{
  Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable>();
  
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  
  // 创建节点
  for (int i = 0; i < numNodes; ++i)
  {
    std::string owner = "Node" + std::to_string(i);
    nodes.push_back(qphyent->GetNode(owner));
  }
  
  // 确保连通性：最小生成树
  for (int i = 0; i < numNodes - 1; ++i)
  {
    std::string src = "Node" + std::to_string(i);
    std::string dst = "Node" + std::to_string(i + 1);
    channels.push_back(CreateObject<QuantumChannel>(src, dst));
  }
  
  // 随机添加额外连接
  for (int i = 0; i < numNodes; ++i)
  {
    for (int j = i + 2; j < numNodes; ++j)
    {
      if (randVar->GetValue(0, 1) < connectionProbability)
      {
        std::string src = "Node" + std::to_string(i);
        std::string dst = "Node" + std::to_string(j);
        channels.push_back(CreateObject<QuantumChannel>(src, dst));
      }
    }
  }
  
  return std::make_pair(nodes, channels);
}

// ===========================================================================
// 测试运行函数
// ===========================================================================

TestResult RunScalingTest(const std::string& testName,
                         const std::string& topologyType,
                         int numNodes,
                         double linkFidelity,
                         int numRequests,
                         const std::vector<double>& requestTimes)
{
  std::cout << "" << std::endl;
  std::cout << "=================================================================" << std::endl;
  std::cout << "开始测试: " << testName << std::endl;
  std::cout << "拓扑类型: " << topologyType << std::endl;
  std::cout << "节点数量: " << numNodes << std::endl;
  std::cout << "链路保真度: " << linkFidelity << std::endl;
  std::cout << "请求数量: " << numRequests << std::endl;
  std::cout << "=================================================================" << std::endl;
  
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
  
  // 创建拓扑
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  
  if (topologyType == "chain")
  {
    auto result = CreateChainTopology(numNodes, qphyent);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "grid")
  {
    int rows = (int)std::sqrt(numNodes);
    int cols = (numNodes + rows - 1) / rows;
    auto result = CreateGridTopology(rows, cols, qphyent);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "random")
  {
    auto result = CreateRandomTopology(numNodes, 0.3, qphyent);
    nodes = result.first;
    channels = result.second;
  }
  
  std::cout << "创建拓扑: " << nodes.size() << " 节点, " << channels.size() << " 通道" << std::endl;
  
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
  qstack.Install(nodes);
  
  // 设置通道去极化模型
  for (auto& channel : channels)
  {
    channel->SetDepolarModel(linkFidelity, qphyent);
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
  
  // 创建路由需求
  QuantumRouteRequirements requirements;
  requirements.minFidelity = 0.8;
  requirements.maxDelay = Seconds(1.0);
  requirements.numQubits = 2;
  requirements.duration = Seconds(30.0);
  requirements.strategy = QFS_ON_DEMAND;
  
  // 性能统计
  int successfulRequests = 0;
  double totalRouteTime = 0.0;
  double totalPathLength = 0.0;
  double totalEstimatedFidelity = 0.0;
  double totalEstimatedDelay = 0.0;
  
  Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable>();
  
  // 安排请求
  for (int reqId = 0; reqId < numRequests; ++reqId)
  {
    // 确定请求时间
    double requestTime;
    if (reqId < (int)requestTimes.size())
    {
      requestTime = requestTimes[reqId];
    }
    else
    {
      requestTime = 0.1 + (reqId * 4.9) / numRequests;
    }
    
    // 随机选择源和目的节点
    int srcIdx = randVar->GetInteger(0, nodes.size() - 1);
    int dstIdx;
    do {
      dstIdx = randVar->GetInteger(0, nodes.size() - 1);
    } while (dstIdx == srcIdx);
    
    Ptr<QuantumNode> srcNode = nodes[srcIdx];
    Ptr<QuantumNode> dstNode = nodes[dstIdx];
    
    Simulator::Schedule(Seconds(requestTime), [=, &successfulRequests, &totalRouteTime,
                                               &totalPathLength, &totalEstimatedFidelity,
                                               &totalEstimatedDelay]() {
      std::cout << "在时间 " << requestTime << "s 处理请求: 节点" << srcIdx << " -> 节点" << dstIdx << std::endl;
      
      Ptr<QuantumNetworkLayer> networkLayer = srcNode->GetQuantumNetworkLayer();
      if (!networkLayer)
      {
        std::cout << "警告: 无法获取网络层" << std::endl;
        return;
      }
      
      // 执行路由请求
      Time startTime = Simulator::Now();
      QuantumRoute route = qcastRouting->RouteRequest(
        srcNode->GetQuantumNetworkLayer()->GetAddress(),
        dstNode->GetQuantumNetworkLayer()->GetAddress(),
        requirements);
      Time routeTime = Simulator::Now() - startTime;
      
      if (route.IsValid())
      {
        successfulRequests++;
        totalRouteTime += routeTime.GetSeconds() * 1000; // 转换为毫秒
        totalPathLength += route.GetHopCount();
        totalEstimatedFidelity += route.estimatedFidelity;
        totalEstimatedDelay += route.estimatedDelay.GetSeconds() * 1000;
        
        std::cout << "  路由成功: " << route.ToString() << std::endl;
        std::cout << "  路由时间: " << routeTime.GetSeconds() * 1000 << " ms" << std::endl;
        std::cout << "  路径长度: " << route.GetHopCount() << " hops" << std::endl;
        std::cout << "  估计保真度: " << route.estimatedFidelity << std::endl;
        
        // 可选：发送测试包
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
      else
      {
        std::cout << "  路由失败" << std::endl;
      }
    });
  }
  
  // 运行模拟
  double maxTime = 0;
  if (!requestTimes.empty())
  {
    maxTime = *std::max_element(requestTimes.begin(), requestTimes.end());
  }
  else
  {
    maxTime = 5.0;
  }
  
  Simulator::Stop(Seconds(maxTime + 2.0));
  std::cout << "运行模拟 " << (maxTime + 2.0) << " 秒..." << std::endl;
  
  Simulator::Run();
  
  // 收集统计信息
  QuantumNetworkStats routingStats = qcastRouting->GetStatistics();
  QuantumNetworkStats forwardingStats = qcastForwarding->GetStatistics();
  
  // 计算测试结果
  TestResult result;
  result.testName = testName;
  result.topologyType = topologyType;
  result.numNodes = nodes.size();
  result.numLinks = channels.size();
  result.linkFidelity = linkFidelity;
  result.numRequests = numRequests;
  result.successfulRequests = successfulRequests;
  result.successRate = numRequests > 0 ? (double)successfulRequests / numRequests : 0.0;
  
  if (successfulRequests > 0)
  {
    result.avgRouteTimeMs = totalRouteTime / successfulRequests;
    result.avgPathLength = totalPathLength / successfulRequests;
    result.avgEstimatedFidelity = totalEstimatedFidelity / successfulRequests;
    result.avgEstimatedDelayMs = totalEstimatedDelay / successfulRequests;
  }
  
  result.totalRoutingPackets = routingStats.packetsSent + routingStats.packetsReceived;
  result.totalForwardingPackets = forwardingStats.packetsForwarded;
  
  Simulator::Destroy();
  
  result.PrintReport();
  
  return result;
}

// ===========================================================================
// 主函数
// ===========================================================================

int main(int argc, char *argv[])
{
  CommandLine cmd;
  std::string outputFile = "qcast_scaling_results.csv";
  bool runAllTests = true;
  std::string singleTest = "";
  
  cmd.AddValue("output", "输出文件路径", outputFile);
  cmd.AddValue("run-all", "运行所有测试 (true/false)", runAllTests);
  cmd.AddValue("single-test", "运行单个测试", singleTest);
  cmd.Parse(argc, argv);
  
  std::cout << "" << std::endl;
  std::cout << "======================================" << std::endl;
  std::cout << "Q-CAST协议规模扩展测试" << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  std::cout << "======================================" << std::endl;
  
  // 打开输出文件
  std::ofstream outputStream(outputFile);
  if (!outputStream.is_open())
  {
    std::cout << "错误: 无法打开输出文件: " << outputFile << std::endl;
    return 1;
  }
  
  // 写入CSV标题
  outputStream << TestResult::GetCsvHeader() << std::endl;
  
  // 定义测试套件
  std::vector<std::tuple<std::string, std::string, int, double, int, std::vector<double>>> testSuites;
  
  // 测试套件1：不同网络规模（链式拓扑）
  std::vector<int> chainSizes = {10, 30, 50, 100};
  for (int size : chainSizes)
  {
    std::vector<double> requestTimes;
    for (int i = 0; i < 10; ++i)
    {
      requestTimes.push_back(0.1 + (i * 4.9) / 10);
    }
    
    testSuites.push_back(std::make_tuple(
      "chain_" + std::to_string(size) + "nodes",
      "chain",
      size,
      0.95,
      10,
      requestTimes
    ));
  }
  
  // 测试套件2：不同拓扑结构（固定30节点）
  std::vector<std::string> topologies = {"chain", "grid", "random"};
  for (const auto& topology : topologies)
  {
    std::vector<double> requestTimes;
    Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable>();
    for (int i = 0; i < 15; ++i)
    {
      requestTimes.push_back(0.1 + randVar->GetValue(0, 5.0));
    }
    std::sort(requestTimes.begin(), requestTimes.end());
    
    testSuites.push_back(std::make_tuple(
      topology + "_30nodes",
      topology,
      30,
      0.95,
      15,
      requestTimes
    ));
  }
  
  // 测试套件3：不同链路质量（网格拓扑，25节点）
  std::vector<double> fidelities = {0.7, 0.8, 0.9, 0.95, 0.99};
  for (double fidelity : fidelities)
  {
    std::vector<double> requestTimes;
    // 突发请求模式
    for (int i = 0; i < 10; ++i)
    {
      requestTimes.push_back(0.1 + (i * 0.9) / 10);
    }
    for (int i = 0; i < 10; ++i)
    {
      requestTimes.push_back(2.0 + (i * 1.0) / 10);
    }
    
    testSuites.push_back(std::make_tuple(
      "grid_25nodes_fidelity" + std::to_string((int)(fidelity * 100)),
      "grid",
      25,
      fidelity,
      20,
      requestTimes
    ));
  }
  
  // 测试套件4：大规模随机网络（100节点）
  std::vector<double> largeRequestTimes;
  for (int i = 0; i < 30; ++i)
  {
    largeRequestTimes.push_back(0.5 + (i * 9.5) / 30);
  }
  
  testSuites.push_back(std::make_tuple(
    "random_100nodes_large",
    "random",
    100,
    0.95,
    30,
    largeRequestTimes
  ));
  
  // 运行测试
  std::vector<TestResult> allResults;
  
  for (size_t i = 0; i < testSuites.size(); ++i)
  {
    const auto& suite = testSuites[i];
    
    std::string testName = std::get<0>(suite);
    std::string topologyType = std::get<1>(suite);
    int numNodes = std::get<2>(suite);
    double linkFidelity = std::get<3>(suite);
    int numRequests = std::get<4>(suite);
    std::vector<double> requestTimes = std::get<5>(suite);
    
    // 检查是否应该运行此测试
    bool shouldRun = runAllTests || 
                    (!singleTest.empty() && testName.find(singleTest) != std::string::npos);
    
    if (!shouldRun)
      continue;
    
    std::cout << "" << std::endl;
    std::cout << "运行测试 " << (i+1) << "/" << testSuites.size() << ": " << testName << std::endl;
    
    // 运行测试
    TestResult result = RunScalingTest(testName, topologyType, numNodes, 
                                      linkFidelity, numRequests, requestTimes);
    
    // 保存结果
    allResults.push_back(result);
    outputStream << result.ToCsvString() << std::endl;
    outputStream.flush();
  }
  
  // 关闭输出文件
  outputStream.close();
  
  // 生成最终报告
  std::cout << "" << std::endl;
  std::cout << "======================================" << std::endl;
  std::cout << "规模扩展测试完成" << std::endl;
  std::cout << "======================================" << std::endl;
  std::cout << "总测试数: " << allResults.size() << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  
  if (!allResults.empty())
  {
    double overallSuccessRate = 0.0;
    double overallAvgTime = 0.0;
    double overallAvgPathLength = 0.0;
    
    for (const auto& result : allResults)
    {
      overallSuccessRate += result.successRate;
      overallAvgTime += result.avgRouteTimeMs;
      overallAvgPathLength += result.avgPathLength;
    }
    
    overallSuccessRate /= allResults.size();
    overallAvgTime /= allResults.size();
    overallAvgPathLength /= allResults.size();
    
    std::cout << "" << std::endl;
    std::cout << "总体统计:" << std::endl;
    std::cout << "  平均成功率: " << overallSuccessRate * 100 << "%" << std::endl;
    std::cout << "  平均路由时间: " << overallAvgTime << " ms" << std::endl;
    std::cout << "  平均路径长度: " << overallAvgPathLength << " hops" << std::endl;
    
    // 找到最佳和最差性能
    auto bestResult = std::max_element(allResults.begin(), allResults.end(),
      [](const TestResult& a, const TestResult& b) {
        return a.successRate < b.successRate;
      });
    
    auto worstResult = std::min_element(allResults.begin(), allResults.end(),
      [](const TestResult& a, const TestResult& b) {
        return a.successRate < b.successRate;
      });
    
    std::cout << "" << std::endl;
    std::cout << "最佳性能: " << bestResult->testName << std::endl;
    std::cout << "  成功率: " << bestResult->successRate * 100 << "%" << std::endl;
    std::cout << "  平均时间: " << bestResult->avgRouteTimeMs << " ms" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "最差性能: " << worstResult->testName << std::endl;
    std::cout << "  成功率: " << worstResult->successRate * 100 << "%" << std::endl;
    std::cout << "  平均时间: " << worstResult->avgRouteTimeMs << " ms" << std::endl;
  }
  
  std::cout << "======================================" << std::endl;
  
  return 0;
}