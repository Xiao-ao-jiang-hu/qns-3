#ifndef QCAST_FORWARDING_ENGINE_H
#define QCAST_FORWARDING_ENGINE_H

#include "../quantum-forwarding-engine.h"
#include "../quantum-resource-manager.h"
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

  // Member variables
  Ptr<QuantumResourceManager> m_resourceManager;
  QuantumForwardingStrategy m_strategy;
  QuantumNetworkStats m_stats;

  std::map<uint32_t, RouteState> m_activeRoutes;
  uint32_t m_nextRouteId;
};

} // namespace ns3

#endif // QCAST_FORWARDING_ENGINE_H