#include "ns3/quantum-basis.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-node.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <string>
#include <vector>
#include <map>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumRoutingProtocol");

// 简单路由协议实现（静态路由）
class SimpleQuantumRoutingProtocol : public QuantumRoutingProtocol
{
private:
  Ptr<QuantumNetworkLayer> m_networkLayer;
  Ptr<QuantumMetric> m_metric;
  Ptr<QuantumResourceManager> m_resourceManager;
  
  std::vector<QuantumRouteEntry> m_routingTable;
  QuantumNetworkStats m_stats;
  
  Time m_routeExpirationTimeout;
  
  RouteAvailableCallback m_routeAvailableCallback;
  RouteExpiredCallback m_routeExpiredCallback;
  RouteUpdatedCallback m_routeUpdatedCallback;
  
  // 邻居信息
  struct NeighborInfo
  {
    Ptr<QuantumChannel> channel;
    Time lastSeen;
    double cost;
  };
  
  std::map<std::string, NeighborInfo> m_neighbors;
  
public:
  SimpleQuantumRoutingProtocol()
    : m_routeExpirationTimeout(Seconds(30.0))
  {
    NS_LOG_LOGIC("Creating SimpleQuantumRoutingProtocol");
    ResetStatistics();
  }
  
  ~SimpleQuantumRoutingProtocol()
  {
    NS_LOG_LOGIC("Destroying SimpleQuantumRoutingProtocol");
  }
  
  static TypeId GetTypeId(void)
  {
    static TypeId tid = TypeId("ns3::SimpleQuantumRoutingProtocol")
      .SetParent<QuantumRoutingProtocol>()
      .AddConstructor<SimpleQuantumRoutingProtocol>();
    return tid;
  }
  
  void DiscoverNeighbors() override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::DiscoverNeighbors");
    
    m_stats.routeRequests++;
    
    if (!m_networkLayer)
    {
      NS_LOG_WARN("No network layer set for neighbor discovery");
      return;
    }
    
    // 获取当前邻居通道
    std::vector<Ptr<QuantumChannel>> channels = m_networkLayer->GetNeighbors();
    
    // 更新邻居信息
    for (const auto& channel : channels)
    {
      std::string neighborAddr = (channel->GetSrcOwner() != m_networkLayer->GetAddress()) ?
                                channel->GetSrcOwner() : channel->GetDstOwner();
      
      NeighborInfo info;
      info.channel = channel;
      info.lastSeen = Simulator::Now();
      info.cost = m_metric ? m_metric->CalculateChannelCost(channel) : 1.0;
      
      m_neighbors[neighborAddr] = info;
      
      NS_LOG_LOGIC("Discovered neighbor: " << neighborAddr << " with cost " << info.cost);
    }
    
