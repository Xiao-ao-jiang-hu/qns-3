#ifndef QCAST_ROUTE_TYPES_H
#define QCAST_ROUTE_TYPES_H

#include "../quantum-route-types.h"
#include "ns3/nstime.h"
#include <map>
#include <vector>
#include <string>
#include <utility>
#include <set>

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

/**
 * \brief Topology Link-State Advertisement (LSA)
 * 
 * Each node generates LSAs containing its neighbor information.
 * LSAs are flooded throughout the network to build global topology.
 */
struct TopologyLSA
{
  std::string nodeId;           ///< Source node ID
  uint32_t sequenceNumber;      ///< Monotonically increasing sequence
  Time timestamp;               ///< Creation time
  std::vector<std::string> neighbors;  ///< Direct neighbor node IDs
  std::vector<double> linkMetrics;     ///< Associated link metrics (fidelity, capacity)
  
  TopologyLSA();
  
  /**
   * \brief Check if this LSA is newer than another LSA
   * \param other The other LSA to compare with
   * \return True if this LSA is newer, false otherwise
   */
  bool IsNewerThan(const TopologyLSA& other) const;
  
  /**
   * \brief Get unique LSA identifier
   * \return LSA ID string (nodeId + sequenceNumber)
   */
  std::string GetLSAId() const;
};

/**
 * \brief Global topology database
 * 
 * Maintains complete network topology information constructed
 * from received LSAs from all nodes.
 */
struct GlobalTopology
{
  std::set<std::string> allNodes;  ///< All node IDs in network
  std::map<std::pair<std::string, std::string>, bool> connections;  ///< Adjacency matrix
  std::map<std::string, TopologyLSA> lsaDatabase;  ///< Latest LSA from each node
  Time lastUpdate;                                 ///< Last update time
  
  GlobalTopology();
  
  /**
   * \brief Update topology from received LSA
   * \param lsa The LSA to incorporate
   */
  void UpdateFromLSA(const TopologyLSA& lsa);
  
  /**
   * \brief Rebuild topology graph from LSA database
   */
  void RebuildTopologyGraph();
  
  /**
   * \brief Check if topology information is complete
   * \return True if we have LSAs from all known nodes, false otherwise
   */
  bool IsComplete() const;
  
  /**
   * \brief Get neighbors of a specific node
   * \param nodeId Node to get neighbors for
   * \return Vector of neighbor node IDs
   */
  std::vector<std::string> GetNeighbors(const std::string& nodeId) const;
  
  /**
   * \brief Get total number of nodes in topology
   * \return Node count
   */
  size_t GetNodeCount() const { return allNodes.size(); }
  
  /**
   * \brief Check if topology is empty
   * \return True if no nodes in topology, false otherwise
   */
  bool IsEmpty() const { return allNodes.empty(); }
};

} // namespace ns3

#endif // QCAST_ROUTE_TYPES_H