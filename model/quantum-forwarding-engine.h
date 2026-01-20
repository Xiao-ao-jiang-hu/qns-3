#ifndef QUANTUM_FORWARDING_ENGINE_H
#define QUANTUM_FORWARDING_ENGINE_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/quantum-route-types.h"
#include "ns3/quantum-packet.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumChannel;
class QuantumResourceManager;

/**
 * \brief Abstract base class for quantum packet forwarding.
 * 
 * This class implements the data plane of the quantum network layer,
 * responsible for forwarding quantum packets and managing entanglement.
 */
class QuantumForwardingEngine : public Object
{
public:
  /**
   * \brief Forward a quantum packet.
   * 
   * \param packet The packet to forward
   * \return true if forwarding succeeded, false otherwise
   */
  virtual bool ForwardPacket(Ptr<QuantumPacket> packet) = 0;
  
  /**
   * \brief Forward specific qubits to the next hop.
   * 
   * \param qubits The names of qubits to forward
   * \param nextHop The address of the next hop
   * \return true if forwarding succeeded, false otherwise
   */
  virtual bool ForwardQubits(const std::vector<std::string>& qubits,
                           const std::string& nextHop) = 0;
  
  /**
   * \brief Establish entanglement along a route.
   * 
   * \param route The route to establish entanglement along
   * \return true if entanglement establishment succeeded, false otherwise
   */
  virtual bool EstablishEntanglement(const QuantumRoute& route) = 0;
  
  /**
   * \brief Perform entanglement swap at an intermediate node.
   * 
   * \param qubits The qubits to use for entanglement swap
   * \return true if entanglement swap succeeded, false otherwise
   */
  virtual bool PerformEntanglementSwap(const std::vector<std::string>& qubits) = 0;
  
  /**
   * \brief Distribute an EPR pair over a channel.
   * 
   * \param channel The quantum channel
   * \param epr The names of the two qubits forming the EPR pair
   * \return true if distribution succeeded, false otherwise
   */
  virtual bool DistributeEPR(Ptr<QuantumChannel> channel,
                           const std::pair<std::string, std::string>& epr) = 0;
  
  /**
   * \brief Check if a route is ready for data transmission.
   * 
   * \param route The route to check
   * \return true if the route is ready, false otherwise
   */
  virtual bool IsRouteReady(const QuantumRoute& route) const = 0;
  
  /**
   * \brief Get the estimated delay for establishing a route.
   * 
   * \param route The route to estimate
   * \return Estimated establishment delay
   */
  virtual Time GetEstimatedDelay(const QuantumRoute& route) const = 0;
  
  /**
   * \brief Get the current forwarding strategy.
   * 
   * \return The current forwarding strategy
   */
  virtual QuantumForwardingStrategy GetForwardingStrategy() const = 0;
  
  /**
   * \brief Set the forwarding strategy.
   * 
   * \param strategy The new forwarding strategy
   */
  virtual void SetForwardingStrategy(QuantumForwardingStrategy strategy) = 0;
  
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
   * \brief Get statistics for this forwarding engine.
   * 
   * \return Statistics structure
   */
  virtual QuantumNetworkStats GetStatistics() const = 0;
  
  /**
   * \brief Reset statistics.
   */
  virtual void ResetStatistics() = 0;
  
  /**
   * \brief Get the TypeId.
   * 
   * \return The TypeId
   */
  static TypeId GetTypeId(void);

  /**
   * \brief Get a default forwarding engine.
   * 
   * \return A default forwarding engine instance
   */
  static Ptr<QuantumForwardingEngine> GetDefaultForwardingEngine();
};

} // namespace ns3

#endif /* QUANTUM_FORWARDING_ENGINE_H */