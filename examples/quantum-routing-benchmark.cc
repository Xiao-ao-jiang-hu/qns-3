/*
 * 量子路由协议通用性能基准测试框架
 * 
 * 本框架用于评估不同量子网络路由协议的性能。
 * 支持的功能：
 * 1. 多种路由协议（Q-CAST等）
 * 2. 多种网络拓扑（链式、网格、随机）
 * 3. 可配置的测试参数
 * 4. 自动化的测试执行
 * 5. 格式化的结果输出
 * 6. 多个测试点的批量执行
 * 
 * 架构设计：
 * 1. 拓扑工厂 - 创建不同网络拓扑
 * 2. 协议工厂 - 创建不同路由协议
 * 3. 测试配置 - 定义测试参数
 * 4. 测试运行器 - 执行测试并收集结果
 * 5. 结果输出 - 格式化输出到文件
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
#include <memory>
#include <functional>

NS_LOG_COMPONENT_DEFINE ("QuantumRoutingBenchmark");

using namespace ns3;

// ===========================================================================
// 配置结构体
// ===========================================================================

/**
 * \brief 路由协议配置
 */
struct RoutingProtocolConfig
{
  std::string protocolName;           /**< 协议名称 */
  std::string className;              /**< C++类名 */
  std::map<std::string, std::string> parameters; /**< 协议参数 */
  
  RoutingProtocolConfig(const std::string& name = "", 
                       const std::string& cls = "")
    : protocolName(name), className(cls)
  {}
};

/**
 * \brief 拓扑配置
 */
struct TopologyConfig
{
  std::string type;                   /**< 拓扑类型: chain, grid, random */
  int numNodes;                       /**< 节点数量 */
  int rows;                           /**< 网格行数（仅网格拓扑） */
  int cols;                           /**< 网格列数（仅网格拓扑） */
  double connectionProbability;       /**< 随机拓扑连接概率 */
  double linkFidelity;                /**< 链路保真度 */
  
  TopologyConfig(const std::string& t = "chain", int n = 10)
    : type(t), numNodes(n), rows(0), cols(0), 
      connectionProbability(0.3), linkFidelity(0.95)
  {
    if (type == "grid")
    {
      rows = (int)std::sqrt(numNodes);
      cols = (numNodes + rows - 1) / rows;
    }
  }
};

/**
 * \brief 测试配置
 */
struct TestConfig
{
  std::string testName;               /**< 测试名称 */
  TopologyConfig topology;            /**< 拓扑配置 */
  RoutingProtocolConfig routingProtocol; /**< 路由协议配置 */
  int numRequests;                    /**< 路由请求数量 */
  double requestIntervalMin;          /**< 最小请求间隔（秒） */
  double requestIntervalMax;          /**< 最大请求间隔（秒） */
  double simulationDuration;          /**< 模拟持续时间（秒） */
  
  // 路由需求参数
  double minFidelity;                 /**< 最小保真度要求 */
  double maxDelay;                    /**< 最大延迟要求（秒） */
  int numQubits;                      /**< 所需量子比特数 */
  double duration;                    /**< 纠缠持续时间（秒） */
  
  TestConfig()
    : testName("default_test"),
      numRequests(10),
      requestIntervalMin(0.1),
      requestIntervalMax(5.0),
      simulationDuration(10.0),
      minFidelity(0.8),
      maxDelay(1.0),
      numQubits(2),
      duration(30.0)
  {}
};

// ===========================================================================
// 测试结果结构体
// ===========================================================================

/**
 * \brief 性能测试结果
 */
struct BenchmarkResult
{
  std::string testName;               /**< 测试名称 */
  std::string routingProtocol;        /**< 路由协议名称 */
  std::string topologyType;           /**< 拓扑类型 */
  int numNodes;                       /**< 节点数量 */
  int numLinks;                       /**< 链路数量 */
  double linkFidelity;                /**< 链路保真度 */
  int numRequests;                    /**< 请求数量 */
  
  // 路由性能指标
  int successfulRequests;             /**< 成功请求数 */
  double successRate;                 /**< 成功率 */
  double avgRouteTimeMs;              /**< 平均路由时间（毫秒） */
  double avgPathLength;               /**< 平均路径长度（跳数） */
  double avgEndToEndDelayMs;          /**< 平均端到端延迟（毫秒） */
  
  // 协议开销指标
  int routingPacketsSent;             /**< 路由包发送数 */
  int routingPacketsReceived;         /**< 路由包接收数 */
  int forwardingPackets;              /**< 转发包数 */
  
  // 资源使用指标
  double avgMemoryUtilization;        /**< 平均内存利用率 */
  double avgCpuUtilization;           /**< 平均CPU利用率 */
  
  // 时间统计
  double setupTimeMs;                 /**< 设置时间（毫秒） */
  double simulationTimeMs;            /**< 模拟时间（毫秒） */
  double totalTimeMs;                 /**< 总时间（毫秒） */
  
