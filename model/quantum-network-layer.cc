#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-routing-protocol.h"

#include "ns3/quantum-basis.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-link-layer-service.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumNetworkLayer");

NS_OBJECT_ENSURE_REGISTERED (QuantumNetworkLayer);

TypeId
QuantumNetworkLayer::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::QuantumNetworkLayer")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddAttribute ("Owner", "The owner name of this network layer",
                         StringValue (), MakeStringAccessor (&QuantumNetworkLayer::m_owner),
                         MakeStringChecker ())
          .AddAttribute ("MaxRetries", "Maximum number of retries for path setup",
                         UintegerValue (3), MakeUintegerAccessor (&QuantumNetworkLayer::m_maxRetries),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("RetryInterval", "Interval between retries",
                         TimeValue (MilliSeconds (100)),
                         MakeTimeAccessor (&QuantumNetworkLayer::m_retryInterval), MakeTimeChecker ())
          .AddAttribute ("EntanglementTimeout",
                         "Timeout for waiting entanglement from link layer",
                         TimeValue (Seconds (1.0)),
                         MakeTimeAccessor (&QuantumNetworkLayer::m_entanglementTimeout),
                         MakeTimeChecker ());
  return tid;
}

QuantumNetworkLayer::QuantumNetworkLayer ()
    : m_qphyent (nullptr),
      m_routingProtocol (nullptr),
      m_linkLayer (nullptr),
      m_nextPathId (1),
      m_nextEntanglementRequestId (1),
      m_maxRetries (3),
      m_retryInterval (MilliSeconds (100)),
      m_entanglementTimeout (Seconds (1.0)),
      m_initialized (false)
{
  NS_LOG_FUNCTION (this);
}

QuantumNetworkLayer::~QuantumNetworkLayer ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumNetworkLayer::DoDispose ()
{
  NS_LOG_FUNCTION (this);

  // Cancel all pending timers
  for (auto &timer : m_pathTimers)
    {
      timer.second.Cancel ();
    }
  m_pathTimers.clear ();

  m_routingProtocol = nullptr;
  m_linkLayer = nullptr;
  m_qphyent = nullptr;

  Object::DoDispose ();
}

void
QuantumNetworkLayer::Initialize ()
{
  NS_LOG_FUNCTION (this);

  if (m_initialized)
    {
      return;
    }

  // If no routing protocol is set, log a warning
  if (m_routingProtocol == nullptr)
    {
      NS_LOG_WARN ("No routing protocol set for QuantumNetworkLayer; routing will be unavailable");
    }

  // Initialize routing protocol
  if (m_routingProtocol)
    {
      m_routingProtocol->Initialize ();
    }

  m_initialized = true;
  NS_LOG_INFO ("QuantumNetworkLayer initialized");
}

void
QuantumNetworkLayer::SetPhyEntity (Ptr<QuantumPhyEntity> qphyent)
{
  NS_LOG_FUNCTION (this << qphyent);
  m_qphyent = qphyent;
}

Ptr<QuantumPhyEntity>
QuantumNetworkLayer::GetPhyEntity () const
{
  return m_qphyent;
}

void
QuantumNetworkLayer::SetRoutingProtocol (Ptr<QuantumRoutingProtocol> routingProtocol)
{
  NS_LOG_FUNCTION (this << routingProtocol);
  m_routingProtocol = routingProtocol;
  if (m_routingProtocol)
    {
      m_routingProtocol->SetNetworkLayer (this);
    }
}

Ptr<QuantumRoutingProtocol>
QuantumNetworkLayer::GetRoutingProtocol () const
{
  return m_routingProtocol;
}

void
QuantumNetworkLayer::SetLinkLayer (Ptr<ILinkLayerService> linkLayer)
{
  NS_LOG_FUNCTION (this << linkLayer);
  m_linkLayer = linkLayer;
}

Ptr<ILinkLayerService>
QuantumNetworkLayer::GetLinkLayer () const
{
  return m_linkLayer;
}

std::string
QuantumNetworkLayer::GetOwner () const
{
  return m_owner;
}

