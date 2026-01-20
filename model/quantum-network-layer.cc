#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-node.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumNetworkLayer");

NS_OBJECT_ENSURE_REGISTERED (QuantumNetworkLayer);

QuantumNetworkLayer::QuantumNetworkLayer()
    : m_address(""),
      m_quantumNode(nullptr),
      m_routingProtocol(nullptr),
      m_forwardingEngine(nullptr),
      m_resourceManager(nullptr),
      m_queueCapacity(1000),
      m_maxPacketLifetime(Seconds(30.0)),
      m_packetLogging(false)
{
  NS_LOG_LOGIC("Creating QuantumNetworkLayer");
}

QuantumNetworkLayer::~QuantumNetworkLayer()
{
  NS_LOG_LOGIC("Destroying QuantumNetworkLayer for " << m_address);
}

TypeId
QuantumNetworkLayer::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::QuantumNetworkLayer")
    .SetParent<Object>()
    .AddConstructor<QuantumNetworkLayer>()
    .AddAttribute("Address", "Address of this node",
                  StringValue(""),
                  MakeStringAccessor(&QuantumNetworkLayer::m_address),
                  MakeStringChecker())
    .AddAttribute("QueueCapacity", "Maximum packet queue capacity",
                  UintegerValue(1000),
                  MakeUintegerAccessor(&QuantumNetworkLayer::m_queueCapacity),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("MaxPacketLifetime", "Maximum lifetime for packets",
                  TimeValue(Seconds(30.0)),
                  MakeTimeAccessor(&QuantumNetworkLayer::m_maxPacketLifetime),
                  MakeTimeChecker())
    .AddAttribute("PacketLogging", "Enable packet logging",
                  BooleanValue(false),
                  MakeBooleanAccessor(&QuantumNetworkLayer::m_packetLogging),
                  MakeBooleanChecker());
  return tid;
}

// 数据平面接口
bool QuantumNetworkLayer::SendPacket(Ptr<QuantumPacket> packet)
{
  NS_LOG_LOGIC("QuantumNetworkLayer::SendPacket from " << m_address);
  
  if (!packet)
  {
    NS_LOG_WARN("Attempt to send null packet");
    return false;
  }
  
  // 设置源地址（如果未设置）
  if (packet->GetSourceAddress().empty())
  {
    packet->SetSourceAddress(m_address);
  }
  
  // 记录发送
  if (m_packetLogging)
  {
    LogPacket("SEND", packet);
  }
  
  // 更新统计信息
  m_stats.packetsSent++;
  
  // 调用回调
  if (!m_packetSentCallback.IsNull())
  {
    m_packetSentCallback(packet);
  }
  
  // 实际发送逻辑需要与转发引擎交互
  // 这里简化处理，直接调用转发引擎
  if (m_forwardingEngine)
  {
    return m_forwardingEngine->ForwardPacket(packet);
  }
  else
  {
    NS_LOG_WARN("No forwarding engine configured");
    return false;
  }
}

void QuantumNetworkLayer::ReceivePacket(Ptr<QuantumPacket> packet)
{
  NS_LOG_LOGIC("QuantumNetworkLayer::ReceivePacket at " << m_address);
  
  if (!packet)
  {
    NS_LOG_WARN("Received null packet");
    return;
  }
  
  // 检查包是否过期
  if (packet->HasExpired())
  {
    NS_LOG_WARN("Discarding expired packet");
    m_stats.packetsReceived++; // 仍计入统计
    return;
  }
  
  // 记录接收
  if (m_packetLogging)
  {
    LogPacket("RECV", packet);
  }
  
  // 更新统计信息
  m_stats.packetsReceived++;
  
  // 调用回调
  if (!m_packetReceivedCallback.IsNull())
  {
    m_packetReceivedCallback(packet);
  }
  
  // 处理包
  if (!ProcessPacket(packet))
  {
    NS_LOG_WARN("Failed to process packet");
  }
}

bool QuantumNetworkLayer::ForwardPacket(Ptr<QuantumPacket> packet)
{
  NS_LOG_LOGIC("QuantumNetworkLayer::ForwardPacket at " << m_address);
  
  if (!packet)
  {
    NS_LOG_WARN("Attempt to forward null packet");
    return false;
  }
  
  // 检查包是否过期
  if (packet->HasExpired())
  {
    NS_LOG_WARN("Discarding expired packet during forwarding");
    return false;
  }
  
  // 记录转发
  if (m_packetLogging)
  {
    LogPacket("FWD", packet);
  }
  
  // 更新统计信息
  m_stats.packetsForwarded++;
  
  // 调用回调
  if (!m_packetForwardedCallback.IsNull())
  {
    m_packetForwardedCallback(packet);
  }
  
  // 实际转发逻辑需要与转发引擎交互
  if (m_forwardingEngine)
  {
    return m_forwardingEngine->ForwardPacket(packet);
  }
  else
  {
    NS_LOG_WARN("No forwarding engine configured");
    return false;
  }
}