  BenchmarkResult()
    : successfulRequests(0),
      successRate(0.0),
      avgRouteTimeMs(0.0),
      avgPathLength(0.0),
      avgEndToEndDelayMs(0.0),
      routingPacketsSent(0),
      routingPacketsReceived(0),
      forwardingPackets(0),
      avgMemoryUtilization(0.0),
      avgCpuUtilization(0.0),
      setupTimeMs(0.0),
      simulationTimeMs(0.0),
      totalTimeMs(0.0)
  {}
  
  /**
   * \brief 获取CSV标题行
   */
  static std::string GetCsvHeader()
  {
    return "test_name,routing_protocol,topology_type,num_nodes,num_links,"
           "link_fidelity,num_requests,successful_requests,success_rate,"
           "avg_route_time_ms,avg_path_length,avg_delay_ms,"
           "routing_packets_sent,routing_packets_received,forwarding_packets,"
           "avg_memory_utilization,avg_cpu_utilization,"
           "setup_time_ms,simulation_time_ms,total_time_ms";
  }
  
  /**
   * \brief 转换为CSV格式字符串
   */
  std::string ToCsvString() const
  {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << testName << ",";
    ss << routingProtocol << ",";
    ss << topologyType << ",";
    ss << numNodes << ",";
    ss << numLinks << ",";
    ss << linkFidelity << ",";
    ss << numRequests << ",";
    ss << successfulRequests << ",";
    ss << successRate << ",";
    ss << avgRouteTimeMs << ",";
    ss << avgPathLength << ",";
    ss << avgEndToEndDelayMs << ",";
    ss << routingPacketsSent << ",";
    ss << routingPacketsReceived << ",";
    ss << forwardingPackets << ",";
    ss << avgMemoryUtilization << ",";
    ss << avgCpuUtilization << ",";
    ss << setupTimeMs << ",";
    ss << simulationTimeMs << ",";
    ss << totalTimeMs;
    
    return ss.str();
  }
  
  /**
   * \brief 打印详细报告
   */
  void PrintReport() const
  {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n================================================" << std::endl;
    std::cout << "性能基准测试结果: " << testName << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "配置信息:" << std::endl;
    std::cout << "  路由协议: " << routingProtocol << std::endl;
    std::cout << "  拓扑类型: " << topologyType << std::endl;
    std::cout << "  节点数量: " << numNodes << std::endl;
    std::cout << "  链路数量: " << numLinks << std::endl;
    std::cout << "  链路保真度: " << linkFidelity << std::endl;
    std::cout << "  请求数量: " << numRequests << std::endl;
    std::cout << "\n性能指标:" << std::endl;
    std::cout << "  成功请求: " << successfulRequests << "/" << numRequests 
              << " (" << successRate * 100 << "%)" << std::endl;
    std::cout << "  平均路由时间: " << avgRouteTimeMs << " ms" << std::endl;
    std::cout << "  平均路径长度: " << avgPathLength << " hops" << std::endl;
    std::cout << "  平均端到端延迟: " << avgEndToEndDelayMs << " ms" << std::endl;
    std::cout << "\n协议开销:" << std::endl;
    std::cout << "  路由包发送: " << routingPacketsSent << std::endl;
    std::cout << "  路由包接收: " << routingPacketsReceived << std::endl;
    std::cout << "  转发包数: " << forwardingPackets << std::endl;
    std::cout << "\n资源使用:" << std::endl;
    std::cout << "  平均内存利用率: " << avgMemoryUtilization * 100 << "%" << std::endl;
    std::cout << "  平均CPU利用率: " << avgCpuUtilization * 100 << "%" << std::endl;
    std::cout << "\n时间统计:" << std::endl;
    std::cout << "  设置时间: " << setupTimeMs << " ms" << std::endl;
    std::cout << "  模拟时间: " << simulationTimeMs << " ms" << std::endl;
    std::cout << "  总时间: " << totalTimeMs << " ms" << std::endl;
    std::cout << "================================================" << std::endl;
  }
};

// ===========================================================================
// 拓扑工厂
// ===========================================================================

/**
 * \brief 拓扑工厂类
 * 
 * 负责创建不同网络拓扑
 */
