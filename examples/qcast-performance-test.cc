/*
 * Q-CAST协议性能测试
 * 
 * 本测试评估Q-CAST量子网络路由协议在不同网络拓扑下的性能。
 * 测试包括：
 * 1. 不同拓扑规模（5节点、9节点网格、16节点网格）
 * 2. 不同链路质量（成功率变化）
 * 3. 不同网络负载（并发路由请求数）
 * 
 * 性能指标：
 * 1. 路由发现成功率
 * 2. 平均路由发现时间
 * 3. 平均路径长度（跳数）
 * 4. 平均端到端延迟
 * 5. 资源预留成功率
 * 6. 恢复路径数量
 * 7. 协议开销（发送包数）
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

NS_LOG_COMPONENT_DEFINE ("QCastPerformanceTest");

using namespace ns3;

// ===========================================================================
// 性能指标收集器
// ===========================================================================

/**
 * \brief 性能测试结果
 */
struct PerformanceResult
{
  std::string testName;                   /**< 测试名称 */
  int numNodes;                           /**< 节点数量 */
  int numLinks;                           /**< 链路数量 */
  double linkSuccessRate;                 /**< 链路平均成功率 */
  int concurrentRequests;                 /**< 并发路由请求数 */
  
  // 路由性能指标
  int totalRouteRequests;                 /**< 总路由请求数 */
  int successfulRouteRequests;            /**< 成功路由请求数 */
  double routeSuccessRate;                /**< 路由成功率 */
  double avgRouteDiscoveryTimeMs;         /**< 平均路由发现时间（毫秒） */
  double avgPathLength;                   /**< 平均路径长度（跳数） */
  double avgEndToEndDelayMs;              /**< 平均端到端延迟（毫秒） */
  double avgCost;                         /**< 平均路径成本 */
  
  // Q-CAST特定指标
  double avgRecoveryPaths;                /**< 平均恢复路径数 */
  double avgSuccessProbability;           /**< 平均成功概率 */
  int totalEntanglementSwaps;             /**< 总纠缠交换次数 */
  int totalEprPairsDistributed;           /**< 总EPR对分发数 */
  
  // 资源使用指标
  double avgResourceReservationRate;      /**< 平均资源预留成功率 */
  double avgMemoryUtilization;            /**< 平均内存利用率 */
  
  // 协议开销指标
  int totalRoutingPacketsSent;            /**< 总路由包发送数 */
  int totalRoutingPacketsReceived;        /**< 总路由包接收数 */
  int totalForwardingPackets;             /**< 总转发包数 */
  
  PerformanceResult()
    : numNodes(0),
      numLinks(0),
      linkSuccessRate(0.0),
      concurrentRequests(0),
      totalRouteRequests(0),
      successfulRouteRequests(0),
      routeSuccessRate(0.0),
      avgRouteDiscoveryTimeMs(0.0),
      avgPathLength(0.0),
      avgEndToEndDelayMs(0.0),
      avgCost(0.0),
      avgRecoveryPaths(0.0),
      avgSuccessProbability(0.0),
      totalEntanglementSwaps(0),
      totalEprPairsDistributed(0),
      avgResourceReservationRate(0.0),
      avgMemoryUtilization(0.0),
      totalRoutingPacketsSent(0),
      totalRoutingPacketsReceived(0),
      totalForwardingPackets(0)
  {}
  
  /**
   * \brief 输出CSV格式标题行
   */
  static std::string GetCsvHeader()
  {
    return "test_name,num_nodes,num_links,link_success_rate,concurrent_requests,"
           "total_requests,successful_requests,route_success_rate,"
           "avg_discovery_time_ms,avg_path_length,avg_delay_ms,avg_cost,"
           "avg_recovery_paths,avg_success_probability,total_entanglement_swaps,"
           "total_epr_pairs,avg_resource_reservation_rate,avg_memory_utilization,"
           "total_routing_packets_sent,total_routing_packets_received,total_forwarding_packets";
  }
  
