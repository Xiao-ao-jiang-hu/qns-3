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
#include <exatn.hpp>  // For ExaTN tensor cleanup between tests

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
  double avgSuccessProbability;           /**< 平均成功概率（估计值）*/
  double avgActualFidelity;               /**< 平均实际保真度（物理仿真） */
  int totalEntanglementSwaps;             /**< 总纠缠交换次数 */
  int totalEprPairsDistributed;           /**< 总EPR对分发数 */
  
  // 资源使用指标
  double avgResourceReservationRate;      /**< 平均资源预留成功率 */
  double avgMemoryUtilization;            /**< 平均内存利用率 */
  
  // 协议开销指标
  int totalRoutingPacketsSent;            /**< 总路由包发送数 */
  int totalRoutingPacketsReceived;        /**< 总路由包接收数 */
  int totalForwardingPackets;             /**< 总转发包数 */
  
  // 每路由保真度统计
  struct RouteFidelityEntry
  {
    uint32_t routeId;
    uint32_t hopCount;
    double estimatedFidelity;
    double actualFidelity;
    double waitTimeMs;
  };
  std::vector<RouteFidelityEntry> routeFidelityStats;  /**< 每路由保真度统计 */
  
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
      avgActualFidelity(-1.0),
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
           "avg_recovery_paths,avg_estimated_fidelity,avg_actual_fidelity,total_entanglement_swaps,"
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
    ss << avgActualFidelity << ",";
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
    NS_LOG_INFO("  平均估计保真度: " << avgSuccessProbability * 100 << "%");
    if (avgActualFidelity >= 0)
    {
      NS_LOG_INFO("  平均实际保真度: " << avgActualFidelity * 100 << "% (物理仿真)");
    }
    else
    {
      NS_LOG_INFO("  平均实际保真度: N/A (物理仿真未启用)");
    }
    NS_LOG_INFO("  总纠缠交换次数: " << totalEntanglementSwaps);
    NS_LOG_INFO("  总EPR对分发数: " << totalEprPairsDistributed);
    NS_LOG_INFO("");
    // 每路由保真度详情
    if (!routeFidelityStats.empty())
    {
      NS_LOG_INFO("每路由保真度详情:");
      NS_LOG_INFO("  +----------+------+----------+----------+----------+----------+");
      NS_LOG_INFO("  | 路由ID   | 跳数 | 估计保真度| 实际保真度| 差异     | 等待时间 |");
      NS_LOG_INFO("  +----------+------+----------+----------+----------+----------+");
      for (const auto& entry : routeFidelityStats)
      {
        double diff = entry.actualFidelity - entry.estimatedFidelity;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "  | " << std::setw(8) << entry.routeId
           << " | " << std::setw(4) << entry.hopCount
           << " | " << std::setw(8) << (entry.estimatedFidelity * 100) << "%"
           << " | " << std::setw(8) << (entry.actualFidelity * 100) << "%"
           << " | " << std::setw(7) << (diff * 100) << "%"
           << " | " << std::setw(6) << entry.waitTimeMs << "ms |";
        NS_LOG_INFO(ss.str());
      }
      NS_LOG_INFO("  +----------+------+----------+----------+----------+----------+");
    }
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
 * @param linkFidelity 链路保真度（用于物理层去极化模型）
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateChainTopology(int numNodes, double linkFidelity = 0.95, const std::string& nodePrefix = "Node")
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
    Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (owners[i], owners[i+1]);
    // Set the depolarization model for actual fidelity calculation
    channel->SetDepolarModel(linkFidelity, qphyent);
    channels.push_back(channel);
  }
  
  return {nodes, channels};
}

/**
 * \brief 创建网格拓扑
 * 
 * @param rows 行数
 * @param cols 列数
 * @param linkFidelity 链路保真度（用于物理层去极化模型）
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateGridTopology(int rows, int cols, double linkFidelity = 0.95, const std::string& nodePrefix = "Node")
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
      Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (owners[idx1], owners[idx2]);
      channel->SetDepolarModel(linkFidelity, qphyent);
      channels.push_back(channel);
    }
  }
  
  // 创建垂直连接
  for (int r = 0; r < rows - 1; ++r)
  {
    for (int c = 0; c < cols; ++c)
    {
      int idx1 = r * cols + c;
      int idx2 = (r + 1) * cols + c;
      Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (owners[idx1], owners[idx2]);
      channel->SetDepolarModel(linkFidelity, qphyent);
      channels.push_back(channel);
    }
  }
  
  return {nodes, channels};
}

/**
 * \brief 创建随机拓扑
 * 
 * @param numNodes 节点数量
 * @param connectionProbability 连接概率
 * @param linkFidelity 链路保真度（用于物理层去极化模型）
 * @param nodePrefix 节点名前缀
 * @return 节点向量和通道向量
 */