class TopologyFactory
{
public:
  /**
   * \brief 创建拓扑
   * 
   * @param config 拓扑配置
   * @return 节点向量和通道向量的对
   */
  static std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
  CreateTopology(const TopologyConfig& config)
  {
    if (config.type == "chain")
    {
      return CreateChainTopology(config.numNodes, config.linkFidelity);
    }
    else if (config.type == "grid")
    {
      return CreateGridTopology(config.rows, config.cols, config.linkFidelity);
    }
    else if (config.type == "random")
    {
      return CreateRandomTopology(config.numNodes, config.connectionProbability, 
                                 config.linkFidelity);
    }
    else
    {
      NS_LOG_ERROR("未知拓扑类型: " << config.type);
      return {};
    }
  }
  
private:
  /**
   * \brief 创建链式拓扑
   */
  static std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
  CreateChainTopology(int numNodes, double linkFidelity, 
                     const std::string& nodePrefix = "Node")
  {
    std::vector<std::string> owners;
    for (int i = 0; i < numNodes; ++i)
    {
      owners.push_back(nodePrefix + std::to_string(i));
    }
    
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
    
    std::vector<Ptr<QuantumNode>> nodes;
    for (const auto& owner : owners)
    {
      nodes.push_back(qphyent->GetNode(owner));
    }
    
    std::vector<Ptr<QuantumChannel>> channels;
    for (int i = 0; i < numNodes - 1; ++i)
    {
      Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[i], owners[i+1]);
      channel->SetDepolarModel(linkFidelity, qphyent);
      channels.push_back(channel);
    }
    
    return {nodes, channels};
  }
  
  /**
   * \brief 创建网格拓扑
   */
  static std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
  CreateGridTopology(int rows, int cols, double linkFidelity,
                    const std::string& nodePrefix = "Node")
  {
    std::vector<std::string> owners;
    for (int r = 0; r < rows; ++r)
    {
      for (int c = 0; c < cols; ++c)
      {
        owners.push_back(nodePrefix + std::to_string(r) + "_" + std::to_string(c));
      }
    }
    
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
    
    std::vector<Ptr<QuantumNode>> nodes;
    for (const auto& owner : owners)
    {
      nodes.push_back(qphyent->GetNode(owner));
    }
    
    std::vector<Ptr<QuantumChannel>> channels;
    
    // 创建水平连接
    for (int r = 0; r < rows; ++r)
    {
      for (int c = 0; c < cols - 1; ++c)
      {
        int idx1 = r * cols + c;
        int idx2 = r * cols + c + 1;
        Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[idx1], owners[idx2]);
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
        Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[idx1], owners[idx2]);
        channel->SetDepolarModel(linkFidelity, qphyent);
        channels.push_back(channel);
      }
    }
    
    return {nodes, channels};
  }
  
  /**
   * \brief 创建随机拓扑
   */
  static std::pair<std::vector<Ptr<QuantumNode>>, std::vector<Ptr<QuantumChannel>>>
  CreateRandomTopology(int numNodes, double connectionProbability, 
                      double linkFidelity, const std::string& nodePrefix = "Node")
  {
    std::vector<std::string> owners;
    for (int i = 0; i < numNodes; ++i)
    {
      owners.push_back(nodePrefix + std::to_string(i));
    }
    
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
    
    std::vector<Ptr<QuantumNode>> nodes;
    for (const auto& owner : owners)
    {
      nodes.push_back(qphyent->GetNode(owner));
    }
    
    std::vector<Ptr<QuantumChannel>> channels;
    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    
    // 确保至少是连通图
    for (int i = 0; i < numNodes - 1; ++i)
    {
      Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[i], owners[i+1]);
      channel->SetDepolarModel(linkFidelity, qphyent);
      channels.push_back(channel);
    }
    
    // 添加随机连接
    for (int i = 0; i < numNodes; ++i)
    {
      for (int j = i + 1; j < numNodes; ++j)
      {
        if (rand->GetValue(0.0, 1.0) < connectionProbability && 
            std::abs(i - j) > 1) // 避免重复添加直接邻居
        {
          Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(owners[i], owners[j]);
          channel->SetDepolarModel(linkFidelity, qphyent);
          channels.push_back(channel);
        }
      }
    }
    
    return {nodes, channels};
  }
};

// ===========================================================================
// 路由协议工厂
// ===========================================================================

/**
 * \brief 路由协议工厂类
 * 
 * 负责创建和配置不同路由协议
 */
class RoutingProtocolFactory
{
public:
  /**
   * \brief 创建路由协议
   * 
   * @param config 路由协议配置
   * @param linkFidelity 链路保真度（用于度量计算）
   * @return 路由协议指针
   */
  static Ptr<QuantumRoutingProtocol>
  CreateRoutingProtocol(const RoutingProtocolConfig& config, double linkFidelity)
  {
    if (config.protocolName == "QCAST" || config.className == "QCastRoutingProtocol")
    {
      return CreateQCastRoutingProtocol(config, linkFidelity);
    }
    else
    {
      NS_LOG_ERROR("未知路由协议: " << config.protocolName);
      return nullptr;
    }
  }
  
