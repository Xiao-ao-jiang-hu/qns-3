#ifndef QUANTUM_ROUTE_TYPES_H
#define QUANTUM_ROUTE_TYPES_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "quantum-channel.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumChannel;

/**
 * \brief Forwarding strategy for quantum networks.
 */
enum QuantumForwardingStrategy
{
  QFS_PRE_ESTABLISHED = 0,    /**< Pre-establish entanglement before data transfer */
  QFS_ON_DEMAND,              /**< Establish entanglement on demand */
  QFS_HYBRID                  /**< Hybrid strategy */
};

/**
 * \brief Requirements for quantum route.
 */
struct QuantumRouteRequirements
{
  double minFidelity;          /**< Minimum required fidelity (0.0 to 1.0) */
  Time maxDelay;               /**< Maximum allowable end-to-end delay */
  unsigned numQubits;          /**< Number of qubits to be transmitted */
  Time duration;               /**< Duration for which the route is needed */
  QuantumForwardingStrategy strategy; /**< Forwarding strategy */
  
  QuantumRouteRequirements()
    : minFidelity(0.0),
      maxDelay(Time::Max()),
      numQubits(1),
      duration(Time(0)),
      strategy(QFS_ON_DEMAND)
  {}
  
  QuantumRouteRequirements(double fidelity, Time delay, unsigned qubits, 
                          Time dur, QuantumForwardingStrategy strat)
    : minFidelity(fidelity),
      maxDelay(delay),
      numQubits(qubits),
      duration(dur),
      strategy(strat)
  {}
  
  bool IsValid() const
  {
    return minFidelity >= 0.0 && minFidelity <= 1.0 && numQubits > 0;
  }
};

/**
 * \brief A quantum route representing a path through the network.
 */
struct QuantumRoute
{
  std::string source;                         /**< Source node address */
  std::string destination;                    /**< Destination node address */
  std::vector<Ptr<QuantumChannel>> path;      /**< Sequence of channels forming the path */
  double totalCost;                           /**< Total cost of the route */
  double estimatedFidelity;                   /**< Estimated end-to-end fidelity */
  Time estimatedDelay;                        /**< Estimated end-to-end delay */
  QuantumForwardingStrategy strategy;         /**< Forwarding strategy for this route */
  Time expirationTime;                        /**< Time when the route expires */
  uint32_t routeId;                          /**< Unique identifier for the route */
  
  QuantumRoute()
    : totalCost(0.0),
      estimatedFidelity(1.0),
      estimatedDelay(Time(0)),
      strategy(QFS_ON_DEMAND),
      expirationTime(Time::Max()),
      routeId(0)
  {}
  
  bool IsValid() const
  {
    return !source.empty() && !destination.empty() && !path.empty();
  }
  
  size_t GetHopCount() const
  {
    return path.size();
  }
  
  std::string ToString() const;
};

/**
 * \brief An entry in the routing table.
 */
struct QuantumRouteEntry
{
  std::string destination;                    /**< Destination node address */
  std::string nextHop;                        /**< Next hop node address */
  Ptr<QuantumChannel> channel;                /**< Channel to the next hop */
  double cost;                                /**< Cost to reach destination via this next hop */
  Time timestamp;                             /**< Time when this entry was last updated */
  std::vector<Ptr<QuantumChannel>> fullPath;  /**< Full path to destination (optional) */
  uint32_t sequenceNumber;                    /**< Sequence number for route updates */
  
  QuantumRouteEntry()
    : cost(0.0),
      timestamp(Simulator::Now()),
      sequenceNumber(0)
  {}
  
  QuantumRouteEntry(const std::string& dest, const std::string& next, 
                   Ptr<QuantumChannel> chan, double c, Time ts, uint32_t seq)
    : destination(dest),
      nextHop(next),
      channel(chan),
      cost(c),
      timestamp(ts),
      sequenceNumber(seq)
  {}
  
  bool IsValid() const
  {
    return !destination.empty() && !nextHop.empty() && channel != nullptr;
  }
  
  bool IsExpired(Time expirationTimeout) const
  {
    return (Simulator::Now() - timestamp) > expirationTimeout;
  }
};

/**
 * \brief Statistics for quantum network layer.
 */
struct QuantumNetworkStats
{
  uint32_t packetsSent;                      /**< Total packets sent */
  uint32_t packetsReceived;                  /**< Total packets received */
  uint32_t packetsForwarded;                 /**< Total packets forwarded */
  uint32_t routeRequests;                    /**< Total route requests */
  uint32_t routeReplies;                     /**< Total route replies */
  uint32_t routeErrors;                      /**< Total route errors */
  uint32_t entanglementSwaps;                /**< Total entanglement swaps performed */
  uint32_t eprPairsDistributed;              /**< Total EPR pairs distributed */
  Time totalProcessingDelay;                 /**< Total processing delay */
  Time totalQueuingDelay;                    /**< Total queuing delay */
  
  QuantumNetworkStats()
    : packetsSent(0),
      packetsReceived(0),
      packetsForwarded(0),
      routeRequests(0),
      routeReplies(0),
      routeErrors(0),
      entanglementSwaps(0),
      eprPairsDistributed(0),
      totalProcessingDelay(Time(0)),
      totalQueuingDelay(Time(0))
  {}
  
  void Reset()
  {
    packetsSent = 0;
    packetsReceived = 0;
    packetsForwarded = 0;
    routeRequests = 0;
    routeReplies = 0;
    routeErrors = 0;
    entanglementSwaps = 0;
    eprPairsDistributed = 0;
    totalProcessingDelay = Time(0);
    totalQueuingDelay = Time(0);
  }
};

} // namespace ns3

#endif /* QUANTUM_ROUTE_TYPES_H */