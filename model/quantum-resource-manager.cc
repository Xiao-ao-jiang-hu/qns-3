#include "ns3/quantum-basis.h"
#include "ns3/quantum-resource-manager.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-node.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumResourceManager");

// 内部默认资源管理器实现
class DefaultQuantumResourceManager : public QuantumResourceManager
{
private:
  // 节点资源信息
  struct NodeResources
  {
    unsigned totalQubits;
    unsigned reservedQubits;
    
    NodeResources() : totalQubits(100), reservedQubits(0) {} // 默认100个量子比特
    
    unsigned GetAvailableQubits() const { return totalQubits - reservedQubits; }
    double GetUtilization() const { 
      return totalQubits > 0 ? (double)reservedQubits / totalQubits : 0.0; 
    }
  };
  
  // 通道资源信息
  struct ChannelResources
  {
    unsigned totalEPRCapacity;
    unsigned reservedEPRPairs;
    
    ChannelResources() : totalEPRCapacity(10), reservedEPRPairs(0) {} // 默认10个EPR对
    
    unsigned GetAvailableEPRCapacity() const { return totalEPRCapacity - reservedEPRPairs; }
    double GetUtilization() const { 
      return totalEPRCapacity > 0 ? (double)reservedEPRPairs / totalEPRCapacity : 0.0; 
    }
  };
  
  std::map<std::string, NodeResources> m_nodeResources;
  std::map<Ptr<QuantumChannel>, ChannelResources> m_channelResources;
  
  ResourceAvailableCallback m_resourceAvailableCallback;
  ResourceDepletedCallback m_resourceDepletedCallback;
  
  // 定时器相关
  struct ReservationRecord
  {
    std::string node;
    Ptr<QuantumChannel> channel;
    unsigned count;
    Time expiration;
    bool isQubit; // true for qubits, false for EPR pairs
  };
  
  std::vector<ReservationRecord> m_activeReservations;
  
public:
  DefaultQuantumResourceManager()
  {
    NS_LOG_LOGIC("Creating DefaultQuantumResourceManager");
  }
  
  ~DefaultQuantumResourceManager()
  {
    NS_LOG_LOGIC("Destroying DefaultQuantumResourceManager");
  }
  
  static TypeId GetTypeId(void)
  {
    static TypeId tid = TypeId("ns3::DefaultQuantumResourceManager")
      .SetParent<QuantumResourceManager>()
      .AddConstructor<DefaultQuantumResourceManager>();
    return tid;
  }
  
  bool ReserveQubits(const std::string& node, unsigned count, Time duration) override
  {
    NS_LOG_LOGIC("ReserveQubits for node " << node << ", count=" << count);
    
    if (count == 0) return true;
    
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      // 自动创建节点资源记录
      m_nodeResources[node] = NodeResources();
      it = m_nodeResources.find(node);
    }
    
    NodeResources& resources = it->second;
    if (resources.GetAvailableQubits() < count)
    {
      NS_LOG_WARN("Insufficient qubits on node " << node 
                  << ": requested=" << count 
                  << ", available=" << resources.GetAvailableQubits());
      return false;
    }
    
    resources.reservedQubits += count;
    
    // 记录预约以便自动释放
    if (duration > Time(0))
    {
      ReservationRecord record;
      record.node = node;
      record.channel = nullptr;
      record.count = count;
      record.expiration = Simulator::Now() + duration;
      record.isQubit = true;
      m_activeReservations.push_back(record);
      
      // 安排自动释放
      Simulator::Schedule(duration, &DefaultQuantumResourceManager::ReleaseExpiredReservation, 
                         this, m_activeReservations.size() - 1);
    }
    
    // 检查是否触发资源耗尽回调
    if (resources.GetAvailableQubits() == 0 && !m_resourceDepletedCallback.IsNull())
    {
      m_resourceDepletedCallback(node);
    }
    