  /**
   * \brief 创建转发引擎
   * 
   * @param config 路由协议配置
   * @return 转发引擎指针
   */
  static Ptr<QuantumForwardingEngine>
  CreateForwardingEngine(const RoutingProtocolConfig& config)
  {
    if (config.protocolName == "QCAST" || config.className == "QCastRoutingProtocol")
    {
      return CreateQCastForwardingEngine(config);
    }
    else
    {
      NS_LOG_ERROR("未知路由协议: " << config.protocolName);
      return nullptr;
    }
  }
  
private:
  /**
   * \brief 创建Q-CAST路由协议
   */
  static Ptr<QCastRoutingProtocol>
  CreateQCastRoutingProtocol(const RoutingProtocolConfig& config, double linkFidelity)
  {
    // 创建期望吞吐量度量
    Ptr<ExpectedThroughputMetric> etMetric = CreateObject<ExpectedThroughputMetric>();
    etMetric->SetLinkSuccessRate(linkFidelity);
    
    // 创建Q-CAST路由协议
    Ptr<QCastRoutingProtocol> routingProtocol = CreateObject<QCastRoutingProtocol>();
    routingProtocol->SetMetric(etMetric);
    
    // 配置参数
    auto it = config.parameters.find("kHopDistance");
    if (it != config.parameters.end())
    {
      routingProtocol->SetKHopDistance(std::stoi(it->second));
    }
    else
    {
      routingProtocol->SetKHopDistance(3); // 默认值
    }
    
    return routingProtocol;
  }
  
  /**
   * \brief 创建Q-CAST转发引擎
   */
  static Ptr<QCastForwardingEngine>
  CreateQCastForwardingEngine(const RoutingProtocolConfig& config)
  {
    Ptr<QCastForwardingEngine> forwardingEngine = CreateObject<QCastForwardingEngine>();
    
    // 配置参数
    auto it = config.parameters.find("forwardingStrategy");
    if (it != config.parameters.end())
    {
      if (it->second == "ON_DEMAND")
        forwardingEngine->SetForwardingStrategy(QFS_ON_DEMAND);
      else if (it->second == "PRE_RESERVED")
        forwardingEngine->SetForwardingStrategy(QFS_PRE_ESTABLISHED);
      else if (it->second == "ADAPTIVE")
        forwardingEngine->SetForwardingStrategy(QFS_HYBRID);
    }
    else
    {
      forwardingEngine->SetForwardingStrategy(QFS_ON_DEMAND); // 默认值
    }
    
    return forwardingEngine;
  }
};

// ===========================================================================
// 测试运行器
// ===========================================================================

/**
 * \brief 测试运行器类
 * 
 * 负责执行单个测试并收集结果
 */