void
QuantumNetworkLayer::SetOwner (const std::string &owner)
{
  NS_LOG_FUNCTION (this << owner);
  m_owner = owner;
}

void
QuantumNetworkLayer::AddNeighbor (const std::string &neighbor, Ptr<QuantumChannel> channel,
                                  double linkFidelity, double linkSuccessRate)
{
  NS_LOG_FUNCTION (this << neighbor << channel << linkFidelity << linkSuccessRate);

  NeighborInfo info;
  info.neighborName = neighbor;
  info.channel = channel;
  info.linkFidelity = linkFidelity;
  info.linkSuccessRate = linkSuccessRate;
  info.isAvailable = true;

  m_neighbors[neighbor] = info;

  // Notify routing protocol of topology change
  if (m_routingProtocol)
    {
      m_routingProtocol->NotifyTopologyChange ();
    }
}

void
QuantumNetworkLayer::RemoveNeighbor (const std::string &neighbor)
{
  NS_LOG_FUNCTION (this << neighbor);

  auto it = m_neighbors.find (neighbor);
  if (it != m_neighbors.end ())
    {
      m_neighbors.erase (it);

      // Notify routing protocol of topology change
      if (m_routingProtocol)
        {
          m_routingProtocol->NotifyTopologyChange ();
        }
    }
}

void
QuantumNetworkLayer::UpdateNeighborAvailability (const std::string &neighbor, bool available)
{
  NS_LOG_FUNCTION (this << neighbor << available);

  auto it = m_neighbors.find (neighbor);
  if (it != m_neighbors.end ())
    {
      it->second.isAvailable = available;

      // Notify routing protocol of topology change
      if (m_routingProtocol)
        {
          m_routingProtocol->NotifyTopologyChange ();
        }
    }
}

const std::map<std::string, NeighborInfo> &
QuantumNetworkLayer::GetNeighbors () const
{
  return m_neighbors;
}

PathId
QuantumNetworkLayer::SetupPath (const std::string &srcNode, const std::string &dstNode,
                                double minFidelity, PathReadyCallback callback)
{
  NS_LOG_FUNCTION (this << srcNode << dstNode << minFidelity);

  // Ensure initialized
  if (!m_initialized)
    {
      Initialize ();
    }

  PathId pathId = m_nextPathId++;

  PathInfo pathInfo;
  pathInfo.id = pathId;
  pathInfo.srcNode = srcNode;
  pathInfo.dstNode = dstNode;
  pathInfo.minFidelity = minFidelity;
  pathInfo.state = PathState::PENDING;
  pathInfo.retryCount = 0;

  m_paths[pathId] = pathInfo;
  m_pathCallbacks[pathId] = callback;

  // Use routing protocol to calculate route
  std::vector<std::string> route;
  if (m_routingProtocol)
    {
      route = m_routingProtocol->CalculateRoute (srcNode, dstNode);
    }

  if (route.empty ())
    {
      NS_LOG_WARN ("No route found from " << srcNode << " to " << dstNode);
      pathInfo.state = PathState::FAILED;
      m_paths[pathId] = pathInfo;
      Simulator::ScheduleNow (&QuantumNetworkLayer::NotifyPathResult, this, pathId, false);
      return pathId;
    }

  pathInfo.route = route;
  m_paths[pathId] = pathInfo;

  NS_LOG_INFO ("Path " << pathId << " route: " << m_routingProtocol->RouteToString (route));

  // Start building entanglement chain
  StartPathBuilding (pathId);

  return pathId;
}

