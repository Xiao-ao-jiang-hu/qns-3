#ifndef QCAST_ROUTING_PROTOCOL_H
#define QCAST_ROUTING_PROTOCOL_H

#include "../quantum-routing-protocol.h"
#include "../quantum-metric.h"
#include "../quantum-resource-manager.h"
#include "../quantum-network-layer.h"
#include "qcast-route-types.h"
#include <map>
#include <vector>
#include <string>
#include <set>
#include <limits>

namespace ns3 {

/**
 * \brief Q-CAST routing protocol implementation
 *
 * Implements Phase P2 (online path selection) and Phase P3 (link state exchange)
 * of the Q-CAST protocol. Uses Greedy Extended Dijkstra Algorithm (G-EDA)
 * for online path selection with k-hop link state information (k=3).
 */
class QCastRoutingProtocol : public QuantumRoutingProtocol
{
public:
  QCastRoutingProtocol();
  ~QCastRoutingProtocol();

  static TypeId GetTypeId(void);

  // QuantumRoutingProtocol interface implementation
  void DiscoverNeighbors() override;
  void UpdateRoutingTable() override;
  QuantumRoute RouteRequest(const std::string& src,
                           const std::string& dst,
                           const QuantumRouteRequirements& requirements) override;
  QuantumRoute GetRoute(const std::string& dst) const override;
  std::vector<QuantumRouteEntry> GetAllRoutes() const override;
  bool AddRoute(const QuantumRouteEntry& entry) override;
  bool RemoveRoute(const std::string& dst) override;
  bool HasRoute(const std::string& dst) const override;
  void SetMetric(Ptr<QuantumMetric> metric) override;
  Ptr<QuantumMetric> GetMetric() const override;
  void SetResourceManager(Ptr<QuantumResourceManager> manager) override;
  Ptr<QuantumResourceManager> GetResourceManager() const override;
  void SetNetworkLayer(Ptr<QuantumNetworkLayer> layer) override;
  Ptr<QuantumNetworkLayer> GetNetworkLayer() const override;
  void ReceivePacket(Ptr<QuantumPacket> packet) override;
  void SendPacket(Ptr<QuantumPacket> packet, const std::string& dst) override;
  QuantumNetworkStats GetStatistics() const override;
  void ResetStatistics() override;
  void SetRouteExpirationTimeout(Time timeout) override;
  Time GetRouteExpirationTimeout() const override;
  void SetRouteAvailableCallback(RouteAvailableCallback cb) override;
  void SetRouteExpiredCallback(RouteExpiredCallback cb) override;
  void SetRouteUpdatedCallback(RouteUpdatedCallback cb) override;

  // Q-CAST specific methods
  /**
   * \brief Set the k-hop distance for link state exchange
   * \param k The k-hop distance (default is 3)
   */
  void SetKHopDistance(unsigned k);

  /**
   * \brief Get the k-hop distance
   * \return The current k-hop distance
   */
  unsigned GetKHopDistance() const;

  /**
   * \brief Get detailed Q-CAST route information
   * \param routeId The route ID
   * \return Q-CAST route information
   */
  QCastRouteInfo GetQCastRouteInfo(uint32_t routeId) const;

  /**
   * \brief Print routing table for debugging
   */
  void PrintRoutingTable() const;

  /**
   * \brief Print global topology for debugging
   */
  void PrintGlobalTopology() const;

  /**
   * \brief Direct topology simulation for testing
   * \param allLayers Vector of all network layers in the simulation
   */
  void SimulateTopologyExchange(const std::vector<Ptr<QuantumNetworkLayer>>& allLayers);

private:
  // Neighbor information structure
  struct NeighborInfo
  {
    Ptr<QuantumChannel> channel;
    Time lastSeen;
    double cost;
    LinkQuality linkQuality;
  };

  // Private helper methods
  void BuildResidualNetworkGraph();
  QuantumRoute GreedyExtendedDijkstra(const std::string& src,
                                     const std::string& dst,
                                     const QuantumRouteRequirements& requirements);
  std::vector<QuantumRoute> FindRecoveryPaths(const QuantumRoute& mainRoute,
                                             const QuantumRouteRequirements& requirements);
  void UpdateLinkState();
  double CalculateSuccessProbability(const QuantumRoute& mainRoute,
                                    const std::vector<QuantumRoute>& recoveryPaths) const;
  void BuildRecoveryRings(QCastRouteInfo& qcastInfo);
  
// Topology discovery methods
  void InitiateTopologyDiscovery();
  void GenerateTopologyLSA();
  void FloodTopologyLSA(const TopologyLSA& lsa);
  void ProcessTopologyLSA(Ptr<QuantumPacket> packet);
  void CheckTopologyConvergence();
  void HandleTopologyConverged();
  

  
  // Member variables
  Ptr<QuantumNetworkLayer> m_networkLayer;
  Ptr<QuantumMetric> m_metric;
  Ptr<QuantumResourceManager> m_resourceManager;

  std::vector<QuantumRouteEntry> m_routingTable;
  QuantumNetworkStats m_stats;

  Time m_routeExpirationTimeout;
  Time m_linkStateExchangeInterval;
  unsigned m_kHopDistance;  // k-hop distance (default is 3)

  RouteAvailableCallback m_routeAvailableCallback;
  RouteExpiredCallback m_routeExpiredCallback;
  RouteUpdatedCallback m_routeUpdatedCallback;

  // Q-CAST specific data structures
  KHopLinkState m_linkState;                    // k-hop link state
  std::map<uint32_t, QCastRouteInfo> m_qcastRoutes;  // Q-CAST route information
  ResidualNetworkGraph m_residualGraph;         // Residual network graph
  std::map<std::string, NeighborInfo> m_neighbors;  // Neighbor information
  
  // Topology discovery
  GlobalTopology m_globalTopology;              ///< Global network topology
  std::map<std::string, uint32_t> m_receivedSequenceNumbers;  ///< Latest LSA sequence from each node
  Time m_topologyDiscoveryInterval;             ///< Interval for topology discovery (5 seconds)
  bool m_topologyConverged;                     ///< Whether topology has converged
  uint32_t m_myLsaSequenceNumber;               ///< My current LSA sequence number
};

} // namespace ns3

#endif // QCAST_ROUTING_PROTOCOL_H