class TestRunner
{
public:
  /**
   * \brief 运行测试
   * 
   * @param config 测试配置
   * @return 测试结果
   */
  static BenchmarkResult RunTest(const TestConfig& config)
  {
    BenchmarkResult result;
    result.testName = config.testName;
    result.routingProtocol = config.routingProtocol.protocolName;
    result.topologyType = config.topology.type;
    result.numNodes = config.topology.numNodes;
    result.linkFidelity = config.topology.linkFidelity;
    result.numRequests = config.numRequests;
    
    Time startSetupTime = Simulator::Now();
    
    // 1. 创建拓扑
    auto topology = TopologyFactory::CreateTopology(config.topology);
    auto& nodes = topology.first;
    auto& channels = topology.second;
    
    result.numLinks = channels.size();
    
    if (nodes.empty())
    {
      NS_LOG_ERROR("无法创建拓扑");
      return result;
    }
    
    // 2. 创建经典网络连接
    NodeContainer nodeContainer;
    for (const auto& node : nodes)
    {
      nodeContainer.Add(node);
    }
    
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("1000kbps")));
    csmaHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
    NetDeviceContainer devices = csmaHelper.Install(nodeContainer);
    
    InternetStackHelper internet;
    internet.Install(nodeContainer);
    
    Ipv6AddressHelper address;
    address.SetBase("2001:1::", Ipv6Prefix(64));
    Ipv6InterfaceContainer interfaces = address.Assign(devices);
    
    // 3. 安装量子网络栈
    QuantumNetStackHelper qstack;
    qstack.Install(nodeContainer);
    
    // 4. 创建路由协议和转发引擎
    Ptr<QuantumRoutingProtocol> routingProtocol = 
      RoutingProtocolFactory::CreateRoutingProtocol(config.routingProtocol, 
                                                   config.topology.linkFidelity);
    
    Ptr<QuantumForwardingEngine> forwardingEngine = 
      RoutingProtocolFactory::CreateForwardingEngine(config.routingProtocol);
    
    if (!routingProtocol || !forwardingEngine)
    {
      NS_LOG_ERROR("无法创建路由协议或转发引擎");
      return result;
    }
    
    // 5. 获取资源管理器
    Ptr<QuantumResourceManager> resourceManager = 
      QuantumResourceManager::GetDefaultResourceManager();
    
    // 6. 为所有节点配置网络层
    for (auto& node : nodes)
    {
      Ptr<QuantumNetworkLayer> networkLayer = node->GetQuantumNetworkLayer();
      if (networkLayer)
      {
        networkLayer->SetRoutingProtocol(routingProtocol);
        networkLayer->SetForwardingEngine(forwardingEngine);
        networkLayer->SetResourceManager(resourceManager);
      }
    }
    
    routingProtocol->SetResourceManager(resourceManager);
    forwardingEngine->SetResourceManager(resourceManager);
    
    Time endSetupTime = Simulator::Now();
    result.setupTimeMs = (endSetupTime - startSetupTime).GetSeconds() * 1000;
    
    // 7. 执行邻居发现
    std::cout << "执行邻居发现..." << std::endl;
    routingProtocol->DiscoverNeighbors();
    
    // 8. 创建路由需求
    QuantumRouteRequirements requirements;
    requirements.minFidelity = config.minFidelity;
    requirements.maxDelay = Seconds(config.maxDelay);
    requirements.numQubits = config.numQubits;
    requirements.duration = Seconds(config.duration);
    requirements.strategy = QFS_ON_DEMAND;
    
    // 9. 安排路由请求
    Ptr<UniformRandomVariable> randVar = CreateObject<UniformRandomVariable>();
    
    struct RouteRequestResult
    {
      bool success;
      Time routeTime;
      int hopCount;
      Time endToEndDelay;
    };
    
    std::vector<RouteRequestResult> requestResults;
    requestResults.reserve(config.numRequests);
    
    for (int reqId = 0; reqId < config.numRequests; ++reqId)
    {
      // 确定请求时间
      double requestTime = config.requestIntervalMin + 
                          (config.requestIntervalMax - config.requestIntervalMin) *
                          (reqId / (double)config.numRequests);
      
      // 随机选择源和目的节点
      int srcIdx = randVar->GetInteger(0, nodes.size() - 1);
      int dstIdx;
      do {
        dstIdx = randVar->GetInteger(0, nodes.size() - 1);
      } while (dstIdx == srcIdx);
      
      Ptr<QuantumNode> srcNode = nodes[srcIdx];
      Ptr<QuantumNode> dstNode = nodes[dstIdx];
      
      Simulator::Schedule(Seconds(requestTime), [=, &requestResults]() {
        RouteRequestResult reqResult;
        reqResult.success = false;
        
        Ptr<QuantumNetworkLayer> networkLayer = srcNode->GetQuantumNetworkLayer();
        if (!networkLayer)
        {
          requestResults.push_back(reqResult);
          return;
        }
        
        // 执行路由请求
        Time startTime = Simulator::Now();
        QuantumRoute route = routingProtocol->RouteRequest(
          networkLayer->GetAddress(),
          dstNode->GetQuantumNetworkLayer()->GetAddress(),
          requirements);
        reqResult.routeTime = Simulator::Now() - startTime;
        
        if (route.IsValid())
        {
          reqResult.success = true;
          reqResult.hopCount = route.GetHopCount();
          reqResult.endToEndDelay = MilliSeconds(reqResult.hopCount * 10); // 简单估计
          
          // 发送测试包
          Ptr<QuantumPacket> packet = CreateObject<QuantumPacket>(
            networkLayer->GetAddress(),
            dstNode->GetQuantumNetworkLayer()->GetAddress());
          packet->SetType(QuantumPacket::DATA);
          packet->SetProtocol(QuantumPacket::PROTO_QUANTUM_FORWARDING);
          packet->SetRoute(route);
          packet->AddQubitReference("TestQubit1");
          packet->AddQubitReference("TestQubit2");
          
          networkLayer->SendPacket(packet);
        }
        
        requestResults.push_back(reqResult);
      });
    }
    
    // 10. 运行模拟
    Time startSimTime = Simulator::Now();
    Simulator::Stop(Seconds(config.simulationDuration));
    Simulator::Run();
    Time endSimTime = Simulator::Now();
    result.simulationTimeMs = (endSimTime - startSimTime).GetSeconds() * 1000;
    
    // 11. 收集结果
    int successfulRequests = 0;
    double totalRouteTime = 0.0;
    double totalPathLength = 0.0;
    double totalEndToEndDelay = 0.0;
    
    for (const auto& reqResult : requestResults)
    {
      if (reqResult.success)
      {
        successfulRequests++;
        totalRouteTime += reqResult.routeTime.GetSeconds() * 1000;
        totalPathLength += reqResult.hopCount;
        totalEndToEndDelay += reqResult.endToEndDelay.GetSeconds() * 1000;
      }
    }
    
    result.successfulRequests = successfulRequests;
    result.successRate = config.numRequests > 0 ? 
                        (double)successfulRequests / config.numRequests : 0.0;
    result.avgRouteTimeMs = successfulRequests > 0 ? 
                           totalRouteTime / successfulRequests : 0.0;
    result.avgPathLength = successfulRequests > 0 ? 
                          totalPathLength / successfulRequests : 0.0;
    result.avgEndToEndDelayMs = successfulRequests > 0 ? 
                               totalEndToEndDelay / successfulRequests : 0.0;
    
    // 12. 收集协议开销统计
    QuantumNetworkStats routingStats = routingProtocol->GetStatistics();
    QuantumNetworkStats forwardingStats = forwardingEngine->GetStatistics();
    
    result.routingPacketsSent = routingStats.packetsSent;
    result.routingPacketsReceived = routingStats.packetsReceived;
    result.forwardingPackets = forwardingStats.packetsForwarded;
    
    // 13. 清理
    Simulator::Destroy();
    
    result.totalTimeMs = result.setupTimeMs + result.simulationTimeMs;
    
    return result;
  }
};