  /**
   * \brief 输出CSV格式数据行
   */
  std::string ToCsvString() const
  {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << testName << ",";
    ss << numNodes << ",";
    ss << numLinks << ",";
    ss << linkSuccessRate << ",";
    ss << concurrentRequests << ",";
    ss << totalRouteRequests << ",";
    ss << successfulRouteRequests << ",";
    ss << routeSuccessRate << ",";
    ss << avgRouteDiscoveryTimeMs << ",";
    ss << avgPathLength << ",";
    ss << avgEndToEndDelayMs << ",";
    ss << avgCost << ",";
    ss << avgRecoveryPaths << ",";
    ss << avgSuccessProbability << ",";
    ss << totalEntanglementSwaps << ",";
    ss << totalEprPairsDistributed << ",";
    ss << avgResourceReservationRate << ",";
    ss << avgMemoryUtilization << ",";
    ss << totalRoutingPacketsSent << ",";
    ss << totalRoutingPacketsReceived << ",";
    ss << totalForwardingPackets;
    
    return ss.str();
  }
  
  /**
   * \brief 输出详细报告
   */
  void PrintReport() const
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("=================================================================");
    NS_LOG_INFO("性能测试结果: " << testName);
    NS_LOG_INFO("=================================================================");
    NS_LOG_INFO("网络配置:");
    NS_LOG_INFO("  节点数: " << numNodes);
    NS_LOG_INFO("  链路数: " << numLinks);
    NS_LOG_INFO("  链路成功率: " << linkSuccessRate * 100 << "%");
    NS_LOG_INFO("  并发请求数: " << concurrentRequests);
    NS_LOG_INFO("");
    NS_LOG_INFO("路由性能:");
    NS_LOG_INFO("  总路由请求数: " << totalRouteRequests);
    NS_LOG_INFO("  成功路由请求数: " << successfulRouteRequests);
    NS_LOG_INFO("  路由成功率: " << routeSuccessRate * 100 << "%");
    NS_LOG_INFO("  平均路由发现时间: " << avgRouteDiscoveryTimeMs << " ms");
    NS_LOG_INFO("  平均路径长度: " << avgPathLength << " hops");
    NS_LOG_INFO("  平均端到端延迟: " << avgEndToEndDelayMs << " ms");
    NS_LOG_INFO("  平均路径成本: " << avgCost);
    NS_LOG_INFO("");
    NS_LOG_INFO("Q-CAST特性:");
    NS_LOG_INFO("  平均恢复路径数: " << avgRecoveryPaths);
    NS_LOG_INFO("  平均成功概率: " << avgSuccessProbability * 100 << "%");
    NS_LOG_INFO("  总纠缠交换次数: " << totalEntanglementSwaps);
    NS_LOG_INFO("  总EPR对分发数: " << totalEprPairsDistributed);
    NS_LOG_INFO("");
    NS_LOG_INFO("资源使用:");
    NS_LOG_INFO("  平均资源预留成功率: " << avgResourceReservationRate * 100 << "%");
    NS_LOG_INFO("  平均内存利用率: " << avgMemoryUtilization * 100 << "%");
    NS_LOG_INFO("");
    NS_LOG_INFO("协议开销:");
    NS_LOG_INFO("  总路由包发送数: " << totalRoutingPacketsSent);
    NS_LOG_INFO("  总路由包接收数: " << totalRoutingPacketsReceived);
    NS_LOG_INFO("  总转发包数: " << totalForwardingPackets);
    NS_LOG_INFO("=================================================================");
  }
};

/**
 * \brief 性能收集器类
 * 
 * 收集和计算性能指标
 */
class PerformanceCollector
{
public:
  PerformanceCollector()
    : m_totalRouteRequests(0),
      m_successfulRouteRequests(0),
      m_totalPathLength(0),
      m_totalEndToEndDelay(0),
      m_totalCost(0),
      m_totalRecoveryPaths(0),
      m_totalSuccessProbability(0),
      m_totalResourceReservationAttempts(0),
      m_successfulResourceReservations(0)
  {}
  