void
QuantumNetworkLayer::StartPathBuilding (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      NS_LOG_ERROR ("Path " << pathId << " not found");
      return;
    }

  PathInfo &path = pathIt->second;

  // Build entanglement for each hop
  for (size_t i = 0; i < path.route.size () - 1; ++i)
    {
      std::string localNode = path.route[i];
      std::string nextNode = path.route[i + 1];

      // Create entanglement request for this hop
      EntanglementRequest req;
      req.pathId = pathId;
      req.hopIndex = i;
      req.srcNode = localNode;
      req.dstNode = nextNode;
      req.minFidelity = path.minFidelity;
      req.state = EntanglementRequestState::PENDING;

      EntanglementRequestId reqId = m_nextEntanglementRequestId++;
      req.id = reqId;
      m_entanglementRequests[reqId] = req;

      // Add to path's entanglement requests
      path.entanglementRequestIds.push_back (reqId);

      // Request entanglement from link layer
      if (m_linkLayer)
        {
          NS_LOG_INFO ("Requesting entanglement for hop " << i << ": " << localNode << " -> "
                                                          << nextNode);

          EntanglementCallback entCallback =
              MakeCallback (&QuantumNetworkLayer::HandleLinkLayerEntanglement, this);

          // Store the callback association
          auto &request = m_entanglementRequests[reqId];
          request.entanglementId = m_nextEntanglementRequestId++;

          m_linkLayer->RequestEntanglement (localNode, nextNode, req.minFidelity, entCallback);
        }
      else
        {
          NS_LOG_ERROR ("No link layer service available");
          path.state = PathState::FAILED;
          NotifyPathResult (pathId, false);
          return;
        }
    }

  m_paths[pathId] = path;

  // Set timeout timer
  EventId timeoutEvent =
      Simulator::Schedule (m_entanglementTimeout, &QuantumNetworkLayer::HandlePathTimeout, this,
                           pathId, path.retryCount);
  m_pathTimers[pathId] = timeoutEvent;
}

void
QuantumNetworkLayer::HandleLinkLayerEntanglement (EntanglementId id, EntanglementState state)
{
  NS_LOG_FUNCTION (this << id << static_cast<int> (state));

  if (state == EntanglementState::READY)
    {
      NS_LOG_INFO ("Link layer entanglement " << id << " is ready");

      // Find which path this entanglement belongs to
      for (auto &reqEntry : m_entanglementRequests)
        {
          if (reqEntry.second.entanglementId == id)
            {
              reqEntry.second.state = EntanglementRequestState::READY;
              CheckPathEntanglements (reqEntry.second.pathId);
              return;
            }
        }
    }
  else if (state == EntanglementState::FAILED)
    {
      NS_LOG_WARN ("Link layer entanglement " << id << " failed");

      for (auto &reqEntry : m_entanglementRequests)
        {
          if (reqEntry.second.entanglementId == id)
            {
              reqEntry.second.state = EntanglementRequestState::FAILED;
              HandlePathFailure (reqEntry.second.pathId);
              return;
            }
        }
    }
}

void
QuantumNetworkLayer::CheckPathEntanglements (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;

  // Check if all entanglements are ready
  bool allReady = true;
  for (EntanglementRequestId reqId : path.entanglementRequestIds)
    {
      auto reqIt = m_entanglementRequests.find (reqId);
      if (reqIt == m_entanglementRequests.end () ||
          reqIt->second.state != EntanglementRequestState::READY)
        {
          allReady = false;
          break;
        }
    }

  if (allReady)
    {
      NS_LOG_INFO ("All entanglements ready for path " << pathId);

      // Cancel timeout timer
      auto timerIt = m_pathTimers.find (pathId);
      if (timerIt != m_pathTimers.end ())
        {
          timerIt->second.Cancel ();
          m_pathTimers.erase (timerIt);
        }

      // Perform entanglement swapping
      PerformEntanglementSwapping (pathId);
    }
}

void
QuantumNetworkLayer::PerformEntanglementSwapping (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      NS_LOG_ERROR ("Path " << pathId << " not found");
      return;
    }

  PathInfo &path = pathIt->second;

  if (path.route.size () <= 2)
    {
      // Direct connection, no swapping needed
      path.state = PathState::READY;
      NotifyPathResult (pathId, true);
      return;
    }

  // Schedule swaps sequentially
  Time swapTime = Simulator::Now ();
  for (size_t i = 1; i < path.route.size () - 1; ++i)
    {
      swapTime += MilliSeconds (10);
      Simulator::Schedule (swapTime - Simulator::Now (),
                           &QuantumNetworkLayer::ExecuteSwapAtNode, this, pathId, i);
    }

  // After all swaps are done, notify success
  Simulator::Schedule (swapTime - Simulator::Now () + MilliSeconds (1),
                       &QuantumNetworkLayer::FinalizePath, this, pathId);
}