// ===========================================================================
// 测试配置生成器
// ===========================================================================

/**
 * \brief 测试配置生成器类
 * 
 * 生成多个测试配置
 */
class TestConfigGenerator
{
public:
  /**
   * \brief 生成默认测试套件
   * 
   * @return 测试配置向量
   */
  static std::vector<TestConfig> GenerateDefaultTestSuite()
  {
    std::vector<TestConfig> configs;
    
    // 定义路由协议配置
    RoutingProtocolConfig qcastConfig("QCAST", "QCastRoutingProtocol");
    qcastConfig.parameters["kHopDistance"] = "3";
    qcastConfig.parameters["forwardingStrategy"] = "ON_DEMAND";
    
    // 套件1：不同拓扑规模（Q-CAST协议）
    std::vector<int> nodeCounts = {5, 10, 20, 30};
    for (int nodes : nodeCounts)
    {
      TestConfig config;
      config.testName = "qcast_chain_" + std::to_string(nodes) + "nodes";
      config.topology = TopologyConfig("chain", nodes);
      config.routingProtocol = qcastConfig;
      config.numRequests = 20;
      configs.push_back(config);
    }
    
    // 套件2：不同拓扑类型（固定20节点）
    std::vector<std::string> topologyTypes = {"chain", "grid", "random"};
    for (const auto& topology : topologyTypes)
    {
      TestConfig config;
      config.testName = "qcast_" + topology + "_20nodes";
      config.topology = TopologyConfig(topology, 20);
      config.routingProtocol = qcastConfig;
      config.numRequests = 20;
      configs.push_back(config);
    }
    
    // 套件3：不同链路质量
    std::vector<double> fidelities = {0.7, 0.8, 0.9, 0.95, 0.99};
    for (double fidelity : fidelities)
    {
      TestConfig config;
      config.testName = "qcast_grid_9nodes_fidelity" + 
                       std::to_string((int)(fidelity * 100));
      config.topology = TopologyConfig("grid", 9);
      config.topology.linkFidelity = fidelity;
      config.routingProtocol = qcastConfig;
      config.numRequests = 20;
      config.minFidelity = fidelity * 0.8; // 设置适当的最小保真度要求
      configs.push_back(config);
    }
    
    // 套件4：不同请求负载
    std::vector<int> requestCounts = {5, 10, 20, 50, 100};
    for (int requests : requestCounts)
    {
      TestConfig config;
      config.testName = "qcast_grid_9nodes_" + std::to_string(requests) + "requests";
      config.topology = TopologyConfig("grid", 9);
      config.routingProtocol = qcastConfig;
      config.numRequests = requests;
      configs.push_back(config);
    }
    
    return configs;
  }
  
  /**
   * \brief 从文件加载测试配置
   * 
   * @param filename 配置文件名
   * @return 测试配置向量
   */
  static std::vector<TestConfig> LoadFromFile(const std::string& filename)
  {
    std::vector<TestConfig> configs;
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
      std::cerr << "错误: 无法打开配置文件: " << filename << std::endl;
      return configs;
    }
    
    // CSV格式，支持Python生成器生成的所有字段
    // 字段顺序: test_name,topology_type,num_nodes,link_fidelity,protocol_name,num_requests,
    //           min_fidelity,max_delay,num_qubits,duration,simulation_duration,
    //           request_interval_min,request_interval_max
    std::string line;
    int lineNum = 0;
    bool isHeader = true;
    
