#ifndef QUANTUM_RESOURCE_MANAGER_H
#define QUANTUM_RESOURCE_MANAGER_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/callback.h"
#include "ns3/quantum-route-types.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumChannel;

/**
 * \brief Abstract base class for quantum resource management.
 * 
 * This class manages quantum resources such as qubit memory and
 * EPR pair capacity. It provides interfaces for reserving and
 * releasing resources, as well as querying resource availability.
 */
class QuantumResourceManager : public Object
{
public:
  /**
   * \brief Reserve qubits on a node.
   * 
   * \param node The node address
   * \param count Number of qubits to reserve
   * \param duration Duration of the reservation
   * \return true if reservation succeeded, false otherwise
   */
  virtual bool ReserveQubits(const std::string& node, unsigned count, 
                           Time duration) = 0;
  
  /**
   * \brief Reserve EPR pairs on a channel.
   * 
   * \param channel The quantum channel
   * \param count Number of EPR pairs to reserve
   * \param duration Duration of the reservation
   * \return true if reservation succeeded, false otherwise
   */
  virtual bool ReserveEPRPairs(Ptr<QuantumChannel> channel, unsigned count,
                             Time duration) = 0;
  
  /**
   * \brief Release reserved qubits.
   * 
   * \param node The node address
   * \param count Number of qubits to release
   */
  virtual void ReleaseQubits(const std::string& node, unsigned count) = 0;
  
  /**
   * \brief Release reserved EPR pairs.
   * 
   * \param channel The quantum channel
   * \param count Number of EPR pairs to release
   */
  virtual void ReleaseEPRPairs(Ptr<QuantumChannel> channel, unsigned count) = 0;
  
  /**
   * \brief Get the number of available qubits on a node.
   * 
   * \param node The node address
   * \return Number of available qubits
   */
  virtual unsigned GetAvailableQubits(const std::string& node) const = 0;
  
  /**
   * \brief Get the available EPR pair capacity on a channel.
   * 
   * \param channel The quantum channel
   * \return Available EPR pair capacity
   */
  virtual unsigned GetAvailableEPRCapacity(Ptr<QuantumChannel> channel) const = 0;
  
  /**
   * \brief Get the memory utilization of a node.
   * 
   * \param node The node address
   * \return Utilization ratio (0.0 to 1.0)
   */
  virtual double GetMemoryUtilization(const std::string& node) const = 0;
  
  /**
   * \brief Get the channel utilization.
   * 
   * \param channel The quantum channel
   * \return Utilization ratio (0.0 to 1.0)
   */
  virtual double GetChannelUtilization(Ptr<QuantumChannel> channel) const = 0;
  
  /**
   * \brief Get the total qubit capacity of a node.
   * 
   * \param node The node address
   * \return Total qubit capacity
   */
  virtual unsigned GetTotalQubitCapacity(const std::string& node) const = 0;
  
  /**
   * \brief Get the total EPR pair capacity of a channel.
   * 
   * \param channel The quantum channel
   * \return Total EPR pair capacity
   */
  virtual unsigned GetTotalEPRCapacity(Ptr<QuantumChannel> channel) const = 0;
  
  /**
   * \brief Set the qubit capacity of a node.
   * 
   * \param node The node address
   * \param capacity The new capacity
   */
  virtual void SetQubitCapacity(const std::string& node, unsigned capacity) = 0;
  
  /**
   * \brief Set the EPR pair capacity of a channel.
   * 
   * \param channel The quantum channel
   * \param capacity The new capacity
   */
  virtual void SetEPRCapacity(Ptr<QuantumChannel> channel, unsigned capacity) = 0;
  
  /**
   * \brief Check if a route has sufficient resources.
   * 
   * \param route The route to check
   * \param requirements The resource requirements
   * \return true if resources are sufficient, false otherwise
   */
  virtual bool CheckRouteResources(const QuantumRoute& route,
                                 const QuantumRouteRequirements& requirements) const = 0;
  
  /**
   * \brief Reserve resources for a route.
   * 
   * \param route The route to reserve resources for
   * \param requirements The resource requirements
   * \return true if reservation succeeded, false otherwise
   */
  virtual bool ReserveRouteResources(const QuantumRoute& route,
                                   const QuantumRouteRequirements& requirements) = 0;
  
  /**
   * \brief Release resources for a route.
   * 
   * \param route The route to release resources for
   */
  virtual void ReleaseRouteResources(const QuantumRoute& route) = 0;
  
  // Callback types
  typedef Callback<void, const std::string&, unsigned> ResourceAvailableCallback;
  typedef Callback<void, const std::string&> ResourceDepletedCallback;
  
  /**
   * \brief Set callback for resource availability notifications.
   * 
   * \param cb The callback function
   */
  virtual void SetResourceAvailableCallback(ResourceAvailableCallback cb) = 0;
  
  /**
   * \brief Set callback for resource depletion notifications.
   * 
   * \param cb The callback function
   */
  virtual void SetResourceDepletedCallback(ResourceDepletedCallback cb) = 0;
  
  /**
   * \brief Get the TypeId.
   * 
   * \return The TypeId
   */
  static TypeId GetTypeId(void);

  /**
   * \brief Get a default resource manager.
   * 
   * \return A default resource manager instance
   */
  static Ptr<QuantumResourceManager> GetDefaultResourceManager();
};

} // namespace ns3

#endif /* QUANTUM_RESOURCE_MANAGER_H */