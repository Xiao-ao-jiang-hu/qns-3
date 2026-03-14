#ifndef QUANTUM_NETWORK_LAYER_H
#define QUANTUM_NETWORK_LAYER_H

#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/event-id.h"

#include "ns3/quantum-link-layer-service.h"
#include "ns3/quantum-channel.h"

#include <vector>
#include <string>
#include <map>
#include <set>
#include <functional>

namespace ns3 {

class QuantumChannel;
class QuantumPhyEntity;
class QuantumRoutingProtocol;

/**
 * \brief Path identifier type
 */
typedef uint32_t PathId;
typedef uint32_t EntanglementRequestId;

// INVALID_PATH_ID is defined in quantum-link-layer-service.h
static const EntanglementRequestId INVALID_ENTANGLEMENT_REQUEST_ID = 0;

/**
 * \brief Path state enumeration
 */
enum class PathState
{
    PENDING,    // Path is being established
    READY,      // Path is ready for use
    FAILED,     // Path setup failed
    CONSUMED    // Path has been consumed/released
};

/**
 * \brief Request state for entanglement establishment
 */
enum class EntanglementRequestState
{
    PENDING,    // Request sent, waiting for response
    READY,      // Entanglement established
    FAILED,     // Entanglement establishment failed
    CONSUMED    // Entanglement has been consumed
};

/**
 * \brief Path information structure
 */
struct PathInfo
{
    PathId id;
    std::string srcNode;
    std::string dstNode;
    std::vector<std::string> route;
    double minFidelity;
    PathState state;
    uint32_t retryCount;
    std::vector<EntanglementRequestId> entanglementRequestIds;

    PathInfo ()
        : id (INVALID_PATH_ID),
          srcNode (""),
          dstNode (""),
          minFidelity (0.0),
          state (PathState::PENDING),
          retryCount (0)
    {
    }
};

/**
 * \brief Neighbor information structure
 */
struct NeighborInfo
{
    std::string neighborName;
    Ptr<QuantumChannel> channel;
    double linkFidelity;
    double linkSuccessRate;
    bool isAvailable;

    NeighborInfo ()
        : neighborName (""),
          channel (nullptr),
          linkFidelity (0.0),
          linkSuccessRate (0.0),
          isAvailable (false)
    {
    }
};

/**
 * \brief Entanglement request structure for tracking link-layer requests
 */
struct EntanglementRequest
{
    EntanglementRequestId id;
    PathId pathId;
    size_t hopIndex;
    std::string srcNode;
    std::string dstNode;
    double minFidelity;
    EntanglementRequestState state;
    EntanglementId entanglementId;

    EntanglementRequest ()
        : id (INVALID_ENTANGLEMENT_REQUEST_ID),
          pathId (INVALID_PATH_ID),
          hopIndex (0),
          minFidelity (0.0),
          state (EntanglementRequestState::PENDING),
          entanglementId (INVALID_ENTANGLEMENT_ID)
    {
    }
};

/**
 * \brief Callback for path ready notification
 * \param pathId The path identifier
 * \param success True if path setup succeeded, false otherwise
 */
typedef Callback<void, PathId, bool> PathReadyCallback;

/**
 * \brief Quantum Network Layer
 * 
 * This class implements the network layer for quantum repeater networks.
 * It is responsible for:
 * - End-to-end path establishment between quantum nodes
 * - Coordination of multi-hop entanglement distribution
 * - Management of entanglement swapping across intermediate nodes
 * - Integration with routing protocols (via QuantumRoutingProtocol interface)
 * 
 * The network layer does NOT implement routing algorithms directly.
 * Instead, it delegates routing decisions to a pluggable routing protocol.
 * This allows different routing strategies (Dijkstra, link-state, distance-vector, etc.)
 * to be used interchangeably.
 * 
 * Architecture:
 * ```
 * Applications
 *      ↓
 * QuantumNetworkLayer (this class)
 *      ↓ (delegates routing)
 * QuantumRoutingProtocol (interface)
 *      ↓
 * ILinkLayerService (interface)
 *      ↓
 * Physical Layer
 * ```
 */
class QuantumNetworkLayer : public Object
{
public:
    static TypeId GetTypeId ();

    QuantumNetworkLayer ();
    ~QuantumNetworkLayer () override;

    void DoDispose () override;

    /**
     * \brief Set the physical entity
     * \param qphyent Pointer to the quantum physical entity
     */
    void SetPhyEntity (Ptr<QuantumPhyEntity> qphyent);

    /**
     * \brief Get the physical entity
     * \return Pointer to the quantum physical entity
     */
    Ptr<QuantumPhyEntity> GetPhyEntity () const;

    /**
     * \brief Set the routing protocol
     * 
     * The routing protocol is responsible for computing routes.
     * The network layer delegates all routing decisions to this protocol.
     * 
     * \param routingProtocol Pointer to the routing protocol
     */
    void SetRoutingProtocol (Ptr<QuantumRoutingProtocol> routingProtocol);

    /**
     * \brief Get the routing protocol
     * \return Pointer to the routing protocol
     */
    Ptr<QuantumRoutingProtocol> GetRoutingProtocol () const;

    /**
     * \brief Set the link layer service
     * 
     * The link layer service provides neighbor-to-neighbor entanglement.
     * The network layer uses this to establish entanglement for each hop.
     * 
     * \param linkLayer Pointer to the link layer service
     */
    void SetLinkLayer (Ptr<ILinkLayerService> linkLayer);