std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
CreateRandomTopology(int numNodes, double connectionProbability, 
                    double linkFidelity = 0.95,
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
    Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (owners[i], owners[i+1]);
    // Set the depolarization model for actual fidelity calculation
    channel->SetDepolarModel(linkFidelity, qphyent);
    channels.push_back(channel);
  }
  
  // 随机添加额外连接
  for (int i = 0; i < numNodes; ++i)
  {
    for (int j = i + 2; j < numNodes; ++j) // 避免重复和相邻节点
    {
      if (randVar->GetValue(0, 1) < connectionProbability)
      {
        Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (owners[i], owners[j]);
        // Set the depolarization model for actual fidelity calculation
        channel->SetDepolarModel(linkFidelity, qphyent);
        channels.push_back(channel);
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
 * @param eprCapacity 每个通道的EPR对容量（默认10）
 * @return 性能结果
 */
PerformanceResult RunPerformanceTest(const std::string& topologyType,
                                    int numNodes,
                                    double linkSuccessRate,
                                    int concurrentRequests,
                                    const std::string& testName,
                                    unsigned eprCapacity = 10)
{
  NS_LOG_INFO("");
  NS_LOG_INFO("=================================================================");
  NS_LOG_INFO("开始性能测试: " << testName);
  NS_LOG_INFO("拓扑类型: " << topologyType);
  NS_LOG_INFO("节点数量: " << numNodes);
  NS_LOG_INFO("链路成功率: " << linkSuccessRate);
  NS_LOG_INFO("并发请求数: " << concurrentRequests);
  NS_LOG_INFO("EPR容量: " << eprCapacity);
  NS_LOG_INFO("=================================================================");
  
  // 创建拓扑
  std::vector<Ptr<QuantumNode>> nodes;
  std::vector<Ptr<QuantumChannel>> channels;
  int rows = 0, cols = 0;
  
  if (topologyType == "chain")
  {
    auto result = CreateChainTopology(numNodes, linkSuccessRate);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "grid")
  {
    // 计算网格尺寸
    rows = (int)std::sqrt(numNodes);
    cols = (numNodes + rows - 1) / rows; // 向上取整
    auto result = CreateGridTopology(rows, cols, linkSuccessRate);
    nodes = result.first;
    channels = result.second;
  }
  else if (topologyType == "random")
  {
    auto result = CreateRandomTopology(numNodes, 0.3, linkSuccessRate); // 30%连接概率
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
  
  // 创建Q-CAST转发引擎
  Ptr<QCastForwardingEngine> forwardingEngine = CreateObject<QCastForwardingEngine>();
  forwardingEngine->SetForwardingStrategy(QFS_ON_DEMAND);
  
  // 获取 QuantumPhyEntity 并配置物理层仿真
  // 所有节点共享同一个 QuantumPhyEntity（拓扑创建时已创建）
  if (!nodes.empty())
  {
    Ptr<QuantumPhyEntity> qphyent = nodes[0]->GetQuantumPhyEntity();
    if (qphyent)
    {
      NS_LOG_INFO("配置物理层仿真：启用 TimeModel 进行存储退相干仿真");
      
      // 为每个节点设置 TimeModel（存储退相干）
      // T2 coherence time: 100ms = 0.1 seconds
      // 使用 rate = T2 time (the larger, the slower decoherence)
      double t2CoherenceTime = 0.1;  // 100ms T2 coherence time
      for (size_t i = 0; i < nodes.size(); ++i)
      {
        std::string owner = GetNodeOwnerName(topologyType, i, rows, cols);
        qphyent->SetTimeModel(owner, t2CoherenceTime);
        NS_LOG_LOGIC("节点 " << owner << " 设置 TimeModel: T2=" << t2CoherenceTime << "s");
      }
      
      // 将 QuantumPhyEntity 传递给转发引擎
      // 这使得 DistributeEPR 和 PerformEntanglementSwap 可以调用实际的物理层操作
      forwardingEngine->SetQuantumPhyEntity(qphyent);
      NS_LOG_INFO("转发引擎已连接物理层：将执行实际 EPR 生成和纠缠交换仿真");
    }
    else
    {
      NS_LOG_WARN("无法获取 QuantumPhyEntity，物理层仿真将被禁用");
    }
  }
  
  // 获取资源管理器
  Ptr<QuantumResourceManager> resourceManager = QuantumResourceManager::GetDefaultResourceManager();
  
  // 设置每个通道的EPR容量
  for (const auto& channel : channels)
  {
    resourceManager->SetEPRCapacity(channel, eprCapacity);
  }
  NS_LOG_INFO("设置所有通道EPR容量为: " << eprCapacity);
  
  // 配置所有节点的网络层
  if (nodes.empty())
  {
    NS_LOG_ERROR("没有可用的节点");
    return PerformanceResult();
  }
  
  // 存储所有路由协议实例和网络层
  std::vector<Ptr<QCastRoutingProtocol>> allRoutingProtocols;
  std::vector<Ptr<QuantumNetworkLayer>> allNetworkLayers;
  Ptr<QCastRoutingProtocol> sourceRoutingProtocol = nullptr;
  Ptr<QuantumNetworkLayer> sourceNetworkLayer = nullptr;
  
  for (size_t i = 0; i < nodes.size(); ++i)
  {
    Ptr<QuantumNode> node = nodes[i];
    Ptr<QuantumNetworkLayer> networkLayer = node->GetQuantumNetworkLayer();
    
    if (!networkLayer)
    {
      NS_LOG_ERROR("无法获取节点 " << i << " 的网络层");
      continue;
    }
    
    // 设置节点地址
    std::string nodeAddress = GetNodeOwnerName(topologyType, i, rows, cols);
    networkLayer->SetAddress(nodeAddress);
    
    // 为每个节点创建独立的路由协议实例
    Ptr<QCastRoutingProtocol> nodeRoutingProtocol = CreateObject<QCastRoutingProtocol>();
    nodeRoutingProtocol->SetMetric(etMetric);
    nodeRoutingProtocol->SetKHopDistance(3);
    
    // Storage decoherence and classical delay parameters are now set by default:
    // - T2 = 100ms (quantum memory coherence time)
    // - Classical delay = 5ms per hop (simulates realistic network latency)
    // - Jitter = 50% (simulates background traffic variance)
    // These defaults cause significant fidelity reduction for multi-hop paths
    
    // 配置网络层
    networkLayer->SetRoutingProtocol(nodeRoutingProtocol);
    networkLayer->SetForwardingEngine(forwardingEngine);
    networkLayer->SetResourceManager(resourceManager);
    
    // 配置路由协议
    nodeRoutingProtocol->SetNetworkLayer(networkLayer);
    nodeRoutingProtocol->SetResourceManager(resourceManager);
    
    // 注册网络层到转发引擎注册表
    QCastForwardingEngine::RegisterNetworkLayer(nodeAddress, networkLayer);
    
    // 保存引用
    allRoutingProtocols.push_back(nodeRoutingProtocol);
    allNetworkLayers.push_back(networkLayer);
    
    // 如果是源节点（第一个节点），保存特殊引用
    if (i == 0)
    {
      sourceRoutingProtocol = nodeRoutingProtocol;
      sourceNetworkLayer = networkLayer;
      NS_LOG_INFO("配置源节点: " << nodeAddress);
    }
    else
    {
      NS_LOG_LOGIC("配置节点 " << i << ": " << nodeAddress);
    }
  }
  
  if (!sourceRoutingProtocol || !sourceNetworkLayer)
  {
    NS_LOG_ERROR("无法配置源节点");
    return PerformanceResult();
  }
  
  // 为后续使用设置引用
  Ptr<QCastRoutingProtocol> routingProtocol = sourceRoutingProtocol;
  Ptr<QuantumNetworkLayer> networkLayer = sourceNetworkLayer;
  
  // 性能收集器
  PerformanceCollector collector;
  
  // 执行邻居发现
  NS_LOG_INFO("执行邻居发现...");
  routingProtocol->DiscoverNeighbors();
  
  // Simulate topology exchange to accelerate convergence
  NS_LOG_INFO("模拟拓扑交换以加速收敛...");
  if (!allRoutingProtocols.empty())
  {
    allRoutingProtocols[0]->SimulateTopologyExchange(allNetworkLayers);
  }
  
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
    QuantumRoute route = routingProtocol->RouteRequest(sourceNetworkLayer->GetAddress(), 
                                                      destAddress, 
                                                      requirements);
    
    Time discoveryTime = Simulator::Now() - requestStartTime;
    
    if (route.IsValid())
    {
      // 使用路由协议计算的估计保真度（包括信道保真度和存储退相干）
      // route.estimatedFidelity 由 G-EDA 算法计算，考虑了：
      // 1. 信道保真度：每跳的去极化信道误差
      // 2. 存储退相干：log-time swap 调度期间的量子存储退相干
      double successProbability = route.estimatedFidelity;
      
      // 如果物理层仿真启用，将在 ForwardPacket 中执行实际的 EPR 生成和纠缠交换
      // 此时 successProbability 是估计值；实际保真度可通过 CalculateActualFidelity 获取
      NS_LOG_INFO("路由成功: " << sourceNetworkLayer->GetAddress() << " -> " << destAddress
                  << ", 跳数=" << route.GetHopCount()
                  << ", 估计保真度=" << successProbability);
      
      // 记录成功路由（使用估计保真度）
      collector.RecordSuccessfulRoute(route, discoveryTime, 1, successProbability);
      
      // 记录资源预留尝试
      collector.RecordResourceReservationAttempt(true);
      
      // 发送测试包 - 这会触发实际的物理层仿真（如果已启用）
      Ptr<QuantumPacket> packet = CreateObject<QuantumPacket>(sourceNetworkLayer->GetAddress(), destAddress);
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
  double memoryUtilization = resourceManager->GetMemoryUtilization(sourceNetworkLayer->GetAddress());
  collector.RecordResourceUtilization(memoryUtilization);
  
  // 计算性能结果
  PerformanceResult result = collector.GetResult(testName, 
                                                numNodes, 
                                                channels.size(),
                                                linkSuccessRate,
                                                concurrentRequests);
  
  // 收集实际保真度统计（来自物理层仿真）
  double avgActualFidelity = forwardingEngine->GetAverageActualFidelity();
  result.avgActualFidelity = avgActualFidelity;
  
  // 复制保真度统计到结果结构体
  auto fidelityStats = forwardingEngine->GetActualFidelityStats();
  for (const auto& stats : fidelityStats)
  {
    PerformanceResult::RouteFidelityEntry entry;
    entry.routeId = stats.routeId;
    entry.hopCount = stats.hopCount;
    entry.estimatedFidelity = stats.estimatedFidelity;
    entry.actualFidelity = stats.actualFidelity;
    entry.waitTimeMs = stats.waitTime.GetMilliSeconds();
    result.routeFidelityStats.push_back(entry);
  }
  
  // Print routing tables and topology for debugging
  NS_LOG_INFO("");
  NS_LOG_INFO("=== Debug Information ===");
  for (size_t i = 0; i < allRoutingProtocols.size(); ++i)
  {
    Ptr<QCastRoutingProtocol> proto = allRoutingProtocols[i];
    Ptr<QuantumNetworkLayer> layer = allNetworkLayers[i];
    
    if (proto && layer)
    {
      NS_LOG_INFO("");
      NS_LOG_INFO("--- Node " << layer->GetAddress() << " ---");
      proto->PrintRoutingTable();
      proto->PrintGlobalTopology();
    }
  }
  NS_LOG_INFO("=== End Debug Information ===");
  NS_LOG_INFO("");
  
  // 清理 ns-3 Simulator
  Simulator::Destroy();
  
  // 彻底重置 ExaTN 状态以便下一次测试
  // 使用 QuantumNetworkSimulator 的静态方法来正确重置 ExaTN
  // 这会销毁所有张量并重新初始化 ExaTN
  NS_LOG_INFO("清理 ExaTN 状态...");
  QuantumNetworkSimulator::ResetExaTNState();
  NS_LOG_INFO("ExaTN 状态重置完成");
  
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
  
  // 测试套件1：不同拓扑规模（10-100节点）和不同EPR容量（10, 20, 50）
  if (runAllTests || singleTest == "test1")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件1: 不同拓扑规模和EPR容量");
    
    // 网络规模：10, 25, 49, 64, 100节点
    std::vector<int> nodeCounts = {10, 25, 49, 64, 100};
    // EPR容量：10, 20, 50
    std::vector<unsigned> eprCapacities = {10, 20, 50};
    
    for (int numNodes : nodeCounts)
    {
      for (unsigned eprCap : eprCapacities)
      {
        // 网格拓扑测试
        std::string testName = "grid_" + std::to_string(numNodes) + "nodes_epr" + 
                              std::to_string(eprCap) + "_95pct_20req";
        PerformanceResult result = RunPerformanceTest("grid", numNodes, 0.95, 20, testName, eprCap);
        allResults.push_back(result);
        outputStream << result.ToCsvString() << std::endl;
      }
    }
  }
  
  // 测试套件2：随机拓扑测试（10-100节点）
  if (runAllTests || singleTest == "test2")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件2: 随机拓扑不同规模和EPR容量");
    
    std::vector<int> nodeCounts = {10, 25, 50, 75, 100};
    std::vector<unsigned> eprCapacities = {10, 20, 50};
    
    for (int numNodes : nodeCounts)
    {
      for (unsigned eprCap : eprCapacities)
      {
        std::string testName = "random_" + std::to_string(numNodes) + "nodes_epr" + 
                              std::to_string(eprCap) + "_95pct_20req";
        PerformanceResult result = RunPerformanceTest("random", numNodes, 0.95, 20, testName, eprCap);
        allResults.push_back(result);
        outputStream << result.ToCsvString() << std::endl;
      }
    }
  }
  
  // 测试套件3：链式拓扑测试（10-100节点）
  if (runAllTests || singleTest == "test3")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件3: 链式拓扑不同规模和EPR容量");
    
    std::vector<int> nodeCounts = {10, 25, 50, 75, 100};
    std::vector<unsigned> eprCapacities = {10, 20, 50};
    
    for (int numNodes : nodeCounts)
    {
      for (unsigned eprCap : eprCapacities)
      {
        std::string testName = "chain_" + std::to_string(numNodes) + "nodes_epr" + 
                              std::to_string(eprCap) + "_95pct_20req";
        PerformanceResult result = RunPerformanceTest("chain", numNodes, 0.95, 20, testName, eprCap);
        allResults.push_back(result);
        outputStream << result.ToCsvString() << std::endl;
      }
    }
  }
  
  // 测试套件4：不同并发负载（固定64节点网格）
  if (runAllTests || singleTest == "test4")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件4: 不同并发负载");
    
    std::vector<int> concurrentRequests = {10, 20, 50, 100};
    std::vector<unsigned> eprCapacities = {10, 20, 50};
    
    for (unsigned eprCap : eprCapacities)
    {
      for (int reqCount : concurrentRequests)
      {
        std::string testName = "grid_64nodes_epr" + std::to_string(eprCap) + 
                              "_95pct_" + std::to_string(reqCount) + "req";
        PerformanceResult result = RunPerformanceTest("grid", 64, 0.95, reqCount, testName, eprCap);
        allResults.push_back(result);
        outputStream << result.ToCsvString() << std::endl;
      }
    }
  }
  
  // 测试套件5：不同链路保真度（验证物理层保真度参数）
  // 
  // *** 重要限制 ***
  // 由于 ExaTN 使用 MPI，而 MPI 只能在每个进程中初始化一次，
  // 在同一进程中运行多个测试可能导致张量状态干扰。
  // 
  // 建议使用 test5a, test5b 等独立测试来验证不同保真度：
  //   ./ns3 run "qcast-performance-test --single-test=test5a"  # 95%
  //   ./ns3 run "qcast-performance-test --single-test=test5b"  # 99%
  //
  // 或者使用 test5 只运行第一个子测试 (90%)
  if (runAllTests || singleTest == "test5")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件5: 不同链路保真度 (单路由验证)");
    NS_LOG_INFO("*** 注意：建议分别运行 test5a/test5b 以获得准确的保真度结果 ***");
    
    // 测试不同的链路保真度值：0.90, 0.95, 0.99
    std::vector<double> fidelities = {0.90, 0.95, 0.99};
    
    for (double fidelity : fidelities)
    {
      // 5节点链式拓扑，单个路由请求（避免多路由共享张量网络的问题）
      std::string testName = "chain_5nodes_epr20_" + 
                            std::to_string((int)(fidelity * 100)) + "pct_1req";
      PerformanceResult result = RunPerformanceTest("chain", 5, fidelity, 1, testName, 20);
      allResults.push_back(result);
      outputStream << result.ToCsvString() << std::endl;
    }
  }
  
  // 测试套件5a：单独测试95%链路保真度
  if (singleTest == "test5a")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件5a: 单独测试95%链路保真度");
    
    std::string testName = "chain_5nodes_epr20_95pct_1req_isolated";
    PerformanceResult result = RunPerformanceTest("chain", 5, 0.95, 1, testName, 20);
    allResults.push_back(result);
    outputStream << result.ToCsvString() << std::endl;
  }
  
  // 测试套件5b：单独测试99%链路保真度
  if (singleTest == "test5b")
  {
    NS_LOG_INFO("");
    NS_LOG_INFO("运行测试套件5b: 单独测试99%链路保真度");
    
    std::string testName = "chain_5nodes_epr20_99pct_1req_isolated";
    PerformanceResult result = RunPerformanceTest("chain", 5, 0.99, 1, testName, 20);
    allResults.push_back(result);
    outputStream << result.ToCsvString() << std::endl;
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