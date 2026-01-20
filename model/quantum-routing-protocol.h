#ifndef QUANTUM_ROUTING_PROTOCOL_H
#define QUANTUM_ROUTING_PROTOCOL_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/callback.h"
#include "ns3/quantum-route-types.h"
#include "ns3/quantum-metric.h"
#include "ns3/quantum-resource-manager.h"
#include "ns3/quantum-packet.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumChannel;
class QuantumNetworkLayer;

/**
 * \brief Abstract base class for quantum routing protocols.
 * 
 * This class defines the interface for quantum routing protocols.
 * Specific routing algorithms (e.g., Dijkstra, distance-vector)
 * should inherit from this class.
 */
class QuantumRoutingProtocol : public Object
{
public:
  /**
   * \brief Discover neighboring nodes.
   * 
   * This method initiates neighbor discovery, which may involve
   * sending hello messages or listening for neighbor advertisements.
   */
  virtual void DiscoverNeighbors() = 0;
  
  /**
   * \brief Update the routing table.
   * 
   * This method recalculates routing tables based on current
   * network state and received routing information.
   */
  virtual void UpdateRoutingTable() = 0;
  
  /**
   * \brief Handle a route request.
   * 
   * \param src Source node address
   * \param dst Destination node address
   * \param requirements Route requirements
   * \return The found route, or an invalid route if not found
   */
  virtual QuantumRoute RouteRequest(const std::string& src, const std::string& dst,
                                  const QuantumRouteRequirements& requirements) = 0;
  
  /**
   * \brief Get a route to a destination.
   * 
   * \param dst Destination node address
   * \return The best route to the destination
   */
  virtual QuantumRoute GetRoute(const std::string& dst) const = 0;
  
  /**
   * \brief Get all known routes.
   * 
   * \return Vector of all route entries
   */
  virtual std::vector<QuantumRouteEntry> GetAllRoutes() const = 0;
  
  /**
   * \brief Add a route to the routing table.
   * 
   * \param entry The route entry to add
   * \return true if added successfully, false otherwise
   */
  virtual bool AddRoute(const QuantumRouteEntry& entry) = 0;
  
  /**
   * \brief Remove a route from the routing table.
   * 
   * \param dst Destination node address
   * \return true if removed successfully, false otherwise
   */
  virtual bool RemoveRoute(const std::string& dst) = 0;
  
  /**
   * \brief Check if a route exists to a destination.
   * 
   * \param dst Destination node address
   * \return true if a route exists, false otherwise
   */
  virtual bool HasRoute(const std::string& dst) const = 0;
  
  /**
   * \brief Set the metric to use for route calculation.
   * 
   * \param metric The metric to use
   */
  virtual void SetMetric(Ptr<QuantumMetric> metric) = 0;
  
  /**
   * \brief Get the current metric.
   * 
   * \return The current metric
   */
  virtual Ptr<QuantumMetric> GetMetric() const = 0;
  
  /**
   * \brief Set the resource manager.
   * 
   * \param manager The resource manager to use
   */
  virtual void SetResourceManager(Ptr<QuantumResourceManager> manager) = 0;
  
  /**
   * \brief Get the resource manager.
   * 
   * \return The current resource manager
   */
  virtual Ptr<QuantumResourceManager> GetResourceManager() const = 0;
  
  /**
   * \brief Set the network layer.
   * 
   * \param layer The network layer this protocol belongs to
   */
  virtual void SetNetworkLayer(Ptr<QuantumNetworkLayer> layer) = 0;
  
  /**
   * \brief Get the network layer.
   * 
   * \return The network layer this protocol belongs to
   */
  virtual Ptr<QuantumNetworkLayer> GetNetworkLayer() const = 0;
  
  /**
   * \brief Handle receipt of a routing packet.
   * 
   * \param packet The received routing packet
   */
  virtual void ReceivePacket(Ptr<QuantumPacket> packet) = 0;
  
  /**
   * \brief Send a routing packet.
   * 
   * \param packet The routing packet to send
   * \param dst Destination address (broadcast if empty)
   */
  virtual void SendPacket(Ptr<QuantumPacket> packet, const std::string& dst = "") = 0;
  
  /**
   * \brief Get protocol statistics.
   * 
   * \return Statistics structure
   */
  virtual QuantumNetworkStats GetStatistics() const = 0;
  
  /**
   * \brief Reset protocol statistics.
   */
  virtual void ResetStatistics() = 0;
  
  /**
   * \brief Set the route expiration timeout.
   * 
   * \param timeout The new timeout value
   */
  virtual void SetRouteExpirationTimeout(Time timeout) = 0;
  
  /**
   * \brief Get the route expiration timeout.
   * 
   * \return The current timeout value
   */
  virtual Time GetRouteExpirationTimeout() const = 0;
  
  // Callback types
  typedef Callback<void, const QuantumRoute&> RouteAvailableCallback;
  typedef Callback<void, const std::string&> RouteExpiredCallback;
  typedef Callback<void, const QuantumRoute&> RouteUpdatedCallback;
  
  /**
   * \brief Set callback for route availability notifications.
   * 
   * \param cb The callback function
   */
  virtual void SetRouteAvailableCallback(RouteAvailableCallback cb) = 0;
  
  /**
   * \brief Set callback for route expiration notifications.
   * 
   * \param cb The callback function
   */
  virtual void SetRouteExpiredCallback(RouteExpiredCallback cb) = 0;
  
  /**
   * \brief Set callback for route update notifications.
   * 
   * \param cb The callback function
   */
  virtual void SetRouteUpdatedCallback(RouteUpdatedCallback cb) = 0;
  
  /**
   * \brief Get the TypeId.
   * 
   * \return The TypeId
   */
  static TypeId GetTypeId(void);

  /**
   * \brief Get a default routing protocol.
   * 
   * \return A default routing protocol instance
   */
  static Ptr<QuantumRoutingProtocol> GetDefaultRoutingProtocol();
};

} // namespace ns3

#endif /* QUANTUM_ROUTING_PROTOCOL_H */