    /**
     * \brief Get the link layer service
     * \return Pointer to the link layer service
     */
    Ptr<ILinkLayerService> GetLinkLayer () const;

    /**
     * \brief Get the owner name
     * \return Name of the owner node
     */
    std::string GetOwner () const;

    /**
     * \brief Set the owner name
     * \param owner Name of the owner node
     */
    void SetOwner (const std::string &owner);

    /**
     * \brief Add a neighbor to the network topology
     * 
     * This updates both the local neighbor table and notifies the
     * routing protocol of the new neighbor.
     * 
     * \param neighbor Name of the neighbor node
     * \param channel Quantum channel to the neighbor
     * \param linkFidelity Fidelity of the quantum link
     * \param linkSuccessRate Success probability of the link
     */
    void AddNeighbor (const std::string &neighbor, Ptr<QuantumChannel> channel,
                      double linkFidelity, double linkSuccessRate);

    /**
     * \brief Remove a neighbor from the network topology
     * \param neighbor Name of the neighbor node to remove
     */
    void RemoveNeighbor (const std::string &neighbor);

    /**
     * \brief Update neighbor availability
     * \param neighbor Name of the neighbor
     * \param available Whether the neighbor is currently available
     */
    void UpdateNeighborAvailability (const std::string &neighbor, bool available);

    /**
     * \brief Get the neighbor table
     * \return Const reference to the map of neighbors and their info
     */
    const std::map<std::string, NeighborInfo>& GetNeighbors (void) const;

    /**
     * \brief Request establishment of an end-to-end path
     * 
     * This is the main API for applications to request quantum connectivity.
     * The network layer will:
     * 1. Use the routing protocol to compute a route
     * 2. Request entanglement for each hop via the link layer
     * 3. Coordinate entanglement swapping at intermediate nodes
     * 4. Notify the application when the path is ready
     * 
     * \param srcNode Source node name
     * \param dstNode Destination node name
     * \param minFidelity Minimum required end-to-end fidelity
     * \param callback Callback to invoke when path is ready or fails
     * \return Path identifier (can be used to query status or release the path)
     */
    PathId SetupPath (const std::string &srcNode, const std::string &dstNode,
                      double minFidelity, PathReadyCallback callback);

    /**
     * \brief Get path information
     * \param pathId Path identifier
     * \return Path information structure
     */
    PathInfo GetPathInfo (PathId pathId) const;

    /**
     * \brief Check if a path is ready for use
     * \param pathId Path identifier
     * \return True if path is in READY state
     */
    bool IsPathReady (PathId pathId) const;

    /**
     * \brief Release a path and free resources
     * 
     * This should be called when the application is done using the path.
     * It releases entanglements and cleans up internal state.
     * 
     * \param pathId Path identifier
     */
    void ReleasePath (PathId pathId);

    /**
     * \brief Initialize the network layer
     * 
     * Should be called after setting up routing protocol and other dependencies.
     */
    void Initialize ();

protected:
    /**
     * \brief Start building a path
     * \param pathId Path identifier
     */
    void StartPathBuilding (PathId pathId);

    /**
     * \brief Handle entanglement ready notification from link layer
     * \param id Entanglement identifier
     * \param state State of the entanglement
     */
    void HandleLinkLayerEntanglement (EntanglementId id, EntanglementState state);

    /**
     * \brief Check if all entanglements for a path are ready
     * \param pathId Path identifier
     */
    void CheckPathEntanglements (PathId pathId);

    /**
     * \brief Perform entanglement swapping across the path
     * \param pathId Path identifier
     */
    void PerformEntanglementSwapping (PathId pathId);

    /**
     * \brief Execute swap at a specific intermediate node
     * \param pathId Path identifier
     * \param nodeIndex Index of the node in the route
     */
    void ExecuteSwapAtNode (PathId pathId, size_t nodeIndex);

    /**
     * \brief Finalize path establishment after all swaps complete
     * \param pathId Path identifier
     */
    void FinalizePath (PathId pathId);

    /**
     * \brief Handle path setup failure
     * \param pathId Path identifier
     */
    void HandlePathFailure (PathId pathId);

    /**
     * \brief Handle path timeout
     * \param pathId Path identifier
     * \param retryCount Retry count at timeout
     */
    void HandlePathTimeout (PathId pathId, uint32_t retryCount);

    /**
     * \brief Notify application of path result
     * \param pathId Path identifier
     * \param success True if path setup succeeded
     */
    void NotifyPathResult (PathId pathId, bool success);

    // Member variables
    Ptr<QuantumPhyEntity> m_qphyent;                    // Physical entity
    Ptr<QuantumRoutingProtocol> m_routingProtocol;      // Routing protocol
    Ptr<ILinkLayerService> m_linkLayer;                 // Link layer service
    std::string m_owner;                                // Owner node name

    // Path management
    std::map<PathId, PathInfo> m_paths;                 // Active paths
    std::map<PathId, PathReadyCallback> m_pathCallbacks; // Path callbacks
    std::map<PathId, EventId> m_pathTimers;             // Path timeout timers

    // Entanglement request tracking
    std::map<EntanglementRequestId, EntanglementRequest> m_entanglementRequests;

    // Neighbor information
    std::map<std::string, NeighborInfo> m_neighbors;

    // ID generation
    PathId m_nextPathId;
    EntanglementRequestId m_nextEntanglementRequestId;

    // Configuration
    uint32_t m_maxRetries;
    Time m_retryInterval;
    Time m_entanglementTimeout;

    // State
    bool m_initialized;
};

} // namespace ns3

#endif /* QUANTUM_NETWORK_LAYER_H */