    return true;
  }
  
  bool ReserveEPRPairs(Ptr<QuantumChannel> channel, unsigned count, Time duration) override
  {
    NS_LOG_LOGIC("ReserveEPRPairs for channel, count=" << count);
    
    if (!channel || count == 0) return true;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      // 自动创建通道资源记录
      m_channelResources[channel] = ChannelResources();
      it = m_channelResources.find(channel);
    }
    
    ChannelResources& resources = it->second;
    if (resources.GetAvailableEPRCapacity() < count)
    {
      NS_LOG_WARN("Insufficient EPR capacity on channel"
                  << ": requested=" << count 
                  << ", available=" << resources.GetAvailableEPRCapacity());
      return false;
    }
    
    resources.reservedEPRPairs += count;
    
    // 记录预约以便自动释放
    if (duration > Time(0))
    {
      ReservationRecord record;
      record.node = "";
      record.channel = channel;
      record.count = count;
      record.expiration = Simulator::Now() + duration;
      record.isQubit = false;
      m_activeReservations.push_back(record);
      
      // 安排自动释放
      Simulator::Schedule(duration, &DefaultQuantumResourceManager::ReleaseExpiredReservation, 
                         this, m_activeReservations.size() - 1);
    }
    
    // 检查是否触发资源耗尽回调
    if (resources.GetAvailableEPRCapacity() == 0 && !m_resourceDepletedCallback.IsNull())
    {
      std::string channelId = channel->GetSrcOwner() + "->" + channel->GetDstOwner();
      m_resourceDepletedCallback(channelId);
    }
    
    return true;
  }
  
  void ReleaseQubits(const std::string& node, unsigned count) override
  {
    NS_LOG_LOGIC("ReleaseQubits for node " << node << ", count=" << count);
    
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      NS_LOG_WARN("Attempt to release qubits from unknown node " << node);
      return;
    }
    
    NodeResources& resources = it->second;
    if (count > resources.reservedQubits)
    {
      NS_LOG_WARN("Attempt to release more qubits than reserved: "
                  << "requested=" << count << ", reserved=" << resources.reservedQubits);
      resources.reservedQubits = 0;
    }
    else
    {
      resources.reservedQubits -= count;
    }
    
    // 检查是否触发资源可用回调
    if (resources.GetAvailableQubits() > 0 && !m_resourceAvailableCallback.IsNull())
    {
      m_resourceAvailableCallback(node, resources.GetAvailableQubits());
    }
  }
  
  void ReleaseEPRPairs(Ptr<QuantumChannel> channel, unsigned count) override
  {
    NS_LOG_LOGIC("ReleaseEPRPairs for channel, count=" << count);
    
    if (!channel) return;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      NS_LOG_WARN("Attempt to release EPR pairs from unknown channel");
      return;
    }
    
    ChannelResources& resources = it->second;
    if (count > resources.reservedEPRPairs)
    {
      NS_LOG_WARN("Attempt to release more EPR pairs than reserved: "
                  << "requested=" << count << ", reserved=" << resources.reservedEPRPairs);
      resources.reservedEPRPairs = 0;
    }
    else
    {
      resources.reservedEPRPairs -= count;
    }
    
    // 检查是否触发资源可用回调
    if (resources.GetAvailableEPRCapacity() > 0 && !m_resourceAvailableCallback.IsNull())
    {
      std::string channelId = channel->GetSrcOwner() + "->" + channel->GetDstOwner();
      m_resourceAvailableCallback(channelId, resources.GetAvailableEPRCapacity());
    }
  }
  
  unsigned GetAvailableQubits(const std::string& node) const override
  {
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      // 返回默认值
      NodeResources defaultResources;
      return defaultResources.GetAvailableQubits();
    }
    return it->second.GetAvailableQubits();
  }
  
  unsigned GetAvailableEPRCapacity(Ptr<QuantumChannel> channel) const override
  {
    if (!channel) return 0;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      // 返回默认值
      ChannelResources defaultResources;
      return defaultResources.GetAvailableEPRCapacity();
    }
    return it->second.GetAvailableEPRCapacity();
  }
  
  double GetMemoryUtilization(const std::string& node) const override
  {
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      return 0.0;
    }
    return it->second.GetUtilization();
  }
  
  double GetChannelUtilization(Ptr<QuantumChannel> channel) const override
  {
    if (!channel) return 0.0;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      return 0.0;
    }
    return it->second.GetUtilization();
  }
  
  unsigned GetTotalQubitCapacity(const std::string& node) const override
  {
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      NodeResources defaultResources;
      return defaultResources.totalQubits;
    }
    return it->second.totalQubits;
  }
  
  unsigned GetTotalEPRCapacity(Ptr<QuantumChannel> channel) const override
  {
    if (!channel) return 0;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      ChannelResources defaultResources;
      return defaultResources.totalEPRCapacity;
    }
    return it->second.totalEPRCapacity;
  }
  
  void SetQubitCapacity(const std::string& node, unsigned capacity) override
  {
    NS_LOG_LOGIC("SetQubitCapacity for node " << node << " to " << capacity);
    
    auto it = m_nodeResources.find(node);
    if (it == m_nodeResources.end())
    {
      m_nodeResources[node] = NodeResources();
      it = m_nodeResources.find(node);
    }
    
    NodeResources& resources = it->second;
    
    // 确保预留的量子比特数不超过新容量
    if (resources.reservedQubits > capacity)
    {
      NS_LOG_WARN("Reducing capacity below reserved qubits: "
                  << "reserved=" << resources.reservedQubits << ", new capacity=" << capacity);
      resources.reservedQubits = capacity;
    }
    
    resources.totalQubits = capacity;
  }
  
  void SetEPRCapacity(Ptr<QuantumChannel> channel, unsigned capacity) override
  {
    NS_LOG_LOGIC("SetEPRCapacity for channel to " << capacity);
    
    if (!channel) return;
    
    auto it = m_channelResources.find(channel);
    if (it == m_channelResources.end())
    {
      m_channelResources[channel] = ChannelResources();
      it = m_channelResources.find(channel);
    }
    
    ChannelResources& resources = it->second;
    
    // 确保预留的EPR对数不超过新容量
    if (resources.reservedEPRPairs > capacity)
    {
      NS_LOG_WARN("Reducing capacity below reserved EPR pairs: "
                  << "reserved=" << resources.reservedEPRPairs << ", new capacity=" << capacity);
      resources.reservedEPRPairs = capacity;
    }
    
    resources.totalEPRCapacity = capacity;
  }
  
  bool CheckRouteResources(const QuantumRoute& route,
                          const QuantumRouteRequirements& requirements) const override
  {
    // 简化实现：检查每个节点和通道是否有足够资源
    // 实际实现可能需要更复杂的检查
    
    if (!route.IsValid()) return false;
    
    // 检查源节点和目标节点的量子比特资源
    if (GetAvailableQubits(route.source) < requirements.numQubits)
    {
      NS_LOG_LOGIC("Insufficient qubits at source node " << route.source);
      return false;
    }
    
    if (GetAvailableQubits(route.destination) < requirements.numQubits)
    {
      NS_LOG_LOGIC("Insufficient qubits at destination node " << route.destination);
      return false;
    }
    
    // 检查每个通道的EPR对资源
    for (const auto& channel : route.path)
    {
      if (GetAvailableEPRCapacity(channel) < requirements.numQubits)
      {
        NS_LOG_LOGIC("Insufficient EPR capacity on channel "
                     << channel->GetSrcOwner() << "->" << channel->GetDstOwner());
        return false;
      }
    }
    
    return true;
  }
  
  bool ReserveRouteResources(const QuantumRoute& route,
                           const QuantumRouteRequirements& requirements) override
  {
    if (!CheckRouteResources(route, requirements))
    {
      NS_LOG_WARN("Route resources check failed");
      return false;
    }
    
    // 预约源节点和目标节点的量子比特
    if (!ReserveQubits(route.source, requirements.numQubits, requirements.duration))
    {
      NS_LOG_WARN("Failed to reserve qubits at source node " << route.source);
      return false;
    }
    
    if (!ReserveQubits(route.destination, requirements.numQubits, requirements.duration))
    {
      // 回滚源节点预约
      ReleaseQubits(route.source, requirements.numQubits);
      NS_LOG_WARN("Failed to reserve qubits at destination node " << route.destination);
      return false;
    }
    
    // 预约每个通道的EPR对
    std::vector<Ptr<QuantumChannel>> reservedChannels;
    for (const auto& channel : route.path)
    {
      if (!ReserveEPRPairs(channel, requirements.numQubits, requirements.duration))
      {
        NS_LOG_WARN("Failed to reserve EPR pairs on channel "
                     << channel->GetSrcOwner() << "->" << channel->GetDstOwner());
        // 回滚所有已预约的资源
        ReleaseQubits(route.source, requirements.numQubits);
        ReleaseQubits(route.destination, requirements.numQubits);
        for (auto& ch : reservedChannels)
        {
          ReleaseEPRPairs(ch, requirements.numQubits);
        }
        return false;
      }
      reservedChannels.push_back(channel);
    }
    
    return true;
  }
  
  void ReleaseRouteResources(const QuantumRoute& route) override
  {
    // 简化实现：假设需求信息已经存储或可以从其他地方获取
    // 实际实现需要跟踪每个路由的资源预约
    
    NS_LOG_LOGIC("ReleaseRouteResources for route " << route.source << "->" << route.destination);
    
    // 这里应该根据路由释放相应资源
    // 由于我们没有跟踪每个路由的具体预约，这里只是记录日志
    // 实际实现需要更复杂的资源跟踪
  }
  
  void SetResourceAvailableCallback(ResourceAvailableCallback cb) override
  {
    m_resourceAvailableCallback = cb;
  }
  
  void SetResourceDepletedCallback(ResourceDepletedCallback cb) override
  {
    m_resourceDepletedCallback = cb;
  }

private:
  void ReleaseExpiredReservation(size_t index)
  {
    if (index >= m_activeReservations.size()) return;
    
    ReservationRecord& record = m_activeReservations[index];
    if (record.isQubit)
    {
      ReleaseQubits(record.node, record.count);
    }
    else
    {
      ReleaseEPRPairs(record.channel, record.count);
    }
    
    // 标记为已处理
    record.count = 0;
  }
};

// QuantumResourceManager静态方法实现
Ptr<QuantumResourceManager> QuantumResourceManager::GetDefaultResourceManager()
{
  return CreateObject<DefaultQuantumResourceManager>();
}

// QuantumResourceManager基类TypeId
NS_OBJECT_ENSURE_REGISTERED (QuantumResourceManager);

TypeId
QuantumResourceManager::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::QuantumResourceManager")
    .SetParent<Object>()
    .SetGroupName("Quantum");
  return tid;
}

} // namespace ns3