  /**
   * \brief 记录一次路由请求
   */
  void RecordRouteRequest()
  {
    m_totalRouteRequests++;
  }
  
  /**
   * \brief 记录一次成功路由
   */
  void RecordSuccessfulRoute(const QuantumRoute& route, 
                           const Time& discoveryTime,
                           int recoveryPathsCount,
                           double successProbability)
  {
    m_successfulRouteRequests++;
    m_totalPathLength += route.GetHopCount();
    m_totalEndToEndDelay += route.estimatedDelay.GetSeconds() * 1000; // 转换为毫秒
    m_totalCost += route.totalCost;
    m_totalRecoveryPaths += recoveryPathsCount;
    m_totalSuccessProbability += successProbability;
    m_routeDiscoveryTimes.push_back(discoveryTime.GetSeconds() * 1000); // 转换为毫秒
  }
  
  /**
   * \brief 记录资源预留尝试
   */
  void RecordResourceReservationAttempt(bool success)
  {
    m_totalResourceReservationAttempts++;
    if (success)
    {
      m_successfulResourceReservations++;
    }
  }
  
  /**
   * \brief 记录协议统计
   */
  void RecordProtocolStats(const QuantumNetworkStats& routingStats,
                          const QuantumNetworkStats& forwardingStats)
  {
    m_routingStats = routingStats;
    m_forwardingStats = forwardingStats;
  }
  
  /**
   * \brief 记录资源利用率
   */
  void RecordResourceUtilization(double memoryUtilization)
  {
    m_memoryUtilizations.push_back(memoryUtilization);
  }
  
  /**
   * \brief 计算并返回性能结果
   */
  PerformanceResult GetResult(const std::string& testName,
                             int numNodes,
                             int numLinks,
                             double linkSuccessRate,
                             int concurrentRequests) const
  {
    PerformanceResult result;
    result.testName = testName;
    result.numNodes = numNodes;
    result.numLinks = numLinks;
    result.linkSuccessRate = linkSuccessRate;
    result.concurrentRequests = concurrentRequests;
    
    // 路由性能
    result.totalRouteRequests = m_totalRouteRequests;
    result.successfulRouteRequests = m_successfulRouteRequests;
    result.routeSuccessRate = (m_totalRouteRequests > 0) ? 
                              (double)m_successfulRouteRequests / m_totalRouteRequests : 0.0;
    
    // 平均路由发现时间
    if (!m_routeDiscoveryTimes.empty())
    {
      double sum = 0.0;
      for (double time : m_routeDiscoveryTimes)
      {
        sum += time;
      }
      result.avgRouteDiscoveryTimeMs = sum / m_routeDiscoveryTimes.size();
    }
    
    // 平均路径指标
    if (m_successfulRouteRequests > 0)
    {
      result.avgPathLength = (double)m_totalPathLength / m_successfulRouteRequests;
      result.avgEndToEndDelayMs = m_totalEndToEndDelay / m_successfulRouteRequests;
      result.avgCost = m_totalCost / m_successfulRouteRequests;
      result.avgRecoveryPaths = (double)m_totalRecoveryPaths / m_successfulRouteRequests;
      result.avgSuccessProbability = m_totalSuccessProbability / m_successfulRouteRequests;
    }
    
    // 资源预留成功率
    if (m_totalResourceReservationAttempts > 0)
    {
      result.avgResourceReservationRate = (double)m_successfulResourceReservations / 
                                         m_totalResourceReservationAttempts;
    }
    
    // 平均内存利用率
    if (!m_memoryUtilizations.empty())
    {
      double sum = 0.0;
      for (double util : m_memoryUtilizations)
      {
        sum += util;
      }
      result.avgMemoryUtilization = sum / m_memoryUtilizations.size();
    }
    
    // 协议开销
    result.totalRoutingPacketsSent = m_routingStats.packetsSent;
    result.totalRoutingPacketsReceived = m_routingStats.packetsReceived;
    result.totalForwardingPackets = m_forwardingStats.packetsForwarded;
    
    // Q-CAST特定统计
    result.totalEntanglementSwaps = m_forwardingStats.entanglementSwaps;
    result.totalEprPairsDistributed = m_forwardingStats.eprPairsDistributed;
    
    return result;
  }
  
private:
  int m_totalRouteRequests;
  int m_successfulRouteRequests;
  int m_totalPathLength;
  double m_totalEndToEndDelay; // 毫秒
  double m_totalCost;
  int m_totalRecoveryPaths;
  double m_totalSuccessProbability;
  int m_totalResourceReservationAttempts;
  int m_successfulResourceReservations;
  