// 控制平面接口
void QuantumNetworkLayer::SetRoutingProtocol(Ptr<QuantumRoutingProtocol> protocol)
{
  m_routingProtocol = protocol;
  if (protocol)
  {
    protocol->SetNetworkLayer(this);
  }
}

Ptr<QuantumRoutingProtocol> QuantumNetworkLayer::GetRoutingProtocol() const
{
  return m_routingProtocol;
}

void QuantumNetworkLayer::SetForwardingEngine(Ptr<QuantumForwardingEngine> engine)
{
  m_forwardingEngine = engine;
  if (engine && m_resourceManager)
  {
    engine->SetResourceManager(m_resourceManager);
  }
}

Ptr<QuantumForwardingEngine> QuantumNetworkLayer::GetForwardingEngine() const
{
  return m_forwardingEngine;
}

void QuantumNetworkLayer::SetResourceManager(Ptr<QuantumResourceManager> manager)
{
  m_resourceManager = manager;
  if (manager && m_forwardingEngine)
  {
    m_forwardingEngine->SetResourceManager(manager);
  }
}

Ptr<QuantumResourceManager> QuantumNetworkLayer::GetResourceManager() const
{
  return m_resourceManager;
}

// 邻居管理
bool QuantumNetworkLayer::AddNeighbor(Ptr<QuantumChannel> channel)
{
  if (!channel)
  {
    NS_LOG_WARN("Attempt to add null channel as neighbor");
    return false;
  }
  
  // 检查是否已是邻居
  for (const auto& neighbor : m_neighbors)
  {
    if (neighbor == channel)
    {
      NS_LOG_LOGIC("Channel already added as neighbor");
      return false;
    }
  }
  
  m_neighbors.push_back(channel);
  NS_LOG_LOGIC("Added neighbor channel from " << channel->GetSrcOwner() 
               << " to " << channel->GetDstOwner());
  
  // 调用回调
  if (!m_neighborAddedCallback.IsNull())
  {
    std::string neighborAddr = (channel->GetSrcOwner() != m_address) ? 
                               channel->GetSrcOwner() : channel->GetDstOwner();
    m_neighborAddedCallback(neighborAddr);
  }
  
  return true;
}

bool QuantumNetworkLayer::RemoveNeighbor(Ptr<QuantumChannel> channel)
{
  if (!channel)
  {
    NS_LOG_WARN("Attempt to remove null channel");
    return false;
  }
  
  auto it = std::find(m_neighbors.begin(), m_neighbors.end(), channel);
  if (it != m_neighbors.end())
  {
    m_neighbors.erase(it);
    NS_LOG_LOGIC("Removed neighbor channel");
    
    // 调用回调
    if (!m_neighborRemovedCallback.IsNull())
    {
      std::string neighborAddr = (channel->GetSrcOwner() != m_address) ? 
                                 channel->GetSrcOwner() : channel->GetDstOwner();
      m_neighborRemovedCallback(neighborAddr);
    }
    
    return true;
  }
  
  NS_LOG_WARN("Channel not found in neighbors");
  return false;
}

std::vector<Ptr<QuantumChannel>> QuantumNetworkLayer::GetNeighbors() const
{
  return m_neighbors;
}

bool QuantumNetworkLayer::IsNeighbor(const std::string& nodeAddress) const
{
  for (const auto& channel : m_neighbors)
  {
    if (channel->GetSrcOwner() == nodeAddress || 
        channel->GetDstOwner() == nodeAddress)
    {
      return true;
    }
  }
  return false;
}

// 地址管理
void QuantumNetworkLayer::SetAddress(const std::string& address)
{
  m_address = address;
}

std::string QuantumNetworkLayer::GetAddress() const
{
  return m_address;
}

void QuantumNetworkLayer::SetQuantumNode(Ptr<QuantumNode> node)
{
  m_quantumNode = node;
}

Ptr<QuantumNode> QuantumNetworkLayer::GetQuantumNode() const
{
  return m_quantumNode;
}

