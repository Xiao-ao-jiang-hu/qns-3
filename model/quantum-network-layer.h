#ifndef QUANTUM_NETWORK_LAYER_H
#define QUANTUM_NETWORK_LAYER_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/callback.h"
#include "ns3/quantum-route-types.h"
#include "ns3/quantum-packet.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-forwarding-engine.h"
#include "ns3/quantum-resource-manager.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumNode;
class QuantumChannel;

/**
 * \brief Core quantum network layer implementation.
 * 
 * This class implements the quantum network layer (L3) that sits
 * between the quantum link layer (L2) and quantum transport layer (L4).
 * It provides packet forwarding, routing, and network management
 * functionality for quantum networks.
 */
class QuantumNetworkLayer : public Object
{
public:
  /**
   * \brief Create a quantum network layer.
   */
  QuantumNetworkLayer();
  
  virtual ~QuantumNetworkLayer();
  
  static TypeId GetTypeId(void);
  
  // 数据平面接口
  
  /**
   * \brief Send a quantum packet.
   * 
   * \param packet The packet to send
   * \return true if sent successfully, false otherwise
   */
  virtual bool SendPacket(Ptr<QuantumPacket> packet);
  
  /**
   * \brief Receive a quantum packet.
   * 
   * \param packet The packet received
   */
  virtual void ReceivePacket(Ptr<QuantumPacket> packet);
  
  /**
   * \brief Forward a quantum packet.
   * 
   * \param packet The packet to forward
   * \return true if forwarded successfully, false otherwise
   */
  virtual bool ForwardPacket(Ptr<QuantumPacket> packet);
  
  // 控制平面接口
  
  /**
   * \brief Set the routing protocol.
   * 
   * \param protocol The routing protocol to use
   */
  virtual void SetRoutingProtocol(Ptr<QuantumRoutingProtocol> protocol);
  
  /**
   * \brief Get the routing protocol.
   * 
   * \return The current routing protocol
   */
  virtual Ptr<QuantumRoutingProtocol> GetRoutingProtocol() const;
  
  /**
   * \brief Set the forwarding engine.
   * 
   * \param engine The forwarding engine to use
   */
  virtual void SetForwardingEngine(Ptr<QuantumForwardingEngine> engine);
  
  /**
   * \brief Get the forwarding engine.
   * 
   * \return The current forwarding engine
   */
  virtual Ptr<QuantumForwardingEngine> GetForwardingEngine() const;
  
  /**
   * \brief Set the resource manager.
   * 
   * \param manager The resource manager to use
   */
  virtual void SetResourceManager(Ptr<QuantumResourceManager> manager);
  
  /**
   * \brief Get the resource manager.
   * 
   * \return The current resource manager
   */
  virtual Ptr<QuantumResourceManager> GetResourceManager() const;
  
  // 邻居管理
  
  /**
   * \brief Add a neighbor.
   * 
   * \param channel The quantum channel to the neighbor
   * \return true if added successfully, false otherwise
   */
  virtual bool AddNeighbor(Ptr<QuantumChannel> channel);
  
  /**
   * \brief Remove a neighbor.
   * 
   * \param channel The quantum channel to remove
   * \return true if removed successfully, false otherwise
   */
  virtual bool RemoveNeighbor(Ptr<QuantumChannel> channel);
  
  /**
   * \brief Get all neighbors.
   * 
   * \return Vector of all neighbor channels
   */
  virtual std::vector<Ptr<QuantumChannel>> GetNeighbors() const;
  
  /**
   * \brief Check if a node is a neighbor.
   * 
   * \param nodeAddress The node address to check
   * \return true if the node is a neighbor, false otherwise
   */
  virtual bool IsNeighbor(const std::string& nodeAddress) const;
  
  // 地址管理
  
  /**
   * \brief Set the address of this node.
   * 
   * \param address The node address
   */
  virtual void SetAddress(const std::string& address);
  
  /**
   * \brief Get the address of this node.
   * 
   * \return The node address
   */
  virtual std::string GetAddress() const;
  
  /**
   * \brief Set the quantum node.
   * 
   * \param node The quantum node this layer belongs to
   */
  virtual void SetQuantumNode(Ptr<QuantumNode> node);
  
  /**
   * \brief Get the quantum node.
   * 
   * \return The quantum node this layer belongs to
   */
  virtual Ptr<QuantumNode> GetQuantumNode() const;
  