    while (std::getline(file, line))
    {
      lineNum++;
      
      // 跳过空行和注释
      if (line.empty() || line[0] == '#')
        continue;
      
      // 跳过标题行
      if (isHeader)
      {
        isHeader = false;
        continue;
      }
      
      std::stringstream ss(line);
      std::string token;
      std::vector<std::string> tokens;
      
      while (std::getline(ss, token, ','))
      {
        tokens.push_back(token);
      }
      
      // 至少需要6个基本字段
      if (tokens.size() >= 6)
      {
        TestConfig config;
        
        // 基本字段（必填）
        config.testName = tokens[0];
        config.topology.type = tokens[1];
        config.topology.numNodes = std::stoi(tokens[2]);
        config.topology.linkFidelity = std::stod(tokens[3]);
        config.routingProtocol.protocolName = tokens[4];
        config.numRequests = std::stoi(tokens[5]);
        
        // 扩展字段（可选）
        if (tokens.size() >= 10)
        {
          config.minFidelity = std::stod(tokens[6]);
          config.maxDelay = std::stod(tokens[7]);
          config.numQubits = std::stoi(tokens[8]);
          config.duration = std::stod(tokens[9]);
        }
        
        if (tokens.size() >= 13)
        {
          config.simulationDuration = std::stod(tokens[10]);
          config.requestIntervalMin = std::stod(tokens[11]);
          config.requestIntervalMax = std::stod(tokens[12]);
        }
        
        // 设置路由协议类名
        if (config.routingProtocol.protocolName == "QCAST")
        {
          config.routingProtocol.className = "QCastRoutingProtocol";
          // 设置默认参数
          config.routingProtocol.parameters["kHopDistance"] = "3";
          config.routingProtocol.parameters["forwardingStrategy"] = "ON_DEMAND";
        }
        
        configs.push_back(config);
        
        if (lineNum <= 5) // 只打印前几个配置的调试信息
        {
          std::cout << "  加载配置: " << config.testName 
                    << " (" << config.topology.type << ", " 
                    << config.topology.numNodes << " nodes)" << std::endl;
        }
      }
      else
      {
        std::cerr << "警告: 配置文件第" << lineNum << "行格式错误: " << line << std::endl;
      }
    }
    
    file.close();
    
    if (configs.size() > 5)
    {
      std::cout << "  还有 " << (configs.size() - 5) << " 个配置..." << std::endl;
    }
    
    return configs;
  }
};

// ===========================================================================
// 结果管理器
// ===========================================================================

/**
 * \brief 结果管理器类
 * 
 * 负责管理和输出测试结果
 */
class ResultManager
{
public:
  /**
   * \brief 构造函数
   * 
   * @param outputFile 输出文件路径
   */
  ResultManager(const std::string& outputFile)
    : m_outputFile(outputFile)
  {
    // 确保输出目录存在
    size_t pos = outputFile.find_last_of('/');
    if (pos != std::string::npos)
    {
      std::string dir = outputFile.substr(0, pos);
      // 注意：这里假设目录已存在，实际中可能需要创建
    }
  }
  
  /**
   * \brief 保存结果到文件
   * 
   * @param results 结果向量
   * @return 是否成功
   */
  bool SaveResults(const std::vector<BenchmarkResult>& results)
  {
    std::ofstream file(m_outputFile);
    if (!file.is_open())
    {
      std::cerr << "错误: 无法打开输出文件: " << m_outputFile << std::endl;
      return false;
    }
    
    // 写入CSV标题
    file << BenchmarkResult::GetCsvHeader() << std::endl;
    
    // 写入数据
    for (const auto& result : results)
    {
      file << result.ToCsvString() << std::endl;
    }
    
    file.close();
    
    std::cout << "结果已保存到: " << m_outputFile << std::endl;
    std::cout << "记录数量: " << results.size() << std::endl;
    
    return true;
  }
  
  /**
   * \brief 打印汇总统计
   * 
   * @param results 结果向量
   */
  void PrintSummary(const std::vector<BenchmarkResult>& results)
  {
    if (results.empty())
    {
      std::cout << "没有结果可汇总" << std::endl;
      return;
    }
    
    std::cout << "\n================================================" << std::endl;
    std::cout << "测试汇总报告" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "总测试数: " << results.size() << std::endl;
    
    // 按路由协议分组
    std::map<std::string, std::vector<const BenchmarkResult*>> protocolGroups;
    for (const auto& result : results)
    {
      protocolGroups[result.routingProtocol].push_back(&result);
    }
    
    for (const auto& group : protocolGroups)
    {
      std::cout << "\n路由协议: " << group.first << std::endl;
      std::cout << "  测试数量: " << group.second.size() << std::endl;
      
      double avgSuccessRate = 0.0;
      double avgRouteTime = 0.0;
      double avgPathLength = 0.0;
      
      for (const auto& result : group.second)
      {
        avgSuccessRate += result->successRate;
        avgRouteTime += result->avgRouteTimeMs;
        avgPathLength += result->avgPathLength;
      }
      
      avgSuccessRate /= group.second.size();
      avgRouteTime /= group.second.size();
      avgPathLength /= group.second.size();
      
      std::cout << "  平均成功率: " << avgSuccessRate * 100 << "%" << std::endl;
      std::cout << "  平均路由时间: " << avgRouteTime << " ms" << std::endl;
      std::cout << "  平均路径长度: " << avgPathLength << " hops" << std::endl;
    }
    
    // 找到最佳和最差测试
    auto bestIt = std::max_element(results.begin(), results.end(),
      [](const BenchmarkResult& a, const BenchmarkResult& b) {
        return a.successRate < b.successRate;
      });
    
    auto worstIt = std::min_element(results.begin(), results.end(),
      [](const BenchmarkResult& a, const BenchmarkResult& b) {
        return a.successRate < b.successRate;
      });
    
    std::cout << "\n最佳性能测试: " << std::endl;
    std::cout << "  名称: " << bestIt->testName << std::endl;
    std::cout << "  成功率: " << bestIt->successRate * 100 << "%" << std::endl;
    
    std::cout << "\n最差性能测试: " << std::endl;
    std::cout << "  名称: " << worstIt->testName << std::endl;
    std::cout << "  成功率: " << worstIt->successRate * 100 << "%" << std::endl;
    
    std::cout << "================================================" << std::endl;
  }
  
private:
  std::string m_outputFile; /**< 输出文件路径 */
};