void
QuantumNetworkLayer::ExecuteSwapAtNode (PathId pathId, size_t nodeIndex)
{
  NS_LOG_FUNCTION (this << pathId << nodeIndex);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;

  if (nodeIndex >= path.route.size ())
    {
      NS_LOG_ERROR ("Invalid node index " << nodeIndex);
      return;
    }

  std::string nodeName = path.route[nodeIndex];
  NS_LOG_INFO ("Executing entanglement swap at node " << nodeName);
}

void
QuantumNetworkLayer::FinalizePath (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;
  path.state = PathState::READY;

  NS_LOG_INFO ("Path " << pathId << " is ready for use");
  NotifyPathResult (pathId, true);
}

void
QuantumNetworkLayer::HandlePathFailure (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;

  // Cancel timeout timer
  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }

  if (path.retryCount < m_maxRetries)
    {
      path.retryCount++;
      NS_LOG_INFO ("Retrying path " << pathId << " (attempt " << path.retryCount << "/"
                                    << m_maxRetries << ")");

      // Clear previous entanglement requests
      for (EntanglementRequestId reqId : path.entanglementRequestIds)
        {
          m_entanglementRequests.erase (reqId);
        }
      path.entanglementRequestIds.clear ();

      // Retry after interval
      Simulator::Schedule (m_retryInterval, &QuantumNetworkLayer::StartPathBuilding, this, pathId);
    }
  else
    {
      NS_LOG_WARN ("Path " << pathId << " failed after " << m_maxRetries << " retries");
      path.state = PathState::FAILED;
      NotifyPathResult (pathId, false);
    }
}

void
QuantumNetworkLayer::HandlePathTimeout (PathId pathId, uint32_t retryCount)
{
  NS_LOG_FUNCTION (this << pathId << retryCount);

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;

  // Only handle timeout if the retry count matches
  if (path.retryCount != retryCount)
    {
      return;
    }

  if (path.state == PathState::READY)
    {
      return;
    }

  NS_LOG_WARN ("Path " << pathId << " timed out");
  HandlePathFailure (pathId);
}

void
QuantumNetworkLayer::NotifyPathResult (PathId pathId, bool success)
{
  NS_LOG_FUNCTION (this << pathId << success);

  auto callbackIt = m_pathCallbacks.find (pathId);
  if (callbackIt != m_pathCallbacks.end ())
    {
      callbackIt->second (pathId, success);
    }

  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }
}

PathInfo
QuantumNetworkLayer::GetPathInfo (PathId pathId) const
{
  auto it = m_paths.find (pathId);
  if (it != m_paths.end ())
    {
      return it->second;
    }
  return PathInfo{};
}

bool
QuantumNetworkLayer::IsPathReady (PathId pathId) const
{
  auto it = m_paths.find (pathId);
  if (it != m_paths.end ())
    {
      return it->second.state == PathState::READY;
    }
  return false;
}

void
QuantumNetworkLayer::ReleasePath (PathId pathId)
{
  NS_LOG_FUNCTION (this << pathId);

  auto it = m_paths.find (pathId);
  if (it == m_paths.end ())
    {
      return;
    }

  PathInfo &path = it->second;

  // Cancel any pending timers
  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }

  // Release entanglements
  for (EntanglementRequestId reqId : path.entanglementRequestIds)
    {
      auto reqIt = m_entanglementRequests.find (reqId);
      if (reqIt != m_entanglementRequests.end ())
        {
          if (m_linkLayer && reqIt->second.entanglementId != INVALID_ENTANGLEMENT_ID)
            {
              m_linkLayer->ConsumeEntanglement (reqIt->second.entanglementId);
            }
        }
      m_entanglementRequests.erase (reqId);
    }

  m_pathCallbacks.erase (pathId);
  m_paths.erase (it);

  NS_LOG_INFO ("Path " << pathId << " released");
}

} // namespace ns3