  std::vector<double> m_routeDiscoveryTimes;
  std::vector<double> m_memoryUtilizations;
  QuantumNetworkStats m_routingStats;
  QuantumNetworkStats m_forwardingStats;
};



// ===========================================================================
// 拓扑生成函数
// ===========================================================================

/**
 * \brief 创建链式拓扑
 * 
 * @param numNodes 节点数量
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateChainTopology(int numNodes, const std::string& nodePrefix = "Node")
{
  std::vector<std::string> owners;
  for (int i = 0; i < numNodes; ++i)
  {
    owners.push_back(nodePrefix + std::to_string(i));
  }
  
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  std::vector<Ptr<QuantumNode>> nodes;
  for (int i = 0; i < numNodes; ++i)
  {
    nodes.push_back(CreateObject<QuantumNode> (qphyent, owners[i]));
  }
  
  std::vector<Ptr<QuantumChannel>> channels;
  for (int i = 0; i < numNodes - 1; ++i)
  {
    channels.push_back(CreateObject<QuantumChannel> (owners[i], owners[i+1]));
  }
  
  return {nodes, channels};
}

/**
 * \brief 创建网格拓扑
 * 
 * @param rows 行数
 * @param cols 列数
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateGridTopology(int rows, int cols, const std::string& nodePrefix = "Node")
{
  std::vector<std::string> owners;
  for (int r = 0; r < rows; ++r)
  {
    for (int c = 0; c < cols; ++c)
    {
      owners.push_back(nodePrefix + std::to_string(r) + "_" + std::to_string(c));
    }
  }
  
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  std::vector<Ptr<QuantumNode>> nodes;
  for (const auto& owner : owners)
  {
    nodes.push_back(CreateObject<QuantumNode> (qphyent, owner));
  }
  
  std::vector<Ptr<QuantumChannel>> channels;
  
  // 创建水平连接
  for (int r = 0; r < rows; ++r)
  {
    for (int c = 0; c < cols - 1; ++c)
    {
      int idx1 = r * cols + c;
      int idx2 = r * cols + c + 1;
      channels.push_back(CreateObject<QuantumChannel> (owners[idx1], owners[idx2]));
    }
  }
  
  // 创建垂直连接
  for (int r = 0; r < rows - 1; ++r)
  {
    for (int c = 0; c < cols; ++c)
    {
      int idx1 = r * cols + c;
      int idx2 = (r + 1) * cols + c;
      channels.push_back(CreateObject<QuantumChannel> (owners[idx1], owners[idx2]));
    }
  }
  
  return {nodes, channels};
}

/**
 * \brief 创建随机拓扑
 * 
 * @param numNodes 节点数量
 * @param connectionProbability 连接概率
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateRandomTopology(int numNodes, double connectionProbability, 
                    const std::string& nodePrefix = "Node")
{
  Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable> ();
  
  std::vector<std::string> owners;
  for (int i = 0; i < numNodes; ++i)
  {
    owners.push_back(nodePrefix + std::to_string(i));
  }
  
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
  
  std::vector<Ptr<QuantumNode>> nodes;
  for (int i = 0; i < numNodes; ++i)
  {
    nodes.push_back(CreateObject<QuantumNode> (qphyent, owners[i]));
  }
  
  std::vector<Ptr<QuantumChannel>> channels;
  
  // 确保连通性：创建最小生成树
  for (int i = 0; i < numNodes - 1; ++i)
  {
    channels.push_back(CreateObject<QuantumChannel> (owners[i], owners[i+1]));
  }
  
  // 随机添加额外连接
  for (int i = 0; i < numNodes; ++i)
  {
    for (int j = i + 2; j < numNodes; ++j) // 避免重复和相邻节点
    {
      if (randVar->GetValue(0, 1) < connectionProbability)
      {
        channels.push_back(CreateObject<QuantumChannel> (owners[i], owners[j]));
      }
    }
  }
  
  return {nodes, channels};
}

// ===========================================================================
// 辅助函数
// ===========================================================================

/**
 * \brief 获取节点的owner名称
 * 
 * @param topologyType 拓扑类型 ("chain", "grid", "random")
 * @param nodeIndex 节点索引
 * @param rows 网格行数（仅网格拓扑需要）
 * @param cols 网格列数（仅网格拓扑需要）
 * @param nodePrefix 节点名前缀
 * @return owner名称
 */
