#ifndef QCAST_FORWARDING_ENGINE_H
#define QCAST_FORWARDING_ENGINE_H

#include "../quantum-forwarding-engine.h"
#include "../quantum-resource-manager.h"
#include "../quantum-network-layer.h"
#include "../quantum-phy-entity.h"
#include "qcast-route-types.h"
#include <map>
#include <vector>
#include <string>

namespace ns3 {

/**
 * \brief Q-CAST forwarding engine implementation
 *
 * Implements Phase P4 (XOR recovery decision and log-time swap scheduling)
 * of the Q-CAST protocol. Provides entanglement establishment and
 * forwarding capabilities with XOR recovery mechanisms.
 */
class QCastForwardingEngine : public QuantumForwardingEngine
{
public:
  QCastForwardingEngine();
  ~QCastForwardingEngine();

  static TypeId GetTypeId(void);

  // QuantumForwardingEngine interface implementation
  bool ForwardPacket(Ptr<QuantumPacket> packet) override;
  bool ForwardQubits(const std::vector<std::string>& qubits,
                    const std::string& nextHop) override;
  bool EstablishEntanglement(const QuantumRoute& route) override;
  bool PerformEntanglementSwap(const std::vector<std::string>& qubits) override;
  bool DistributeEPR(Ptr<QuantumChannel> channel,
                    const std::pair<std::string, std::string>& epr) override;
  bool IsRouteReady(const QuantumRoute& route) const override;
  Time GetEstimatedDelay(const QuantumRoute& route) const override;
  QuantumForwardingStrategy GetForwardingStrategy() const override;
  void SetForwardingStrategy(QuantumForwardingStrategy strategy) override;
  void SetResourceManager(Ptr<QuantumResourceManager> manager) override;
  Ptr<QuantumResourceManager> GetResourceManager() const override;
   QuantumNetworkStats GetStatistics() const override;
   void ResetStatistics() override;
   
   // Network layer registry for packet delivery
   static void RegisterNetworkLayer(const std::string& address, Ptr<QuantumNetworkLayer> layer);
   static void UnregisterNetworkLayer(const std::string& address);
   static Ptr<QuantumNetworkLayer> GetNetworkLayer(const std::string& address);
   
   // Physics layer integration for actual quantum operations
   /**
    * \brief Set the quantum physics entity for actual quantum operations.
    * \param qphyent Pointer to the QuantumPhyEntity.
    */
   void SetQuantumPhyEntity(Ptr<QuantumPhyEntity> qphyent);
   
   /**
    * \brief Get the quantum physics entity.
    * \return Pointer to the QuantumPhyEntity.
    */
   Ptr<QuantumPhyEntity> GetQuantumPhyEntity() const;
   
   /**
    * \brief Calculate the actual fidelity of an established entanglement.
    * 
    * This method calls the physics layer to compute the fidelity from the
    * actual density matrix using tensor network contraction.
    * 
    * \param epr The EPR pair qubits (source qubit, destination qubit).
    * \return The actual fidelity F = <Phi+|rho|Phi+> computed from density matrix.
    */
   double CalculateActualFidelity(const std::pair<std::string, std::string>& epr);
   
   /**
    * \brief Check if physics layer simulation is enabled.
    * \return true if QuantumPhyEntity is set and physics simulation is enabled.
    */
   bool IsPhysicsEnabled() const;
   
   /**
    * \brief Structure to hold actual fidelity statistics from physics simulation.
    */
   struct ActualFidelityStats
   {
     uint32_t routeId;              ///< Route identifier
     double estimatedFidelity;      ///< Fidelity estimated by routing algorithm (G-EDA)
     double actualFidelity;         ///< Actual fidelity computed from density matrix
     uint32_t hopCount;             ///< Number of hops in the route
     Time establishmentTime;        ///< Time when entanglement was established
     Time waitTime;                 ///< Actual waiting time during establishment
   };
   
   /**
    * \brief Get the actual fidelity statistics from physics simulation.
    * \return Vector of ActualFidelityStats for all established routes.
    */
   std::vector<ActualFidelityStats> GetActualFidelityStats() const;
   
   /**
    * \brief Get the average actual fidelity from physics simulation.
    * \return Average actual fidelity, or -1 if no measurements available.
    */
   double GetAverageActualFidelity() const;
   
   /**
    * \brief Clear the actual fidelity statistics.
    */
   void ClearActualFidelityStats();

private:
  // Active route state
  struct RouteState
  {
    QuantumRoute route;
    QuantumRouteRequirements requirements;
    Time establishmentTime;
    bool isEstablished;
    std::vector<std::string> establishedQubits;
  };

  // Private helper methods
  bool XORRecoveryDecision(const QuantumRoute& mainRoute,
                          const std::vector<QuantumRoute>& recoveryPaths,
                          const std::map<uint32_t, std::vector<QuantumRoute>>& recoveryRings);
  void LogTimeSwapScheduling(const QuantumRoute& route);
  
   // Network layer registry for packet delivery
  static std::map<std::string, Ptr<QuantumNetworkLayer>> s_networkLayerRegistry;

  // Classical network delay configuration
  void SetClassicalDelay(Time delay);
  Time GetClassicalDelay() const;
  void SetClassicalDelayPerHop(Time delayPerHop);
  Time GetClassicalDelayPerHop() const;
  void SetClassicalDelayJitter(double jitterRatio);
  double GetClassicalDelayJitter() const;
  
  // Helper method to deliver packet with delay
  void DeliverPacketWithDelay(Ptr<QuantumPacket> packet, 
                              Ptr<QuantumNetworkLayer> dstNetworkLayer,
                              Time delay);
  
  // Get random delay with jitter (simulating background traffic)
  Time GetRandomClassicalDelay() const;

  // Member variables
  Ptr<QuantumResourceManager> m_resourceManager;
  QuantumForwardingStrategy m_strategy;
  QuantumNetworkStats m_stats;

  std::map<uint32_t, RouteState> m_activeRoutes;
  uint32_t m_nextRouteId;
  
  // Classical network delay parameters
  Time m_classicalDelay;        ///< Base classical delay per transmission
  Time m_classicalDelayPerHop;  ///< Additional delay per hop (for multi-hop classical routing)
  double m_classicalDelayJitter; ///< Jitter ratio (0-1) to simulate background traffic variance
  
  // Physics layer for actual quantum operations
  Ptr<QuantumPhyEntity> m_qphyent;  ///< Quantum physics entity for EPR generation, gates, measurement
  
  // Track generated EPR pairs for fidelity calculation
  std::vector<std::pair<std::string, std::string>> m_generatedEprPairs;
  
  // Actual fidelity statistics from physics simulation
  std::vector<ActualFidelityStats> m_actualFidelityStats;
  
  // Track current route's EPR pairs for fidelity calculation
  std::pair<std::string, std::string> m_currentRouteEndpoints;  ///< Source and destination qubits of current route
  std::vector<std::pair<std::string, std::string>> m_currentRouteEprPairs;  ///< All EPR pairs for current route
  
  // Route-specific QuantumPhyEntity for isolated tensor network (avoids interference between routes)
  Ptr<QuantumPhyEntity> m_currentRouteQphyent;  ///< Dedicated physics entity for current route
};

} // namespace ns3

#endif // QCAST_FORWARDING_ENGINE_H