    // 根据邻居信息更新路由表
    UpdateRoutingTable();
  }
  
  void UpdateRoutingTable() override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::UpdateRoutingTable");
    
    // 清空旧路由表
    m_routingTable.clear();
    
    // 为每个邻居创建直接路由
    for (const auto& entry : m_neighbors)
    {
      const std::string& neighborAddr = entry.first;
      const NeighborInfo& info = entry.second;
      
      QuantumRouteEntry routeEntry;
      routeEntry.destination = neighborAddr;
      routeEntry.nextHop = neighborAddr;
      routeEntry.channel = info.channel;
      routeEntry.cost = info.cost;
      routeEntry.timestamp = Simulator::Now();
      routeEntry.sequenceNumber = m_stats.routeReplies;
      
      m_routingTable.push_back(routeEntry);
      
      NS_LOG_LOGIC("Added route to " << neighborAddr << " via " << routeEntry.nextHop
                    << " with cost " << routeEntry.cost);
    }
    
    // 调用路由更新回调
    if (!m_routeUpdatedCallback.IsNull())
    {
      // 这里简化处理：只通知一个虚拟路由
      QuantumRoute dummyRoute;
      m_routeUpdatedCallback(dummyRoute);
    }
    
    m_stats.routeReplies++;
  }
  
  QuantumRoute RouteRequest(const std::string& src, const std::string& dst,
                          const QuantumRouteRequirements& requirements) override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::RouteRequest from " 
                << src << " to " << dst);
    
    m_stats.routeRequests++;
    
    QuantumRoute route;
    route.source = src;
    route.destination = dst;
    
    // 简化实现：如果目的地是直接邻居，使用直接通道
    auto neighborIt = m_neighbors.find(dst);
    if (neighborIt != m_neighbors.end())
    {
      route.path.push_back(neighborIt->second.channel);
      route.totalCost = neighborIt->second.cost;
      route.estimatedFidelity = 1.0 - route.totalCost; // 简化假设
      route.estimatedDelay = MilliSeconds(route.totalCost * 1000);
      route.strategy = requirements.strategy;
      route.expirationTime = Simulator::Now() + m_routeExpirationTimeout;
      route.routeId = m_stats.routeReplies + 1;
      
      NS_LOG_LOGIC("Found direct route to neighbor " << dst);
      m_stats.routeReplies++;
      
      // 调用路由可用回调
      if (!m_routeAvailableCallback.IsNull())
      {
        m_routeAvailableCallback(route);
      }
      
      return route;
    }
    
    // 否则返回无效路由
    NS_LOG_WARN("No route found to " << dst);
    m_stats.routeErrors++;
    return route; // 返回无效路由
  }
  
  QuantumRoute GetRoute(const std::string& dst) const override
  {
    // 在路由表中查找
    for (const auto& entry : m_routingTable)
    {
      if (entry.destination == dst)
      {
        // 构建路由
        QuantumRoute route;
        route.source = m_networkLayer ? m_networkLayer->GetAddress() : "";
        route.destination = dst;
        route.path.push_back(entry.channel);
        route.totalCost = entry.cost;
        route.estimatedFidelity = 1.0 - entry.cost;
        route.estimatedDelay = MilliSeconds(entry.cost * 1000);
        route.strategy = QFS_ON_DEMAND;
        route.expirationTime = entry.timestamp + m_routeExpirationTimeout;
        route.routeId = entry.sequenceNumber;
        
        return route;
      }
    }
    
    // 返回无效路由
    return QuantumRoute();
  }
  
  std::vector<QuantumRouteEntry> GetAllRoutes() const override
  {
    return m_routingTable;
  }
  
  bool AddRoute(const QuantumRouteEntry& entry) override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::AddRoute to " << entry.destination);
    
    // 检查是否已存在到相同目的地的路由
    for (auto& existingEntry : m_routingTable)
    {
      if (existingEntry.destination == entry.destination)
      {
        // 更新现有路由（如果新路由的序列号更高或成本更低）
        if (entry.sequenceNumber > existingEntry.sequenceNumber ||
            entry.cost < existingEntry.cost)
        {
          existingEntry = entry;
          existingEntry.timestamp = Simulator::Now();
          
          NS_LOG_LOGIC("Updated existing route to " << entry.destination);
          return true;
        }
        else
        {
          NS_LOG_LOGIC("Existing route is better or equal, keeping it");
          return false;
        }
      }
    }
    
    // 添加新路由
    m_routingTable.push_back(entry);
    NS_LOG_LOGIC("Added new route to " << entry.destination);
    return true;
  }
  
  bool RemoveRoute(const std::string& dst) override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::RemoveRoute to " << dst);
    
    for (auto it = m_routingTable.begin(); it != m_routingTable.end(); ++it)
    {
      if (it->destination == dst)
      {
        m_routingTable.erase(it);
        NS_LOG_LOGIC("Removed route to " << dst);
        
        // 调用路由过期回调
        if (!m_routeExpiredCallback.IsNull())
        {
          m_routeExpiredCallback(dst);
        }
        
        return true;
      }
    }
    
    NS_LOG_WARN("Route to " << dst << " not found");
    return false;
  }
  
  bool HasRoute(const std::string& dst) const override
  {
    for (const auto& entry : m_routingTable)
    {
      if (entry.destination == dst)
      {
        return true;
      }
    }
    return false;
  }
  
  void SetMetric(Ptr<QuantumMetric> metric) override
  {
    m_metric = metric;
  }
  
  Ptr<QuantumMetric> GetMetric() const override
  {
    return m_metric;
  }
  
  void SetResourceManager(Ptr<QuantumResourceManager> manager) override
  {
    m_resourceManager = manager;
  }
  
  Ptr<QuantumResourceManager> GetResourceManager() const override
  {
    return m_resourceManager;
  }
  
  void SetNetworkLayer(Ptr<QuantumNetworkLayer> layer) override
  {
    m_networkLayer = layer;
  }
  
  Ptr<QuantumNetworkLayer> GetNetworkLayer() const override
  {
    return m_networkLayer;
  }
  
  void ReceivePacket(Ptr<QuantumPacket> packet) override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::ReceivePacket");
    
    if (!packet)
    {
      NS_LOG_WARN("Received null packet");
      return;
    }
    
    m_stats.packetsReceived++;
    
    // 简化实现：记录日志
    NS_LOG_LOGIC("Received routing packet: " << packet->ToString());
    
    // 这里应该处理路由协议包
    // 实际实现需要根据包类型更新路由信息
  }
  
  void SendPacket(Ptr<QuantumPacket> packet, const std::string& dst) override
  {
    NS_LOG_LOGIC("SimpleQuantumRoutingProtocol::SendPacket to " << dst);
    
    if (!packet)
    {
      NS_LOG_WARN("Attempt to send null packet");
      return;
    }
    
    if (m_networkLayer)
    {
      // 设置目的地址
      if (!dst.empty())
      {
        packet->SetDestinationAddress(dst);
      }
      
      // 通过网络层发送
      m_networkLayer->SendPacket(packet);
      m_stats.packetsSent++;
    }
    else
    {
      NS_LOG_WARN("No network layer set for sending packets");
    }
  }
  
  QuantumNetworkStats GetStatistics() const override
  {
    return m_stats;
  }
  
  void ResetStatistics() override
  {
    m_stats.Reset();
  }
  
  void SetRouteExpirationTimeout(Time timeout) override
  {
    m_routeExpirationTimeout = timeout;
  }
  
  Time GetRouteExpirationTimeout() const override
  {
    return m_routeExpirationTimeout;
  }
  
  void SetRouteAvailableCallback(RouteAvailableCallback cb) override
  {
    m_routeAvailableCallback = cb;
  }
  
  void SetRouteExpiredCallback(RouteExpiredCallback cb) override
  {
    m_routeExpiredCallback = cb;
  }
  
  void SetRouteUpdatedCallback(RouteUpdatedCallback cb) override
  {
    m_routeUpdatedCallback = cb;
  }
};

// QuantumRoutingProtocol静态方法实现
Ptr<QuantumRoutingProtocol> QuantumRoutingProtocol::GetDefaultRoutingProtocol()
{
  return CreateObject<SimpleQuantumRoutingProtocol>();
}

// QuantumRoutingProtocol基类TypeId
NS_OBJECT_ENSURE_REGISTERED (QuantumRoutingProtocol);

TypeId
QuantumRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::QuantumRoutingProtocol")
    .SetParent<Object>()
    .SetGroupName("Quantum");
  return tid;
}

} // namespace ns3