  // 统计信息
  
  /**
   * \brief Get network layer statistics.
   * 
   * \return Statistics structure
   */
  virtual QuantumNetworkStats GetStatistics() const;
  
  /**
   * \brief Reset statistics.
   */
  virtual void ResetStatistics();
  
  /**
   * \brief Get packet queue size.
   * 
   * \return Current queue size
   */
  virtual uint32_t GetQueueSize() const;
  
  /**
   * \brief Get queue capacity.
   * 
   * \return Queue capacity
   */
  virtual uint32_t GetQueueCapacity() const;
  
  /**
   * \brief Set queue capacity.
   * 
   * \param capacity New queue capacity
   */
  virtual void SetQueueCapacity(uint32_t capacity);
  
  // 配置
  
  /**
   * \brief Set the maximum packet lifetime.
   * 
   * \param lifetime Maximum packet lifetime
   */
  virtual void SetMaxPacketLifetime(Time lifetime);
  
  /**
   * \brief Get the maximum packet lifetime.
   * 
   * \return Maximum packet lifetime
   */
  virtual Time GetMaxPacketLifetime() const;
  
  /**
   * \brief Enable or disable packet logging.
   * 
   * \param enable true to enable, false to disable
   */
  virtual void SetPacketLogging(bool enable);
  
  /**
   * \brief Check if packet logging is enabled.
   * 
   * \return true if enabled, false otherwise
   */
  virtual bool IsPacketLoggingEnabled() const;
  
  // 事件回调
  
  typedef Callback<void, Ptr<QuantumPacket>> PacketSentCallback;
  typedef Callback<void, Ptr<QuantumPacket>> PacketReceivedCallback;
  typedef Callback<void, Ptr<QuantumPacket>> PacketForwardedCallback;
  typedef Callback<void, const std::string&> NeighborAddedCallback;
  typedef Callback<void, const std::string&> NeighborRemovedCallback;
  
  /**
   * \brief Set callback for packet sent events.
   */
  virtual void SetPacketSentCallback(PacketSentCallback cb);
  
  /**
   * \brief Set callback for packet received events.
   */
  virtual void SetPacketReceivedCallback(PacketReceivedCallback cb);
  
  /**
   * \brief Set callback for packet forwarded events.
   */
  virtual void SetPacketForwardedCallback(PacketForwardedCallback cb);
  
  /**
   * \brief Set callback for neighbor added events.
   */
  virtual void SetNeighborAddedCallback(NeighborAddedCallback cb);
  
  /**
   * \brief Set callback for neighbor removed events.
   */
  virtual void SetNeighborRemovedCallback(NeighborRemovedCallback cb);
  
private:
  // 内部实现方法
  bool ProcessPacket(Ptr<QuantumPacket> packet);
  bool RoutePacket(Ptr<QuantumPacket> packet);
  void UpdateNeighbors();
  void CleanupExpiredRoutes();
  void LogPacket(const std::string& action, Ptr<QuantumPacket> packet);
  
  // 成员变量
  std::string m_address;                            /**< Node address */
  Ptr<QuantumNode> m_quantumNode;                   /**< Associated quantum node */
  Ptr<QuantumRoutingProtocol> m_routingProtocol;    /**< Routing protocol */
  Ptr<QuantumForwardingEngine> m_forwardingEngine;  /**< Forwarding engine */
  Ptr<QuantumResourceManager> m_resourceManager;    /**< Resource manager */
  
  std::vector<Ptr<QuantumChannel>> m_neighbors;     /**< Neighbor channels */
  std::vector<Ptr<QuantumPacket>> m_packetQueue;    /**< Packet queue */
  
  QuantumNetworkStats m_stats;                      /**< Statistics */
  uint32_t m_queueCapacity;                         /**< Queue capacity */
  Time m_maxPacketLifetime;                         /**< Maximum packet lifetime */
  bool m_packetLogging;                             /**< Packet logging enabled */
  
  // 回调函数
  PacketSentCallback m_packetSentCallback;
  PacketReceivedCallback m_packetReceivedCallback;
  PacketForwardedCallback m_packetForwardedCallback;
  NeighborAddedCallback m_neighborAddedCallback;
  NeighborRemovedCallback m_neighborRemovedCallback;
};

} // namespace ns3

#endif /* QUANTUM_NETWORK_LAYER_H */