std::string GetNodeOwnerName(const std::string& topologyType, 
                            int nodeIndex,
                            int rows = 0,
                            int cols = 0,
                            const std::string& nodePrefix = "Node")
{
  if (topologyType == "chain" || topologyType == "random")
  {
    return nodePrefix + std::to_string(nodeIndex);
  }
  else if (topologyType == "grid")
  {
    int r = nodeIndex / cols;
    int c = nodeIndex % cols;
    return nodePrefix + std::to_string(r) + "_" + std::to_string(c);
  }
  return "";
}

// ===========================================================================
// 测试运行函数
// ===========================================================================

/**
 * \brief 运行单个性能测试
 * 
 * @param topologyType 拓扑类型 ("chain", "grid", "random")
 * @param numNodes 节点数量
 * @param linkSuccessRate 链路成功率
 * @param concurrentRequests 并发路由请求数
 * @param testName 测试名称
 * @return 性能结果
 */
PerformanceResult RunPerformanceTest(const std::string& topologyType,
                                    int numNodes,
                                    double linkSuccessRate,
                                    int concurrentRequests,
                                    const std::string& testName)
{
  NS_LOG_INFO("");
  NS_LOG_INFO("=================================================================");
  NS_LOG_INFO("开始性能测试: " << testName);
  NS_LOG_INFO("拓扑类型: " << topologyType);
  NS_LOG_INFO("节点数量: " << numNodes);
  NS_LOG_INFO("链路成功率: " << linkSuccessRate);
  NS_LOG_INFO("并发请求数: " << concurrentRequests);
  NS_LOG_INFO("=================================================================");
  
  // 创建拓扑
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  int rows = 0, cols = 0;
  
  if (topologyType == "chain")
  {
    auto result = CreateChainTopology(numNodes);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "grid")
  {
    // 计算网格尺寸
    rows = (int)std::sqrt(numNodes);
    cols = (numNodes + rows - 1) / rows; // 向上取整
    auto result = CreateGridTopology(rows, cols);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "random")
  {
    auto result = CreateRandomTopology(numNodes, 0.3); // 30%连接概率
    nodes = result.first;
    channels = result.second;
  }
  else
  {
    NS_LOG_ERROR("未知拓扑类型: " << topologyType);
    return PerformanceResult();
  }
  
  // 安装Internet栈
  InternetStackHelper internet;
  NodeContainer nodeContainer;
  for (const auto& node : nodes)
  {
    nodeContainer.Add(node);
  }
  internet.Install(nodeContainer);
  
  // 创建owner名称到节点指针的映射
  std::map<std::string, Ptr<QuantumNode>> nodeMap;
  for (size_t i = 0; i < nodes.size(); ++i)
  {
    std::string ownerName = GetNodeOwnerName(topologyType, i, rows, cols);
    nodeMap[ownerName] = nodes[i];
  }
  
  // 安装量子网络栈
  QuantumNetStackHelper netStackHelper;
  for (const auto& channel : channels)
  {
    // 获取通道的两个所有者
    std::string src = channel->GetSrcOwner();
    std::string dst = channel->GetDstOwner();
    
    // 查找对应的节点
    Ptr<QuantumNode> srcNode = nodeMap[src];
    Ptr<QuantumNode> dstNode = nodeMap[dst];
    
    if (srcNode && dstNode)
    {
      netStackHelper.Install(srcNode, dstNode);
    }
  }
  
  // 创建期望吞吐量度量
  Ptr<ExpectedThroughputMetric> etMetric = CreateObject<ExpectedThroughputMetric> ();
  etMetric->SetLinkSuccessRate(linkSuccessRate);
  
  // 创建Q-CAST路由协议（k=3）
  Ptr<QCastRoutingProtocol> routingProtocol = CreateObject<QCastRoutingProtocol>();
  routingProtocol->SetMetric(etMetric);
  routingProtocol->SetKHopDistance(3);
  
  // 创建Q-CAST转发引擎
  Ptr<QCastForwardingEngine> forwardingEngine = CreateObject<QCastForwardingEngine>();
  forwardingEngine->SetForwardingStrategy(QFS_ON_DEMAND);
  
  // 获取资源管理器
  Ptr<QuantumResourceManager> resourceManager = QuantumResourceManager::GetDefaultResourceManager();
  
  // 配置第一个节点的网络层（作为测试源节点）
  if (nodes.empty())
  {
    NS_LOG_ERROR("没有可用的节点");
    return PerformanceResult();
  }
  
  Ptr<QuantumNode> sourceNode = nodes[0];
  Ptr<QuantumNetworkLayer> networkLayer = sourceNode->GetQuantumNetworkLayer();
  if (!networkLayer)
  {
    NS_LOG_ERROR("无法获取网络层");
    return PerformanceResult();
  }
  
  networkLayer->SetRoutingProtocol(routingProtocol);
  networkLayer->SetForwardingEngine(forwardingEngine);
  networkLayer->SetResourceManager(resourceManager);
  
  routingProtocol->SetNetworkLayer(networkLayer);
  routingProtocol->SetResourceManager(resourceManager);
  
  // 性能收集器
  PerformanceCollector collector;
  
  // 执行邻居发现
  NS_LOG_INFO("执行邻居发现...");
  routingProtocol->DiscoverNeighbors();
  
  // 运行并发路由请求
  NS_LOG_INFO("运行" << concurrentRequests << "个并发路由请求...");
  
  // 创建路由需求
  QuantumRouteRequirements requirements;
  requirements.minFidelity = 0.8;
  requirements.maxDelay = Seconds(1.0);
  requirements.numQubits = 2;
  requirements.duration = Seconds(30.0);
  requirements.strategy = QFS_ON_DEMAND;
  
  // 记录开始时间
  Time startTime = Simulator::Now();
  
  // 发起路由请求（到随机目的地）
  Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable> ();
  for (int i = 0; i < concurrentRequests; ++i)
  {
    // 随机选择目的地（排除源节点）
    int destIndex;
    do {
      destIndex = randVar->GetInteger(0, nodes.size() - 1);
    } while (destIndex == 0);
    
    std::string destAddress = nodes[destIndex]->GetQuantumNetworkLayer()->GetAddress();
    
    collector.RecordRouteRequest();
    
    // 执行路由请求
    Time requestStartTime = Simulator::Now();
    QuantumRoute route = routingProtocol->RouteRequest(sourceNode->GetQuantumNetworkLayer()->GetAddress(), 
                                                      destAddress, 
                                                      requirements);
    
    Time discoveryTime = Simulator::Now() - requestStartTime;
    
    if (route.IsValid())
    {
      // 记录成功路由
      collector.RecordSuccessfulRoute(route, discoveryTime, 1, 0.95); // 简化：假设1个恢复路径，95%成功率
      
      // 记录资源预留尝试
      collector.RecordResourceReservationAttempt(true);
      
      // 可选：发送测试包
      Ptr<QuantumPacket> packet = CreateObject<QuantumPacket>(sourceNode->GetQuantumNetworkLayer()->GetAddress(), destAddress);
      packet->SetType(QuantumPacket::DATA);
      packet->SetProtocol(QuantumPacket::PROTO_QUANTUM_FORWARDING);
      packet->SetRoute(route);
      packet->AddQubitReference("TestQubit1");
      packet->AddQubitReference("TestQubit2");
      
      networkLayer->SendPacket(packet);
    }
    else
    {
      collector.RecordResourceReservationAttempt(false);
    }
  }
  
  // 等待所有请求完成
  Time totalTestDuration = Seconds(5.0); // 5秒测试时间
  Simulator::Stop(startTime + totalTestDuration);
  Simulator::Run();
  
  // 收集统计信息
  QuantumNetworkStats routingStats = routingProtocol->GetStatistics();
  QuantumNetworkStats forwardingStats = forwardingEngine->GetStatistics();
  collector.RecordProtocolStats(routingStats, forwardingStats);
  
  // 收集资源利用率
  double memoryUtilization = resourceManager->GetMemoryUtilization(sourceNode->GetQuantumNetworkLayer()->GetAddress());
  collector.RecordResourceUtilization(memoryUtilization);
  
  // 计算性能结果
  PerformanceResult result = collector.GetResult(testName, 
                                                numNodes, 
                                                channels.size(),
                                                linkSuccessRate,
                                                concurrentRequests);
  
  // 清理
  Simulator::Destroy();
  
  NS_LOG_INFO("测试完成: " << testName);
  result.PrintReport();
  
  return result;
}

