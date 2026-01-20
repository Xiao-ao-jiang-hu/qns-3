#ifndef QCAST_ROUTE_TYPES_H
#define QCAST_ROUTE_TYPES_H

#include "../quantum-route-types.h"
#include "ns3/nstime.h"
#include <map>
#include <vector>
#include <string>
#include <utility>

namespace ns3 {

// Forward declarations
class QuantumChannel;
class QuantumRoute;

/**
 * \brief Link quality information for Q-CAST protocol
 */
struct LinkQuality
{
  double fidelity;      ///< Fidelity of the link
  Time latency;        ///< Latency of the link
  double errorRate;    ///< Error rate of the link
  bool isEstablished;  ///< Whether entanglement is established
  Time timestamp;      ///< Last update time
  
  LinkQuality() 
    : fidelity(1.0),
      latency(Seconds(0.01)),
      errorRate(0.01),
      isEstablished(false),
      timestamp(Seconds(0.0))
  {}
};

/**
 * \brief k-hop link state information (k=3 for Q-CAST)
 * 
 * Each node maintains link state information for all links within k hops.
 */
struct KHopLinkState
{
  std::map<std::string, LinkQuality> linkQuality;  ///< Link ID -> link quality
  Time lastUpdate;                                 ///< Last update time
  
  KHopLinkState();
  
  /**
   * \brief Check if the link state information is expired
   * \param expirationTime Expiration time threshold
   * \return True if expired, false otherwise
   */
  bool IsExpired(Time expirationTime) const;
};

/**
 * \brief Q-CAST route information
 * 
 * Contains complete routing information including main path and recovery paths.
 */
struct QCastRouteInfo
{
  QuantumRoute mainRoute;                          ///< Main path
  std::vector<QuantumRoute> recoveryPaths;         ///< List of recovery paths
  std::map<uint32_t, std::vector<QuantumRoute>> recoveryRings;  ///< Recovery rings
  double successProbability;                       ///< Estimated success probability
  Time creationTime;                               ///< Creation time
  
  QCastRouteInfo();
  
  /**
   * \brief Check if the Q-CAST route information is valid
   * \return True if valid, false otherwise
   */
  bool IsValid() const;
};

/**
 * \brief Residual network graph node information for G-EDA algorithm
 */
struct ResidualNodeInfo
{
  std::string address;                     ///< Node address
  unsigned availableQubits;                ///< Available qubits
  std::vector<std::string> neighborAddresses;  ///< Neighbor addresses
  
  ResidualNodeInfo();
};

/**
 * \brief Residual network graph channel information for G-EDA algorithm
 */
struct ResidualChannelInfo
{
  std::string src;                  ///< Source address
  std::string dst;                  ///< Destination address
  unsigned availableEPRCapacity;    ///< Available EPR pair capacity
  double cost;                      ///< Expected throughput cost
  
  ResidualChannelInfo();
};

/**
 * \brief Residual network graph for G-EDA algorithm
 * 
 * Represents currently available network resources for online path selection.
 */
struct ResidualNetworkGraph
{
  std::map<std::string, ResidualNodeInfo> nodes;  ///< Node information
  std::map<std::pair<std::string, std::string>, ResidualChannelInfo> channels;  ///< Channel information
  
  /**
   * \brief Check if a path is conflict-free (has sufficient resources)
   * \param route The quantum route to check
   * \param requirements Route requirements
   * \return True if conflict-free, false otherwise
   */
  bool IsPathConflictFree(const QuantumRoute& route,
                         const QuantumRouteRequirements& requirements) const;
};

} // namespace ns3

#endif // QCAST_ROUTE_TYPES_H