// 统计信息
QuantumNetworkStats QuantumNetworkLayer::GetStatistics() const
{
  return m_stats;
}

void QuantumNetworkLayer::ResetStatistics()
{
  m_stats.Reset();
}

uint32_t QuantumNetworkLayer::GetQueueSize() const
{
  return m_packetQueue.size();
}

uint32_t QuantumNetworkLayer::GetQueueCapacity() const
{
  return m_queueCapacity;
}

void QuantumNetworkLayer::SetQueueCapacity(uint32_t capacity)
{
  m_queueCapacity = capacity;
  // 如果队列超出新容量，可能需要丢弃一些包
  while (m_packetQueue.size() > m_queueCapacity)
  {
    m_packetQueue.pop_back();
  }
}

// 配置
void QuantumNetworkLayer::SetMaxPacketLifetime(Time lifetime)
{
  m_maxPacketLifetime = lifetime;
}

Time QuantumNetworkLayer::GetMaxPacketLifetime() const
{
  return m_maxPacketLifetime;
}

void QuantumNetworkLayer::SetPacketLogging(bool enable)
{
  m_packetLogging = enable;
}

bool QuantumNetworkLayer::IsPacketLoggingEnabled() const
{
  return m_packetLogging;
}

// 回调设置
void QuantumNetworkLayer::SetPacketSentCallback(PacketSentCallback cb)
{
  m_packetSentCallback = cb;
}

void QuantumNetworkLayer::SetPacketReceivedCallback(PacketReceivedCallback cb)
{
  m_packetReceivedCallback = cb;
}

void QuantumNetworkLayer::SetPacketForwardedCallback(PacketForwardedCallback cb)
{
  m_packetForwardedCallback = cb;
}

void QuantumNetworkLayer::SetNeighborAddedCallback(NeighborAddedCallback cb)
{
  m_neighborAddedCallback = cb;
}

void QuantumNetworkLayer::SetNeighborRemovedCallback(NeighborRemovedCallback cb)
{
  m_neighborRemovedCallback = cb;
}

// 私有方法实现
bool QuantumNetworkLayer::ProcessPacket(Ptr<QuantumPacket> packet)
{
  // 简化处理逻辑：
  // 1. 如果是路由协议包，交给路由协议处理
  // 2. 如果是数据包，检查目的地
  // 3. 如果目的地是本节点，传递给上层
  // 4. 否则，尝试转发
  
  if (!packet) return false;
  
  std::string dstAddr = packet->GetDestinationAddress();
  
  // 检查是否为本节点
  if (dstAddr == m_address || dstAddr.empty())
  {
    // 本节点是目的地，传递给上层（应用层）
    NS_LOG_LOGIC("Packet destined for this node");
    // 这里应该调用上层回调或存储包供应用获取
    return true;
  }
  else
  {
    // 需要转发
    NS_LOG_LOGIC("Packet needs forwarding to " << dstAddr);
    return ForwardPacket(packet);
  }
}

bool QuantumNetworkLayer::RoutePacket(Ptr<QuantumPacket> packet)
{
  // 路由包逻辑
  if (!m_routingProtocol || !packet)
  {
    return false;
  }
  
  std::string dstAddr = packet->GetDestinationAddress();
  if (dstAddr.empty())
  {
    NS_LOG_WARN("Packet has no destination address");
    return false;
  }
  
  // 这里应该查询路由表或请求路由
  // 简化实现：如果有路由协议，让它处理
  if (m_routingProtocol->HasRoute(dstAddr))
  {
    QuantumRoute route = m_routingProtocol->GetRoute(dstAddr);
    packet->SetRoute(route);
    return true;
  }
  else
  {
    NS_LOG_WARN("No route to destination " << dstAddr);
    return false;
  }
}

void QuantumNetworkLayer::UpdateNeighbors()
{
  // 更新邻居信息，可以定时调用
  // 这里可以发送Hello消息或处理邻居发现
  NS_LOG_LOGIC("Updating neighbors");
}

void QuantumNetworkLayer::CleanupExpiredRoutes()
{
  // 清理过期路由
  if (m_routingProtocol)
  {
    // 路由协议应该有自己的清理逻辑
    NS_LOG_LOGIC("Cleaning up expired routes");
  }
}

void QuantumNetworkLayer::LogPacket(const std::string& action, Ptr<QuantumPacket> packet)
{
  if (!packet) return;
  
  NS_LOG_INFO(action << " packet: " << packet->ToString());
}

} // namespace ns3