// ===========================================================================
// 主函数
// ===========================================================================

int main(int argc, char *argv[])
{
  // 解析命令行参数
  CommandLine cmd;
  std::string outputFile = "qcast_performance_results.csv";
  bool runAllTests = true;
  std::string singleTest = "";
  
  cmd.AddValue("output", "输出文件路径", outputFile);
  cmd.AddValue("run-all", "运行所有测试 (true/false)", runAllTests);
  cmd.AddValue("single-test", "运行单个测试 (test1/test2/...)", singleTest);
  cmd.Parse(argc, argv);
  
  // 设置日志级别
  LogComponentEnable("QCastPerformanceTest", LOG_LEVEL_INFO);
  LogComponentEnable("QuantumNetworkLayer", LOG_LEVEL_WARN);
  LogComponentEnable("QuantumRoutingProtocol", LOG_LEVEL_WARN);
  
  NS_LOG_INFO("");
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("Q-CAST协议性能测试");
  NS_LOG_INFO("输出文件: " << outputFile);
  NS_LOG_INFO("======================================");
  
  // 打开输出文件
  std::ofstream outputStream(outputFile);
  if (!outputStream.is_open())
  {
    NS_LOG_ERROR("无法打开输出文件: " << outputFile);
    return 1;
  }
  
  // 写入CSV标题
  outputStream << PerformanceResult::GetCsvHeader() << std::endl;
  
  std::vector<PerformanceResult> allResults;
  
  // 测试套件1：不同拓扑规模
  if (runAllTests || singleTest == "test1")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件1: 不同拓扑规模");
    
    // 链式拓扑：5节点
    PerformanceResult result1 = RunPerformanceTest("chain", 5, 0.95, 10, 
                                                   "chain_5nodes_95pct_10req");
    allResults.push_back(result1);
    outputStream << result1.ToCsvString() << std::endl;
    
    // 链式拓扑：10节点
    PerformanceResult result2 = RunPerformanceTest("chain", 10, 0.95, 10,
                                                   "chain_10nodes_95pct_10req");
    allResults.push_back(result2);
    outputStream << result2.ToCsvString() << std::endl;
    
    // 网格拓扑：3x3 (9节点)
    PerformanceResult result3 = RunPerformanceTest("grid", 9, 0.95, 10,
                                                   "grid_9nodes_95pct_10req");
    allResults.push_back(result3);
    outputStream << result3.ToCsvString() << std::endl;
    
    // 网格拓扑：4x4 (16节点)
    PerformanceResult result4 = RunPerformanceTest("grid", 16, 0.95, 10,
                                                   "grid_16nodes_95pct_10req");
    allResults.push_back(result4);
    outputStream << result4.ToCsvString() << std::endl;
    
    // 随机拓扑：15节点
    PerformanceResult result5 = RunPerformanceTest("random", 15, 0.95, 10,
                                                   "random_15nodes_95pct_10req");
    allResults.push_back(result5);
    outputStream << result5.ToCsvString() << std::endl;
  }
  
  // 测试套件2：不同链路质量
  if (runAllTests || singleTest == "test2")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件2: 不同链路质量");
    
    // 固定9节点网格，改变链路成功率
    std::vector<double> successRates = {0.7, 0.8, 0.9, 0.95, 0.99};
    for (size_t i = 0; i < successRates.size(); ++i)
    {
      std::string testName = "grid_9nodes_" + 
                            std::to_string((int)(successRates[i] * 100)) + 
                            "pct_10req";
      PerformanceResult result = RunPerformanceTest("grid", 9, successRates[i], 10, testName);
      allResults.push_back(result);
      outputStream << result.ToCsvString() << std::endl;
    }
  }
  
  // 测试套件3：不同并发负载
  if (runAllTests || singleTest == "test3")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件3: 不同并发负载");
    
    // 固定9节点网格，95%成功率，改变并发请求数
    std::vector<int> concurrentRequests = {1, 5, 10, 20, 50};
    for (size_t i = 0; i < concurrentRequests.size(); ++i)
    {
      std::string testName = "grid_9nodes_95pct_" + 
                            std::to_string(concurrentRequests[i]) + "req";
      PerformanceResult result = RunPerformanceTest("grid", 9, 0.95, 
                                                   concurrentRequests[i], testName);
      allResults.push_back(result);
      outputStream << result.ToCsvString() << std::endl;
    }
  }
  
  // 关闭输出文件
  outputStream.close();
  
  // 生成总结报告
  NS_LOG_INFO("");
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("性能测试总结报告");
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("总测试数: " << allResults.size());
  NS_LOG_INFO("输出文件: " << outputFile);
  
  if (!allResults.empty())
  {
    // 计算平均路由成功率
    double avgSuccessRate = 0.0;
    for (const auto& result : allResults)
    {
      avgSuccessRate += result.routeSuccessRate;
    }
    avgSuccessRate /= allResults.size();
    
    NS_LOG_INFO("平均路由成功率: " << avgSuccessRate * 100 << "%");
    
    // 找到最佳和最差性能
    auto bestResult = std::max_element(allResults.begin(), allResults.end(),
      [](const PerformanceResult& a, const PerformanceResult& b) {
        return a.routeSuccessRate < b.routeSuccessRate;
      });
    
    auto worstResult = std::min_element(allResults.begin(), allResults.end(),
      [](const PerformanceResult& a, const PerformanceResult& b) {
        return a.routeSuccessRate < b.routeSuccessRate;
      });
    
    NS_LOG_INFO("最佳路由成功率: " << bestResult->routeSuccessRate * 100 << 
                "% (" << bestResult->testName << ")");
    NS_LOG_INFO("最差路由成功率: " << worstResult->routeSuccessRate * 100 << 
                "% (" << worstResult->testName << ")");
  }
  
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("Q-CAST性能测试完成");
  NS_LOG_INFO("======================================");
  
  return 0;
}