// ===========================================================================
// 主函数
// ===========================================================================

int main(int argc, char *argv[])
{
  // 解析命令行参数
  CommandLine cmd;
  std::string outputFile = "quantum_routing_benchmark.csv";
  std::string configFile = "";
  bool runDefaultTests = true;
  bool verbose = false;
  
  cmd.AddValue("output", "输出文件路径", outputFile);
  cmd.AddValue("config", "配置文件路径（CSV格式）", configFile);
  cmd.AddValue("default", "运行默认测试套件（true/false）", runDefaultTests);
  cmd.AddValue("verbose", "详细输出模式", verbose);
  cmd.Parse(argc, argv);
  
  std::cout << std::endl;
  std::cout << "================================================" << std::endl;
  std::cout << "量子路由协议性能基准测试框架" << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  if (!configFile.empty())
    std::cout << "配置文件: " << configFile << std::endl;
  std::cout << "================================================" << std::endl;
  
  // 生成测试配置
  std::vector<TestConfig> testConfigs;
  
  if (!configFile.empty())
  {
    std::cout << "从配置文件加载测试配置..." << std::endl;
    auto fileConfigs = TestConfigGenerator::LoadFromFile(configFile);
    testConfigs.insert(testConfigs.end(), fileConfigs.begin(), fileConfigs.end());
    std::cout << "从文件加载了 " << fileConfigs.size() << " 个测试配置" << std::endl;
  }
  
  if (runDefaultTests || testConfigs.empty())
  {
    std::cout << "生成默认测试套件..." << std::endl;
    auto defaultConfigs = TestConfigGenerator::GenerateDefaultTestSuite();
    testConfigs.insert(testConfigs.end(), defaultConfigs.begin(), defaultConfigs.end());
    std::cout << "生成了 " << defaultConfigs.size() << " 个默认测试配置" << std::endl;
  }
  
  if (testConfigs.empty())
  {
    std::cerr << "错误: 没有可运行的测试配置" << std::endl;
    return 1;
  }
  
  std::cout << "总测试配置数: " << testConfigs.size() << std::endl;
  std::cout << "开始执行测试..." << std::endl;
  
  // 执行测试
  std::vector<BenchmarkResult> results;
  int testCount = 0;
  
  for (const auto& config : testConfigs)
  {
    testCount++;
    std::cout << "\n------------------------------------------------" << std::endl;
    std::cout << "执行测试 " << testCount << "/" << testConfigs.size() << ": " 
              << config.testName << std::endl;
    std::cout << "  拓扑: " << config.topology.type << " (" 
              << config.topology.numNodes << " 节点)" << std::endl;
    std::cout << "  协议: " << config.routingProtocol.protocolName << std::endl;
    std::cout << "  请求数: " << config.numRequests << std::endl;
    
    try
    {
      BenchmarkResult result = TestRunner::RunTest(config);
      result.PrintReport();
      results.push_back(result);
      
      std::cout << "  完成: " << config.testName 
                << " (成功率: " << result.successRate * 100 << "%)" << std::endl;
    }
    catch (const std::exception& e)
    {
      std::cerr << "  错误: 测试执行失败 - " << e.what() << std::endl;
    }
    catch (...)
    {
      std::cerr << "  错误: 测试执行失败 - 未知异常" << std::endl;
    }
  }
  
  // 保存结果
  ResultManager resultManager(outputFile);
  if (!resultManager.SaveResults(results))
  {
    std::cerr << "错误: 无法保存结果到文件" << std::endl;
    return 1;
  }
  
  // 打印汇总
  resultManager.PrintSummary(results);
  
  std::cout << "\n================================================" << std::endl;
  std::cout << "量子路由协议性能基准测试完成" << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  std::cout << "总测试数: " << results.size() << "/" << testConfigs.size() << std::endl;
  std::cout << "================================================" << std::endl;
  
  return 0;
}