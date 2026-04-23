#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-routing-protocol.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-delay-model.h"
#include "ns3/quantum-link-layer-service.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/simulator.h"

#include <set>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumNetworkLayer");

NS_OBJECT_ENSURE_REGISTERED (QuantumNetworkLayer);

std::map<std::string, QuantumNetworkLayer*> QuantumNetworkLayer::s_instances;
std::map<std::string, std::map<std::string, LinkMetrics>> QuantumNetworkLayer::s_topology;

namespace {

void
ApplyMeasurementCorrections (Ptr<QuantumPhyEntity> qphyent,
                             const std::string &owner,
                             const std::string &qubit,
                             unsigned leftOutcome,
                             unsigned rightOutcome)
{
  if (rightOutcome == 1)
    {
      qphyent->ApplyGate (owner,
                          QNS_GATE_PREFIX + "PX",
                          std::vector<std::complex<double>>{},
                          {qubit});
    }

  if (leftOutcome == 1)
    {
      qphyent->ApplyGate (owner,
                          QNS_GATE_PREFIX + "PZ",
                          std::vector<std::complex<double>>{},
                          {qubit});
    }
}

} // namespace

TypeId
QuantumNetworkLayer::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::QuantumNetworkLayer")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddAttribute ("Owner",
                         "The owner name of this network layer",
                         StringValue (),
                         MakeStringAccessor (&QuantumNetworkLayer::m_owner),
                         MakeStringChecker ())
          .AddAttribute ("MaxRetries",
                         "Maximum number of retries for path setup",
                         UintegerValue (3),
                         MakeUintegerAccessor (&QuantumNetworkLayer::m_maxRetries),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("RetryInterval",
                         "Interval between retries",
                         TimeValue (MilliSeconds (100)),
                         MakeTimeAccessor (&QuantumNetworkLayer::m_retryInterval),
                         MakeTimeChecker ())
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
      m_signalingChannel (nullptr),
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

  for (auto &timer : m_pathTimers)
    {
      timer.second.Cancel ();
    }
  m_pathTimers.clear ();

  if (!m_owner.empty ())
    {
      auto it = s_instances.find (m_owner);
      if (it != s_instances.end () && it->second == this)
        {
          s_instances.erase (it);
        }
      if (s_instances.empty ())
        {
          s_topology.clear ();
        }
    }

  m_pendingSwapSignals.clear ();
  m_entanglementToRequest.clear ();
  m_pathSegments.clear ();
  m_entanglementRequests.clear ();
  m_neighbors.clear ();
  m_paths.clear ();
  m_pathCallbacks.clear ();
  m_routingProtocol = nullptr;
  m_linkLayer = nullptr;
  m_signalingChannel = nullptr;
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

  if (!m_owner.empty ())
    {
      s_instances[m_owner] = this;
    }

  if (m_linkLayer && m_qphyent && m_linkLayer->GetPhyEntity () == nullptr)
    {
      m_linkLayer->SetPhyEntity (m_qphyent);
    }

  if (m_signalingChannel == nullptr)
    {
      m_signalingChannel = CreateObject<QuantumSignalingChannel> ();
      m_signalingChannel->SetPacketLossProbability (0.0);
    }

  if (m_signalingChannel)
    {
      if (m_qphyent)
        {
          m_signalingChannel->SetPhysicalEntity (m_qphyent);
        }
      for (const auto &srcEntry : s_topology)
        {
          for (const auto &dstEntry : srcEntry.second)
            {
              SeedSignalingDelayModel (srcEntry.first,
                                       dstEntry.first,
                                       dstEntry.second.classicalControlDelayMs);
            }
        }
    }

  if (m_routingProtocol == nullptr)
    {
      NS_LOG_WARN ("No routing protocol set for QuantumNetworkLayer; routing will be unavailable");
    }
  else
    {
      m_routingProtocol->SetNetworkLayer (this);
      m_routingProtocol->UpdateTopology (s_topology);
      m_routingProtocol->Initialize ();
    }

  m_initialized = true;
}

void
QuantumNetworkLayer::SetPhyEntity (Ptr<QuantumPhyEntity> qphyent)
{
  NS_LOG_FUNCTION (this << qphyent);
  m_qphyent = qphyent;
  if (m_linkLayer)
    {
      m_linkLayer->SetPhyEntity (qphyent);
    }
  if (m_signalingChannel)
    {
      m_signalingChannel->SetPhysicalEntity (qphyent);
    }
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
      m_routingProtocol->UpdateTopology (s_topology);
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
  if (m_linkLayer && m_qphyent)
    {
      m_linkLayer->SetPhyEntity (m_qphyent);
    }

  if (m_linkLayer)
    {
      for (const auto &neighborEntry : m_neighbors)
        {
          LinkMetrics metrics;
          metrics.fidelity = neighborEntry.second.linkFidelity;
          metrics.initialFidelity = neighborEntry.second.linkFidelity;
          metrics.noiseFamily = neighborEntry.second.noiseFamily;
          metrics.successRate = neighborEntry.second.linkSuccessRate;
          metrics.latency = neighborEntry.second.classicalControlDelayMs;
          metrics.quantumSetupTimeMs = neighborEntry.second.quantumSetupTimeMs;
          metrics.classicalControlDelayMs = neighborEntry.second.classicalControlDelayMs;
          metrics.isAvailable = neighborEntry.second.isAvailable;
          m_linkLayer->AddOrUpdateLink (m_owner, neighborEntry.first, metrics);
        }
    }
}

Ptr<ILinkLayerService>
QuantumNetworkLayer::GetLinkLayer () const
{
  return m_linkLayer;
}

void
QuantumNetworkLayer::SetSignalingChannel (Ptr<QuantumSignalingChannel> signalingChannel)
{
  m_signalingChannel = signalingChannel;
  if (m_signalingChannel && m_qphyent)
    {
      m_signalingChannel->SetPhysicalEntity (m_qphyent);
    }

  if (m_signalingChannel)
    {
      for (const auto &srcEntry : s_topology)
        {
          for (const auto &dstEntry : srcEntry.second)
            {
              SeedSignalingDelayModel (srcEntry.first,
                                       dstEntry.first,
                                       dstEntry.second.classicalControlDelayMs);
            }
        }
    }
}

Ptr<QuantumSignalingChannel>
QuantumNetworkLayer::GetSignalingChannel () const
{
  return m_signalingChannel;
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

  if (!m_owner.empty ())
    {
      auto it = s_instances.find (m_owner);
      if (it != s_instances.end () && it->second == this)
        {
          s_instances.erase (it);
        }
    }

  m_owner = owner;
  if (!m_owner.empty ())
    {
      s_instances[m_owner] = this;
    }

  if (m_routingProtocol)
    {
      m_routingProtocol->SetLocalNode (owner);
    }
}

void
QuantumNetworkLayer::SeedSignalingDelayModel (const std::string &srcNode,
                                              const std::string &dstNode,
                                              double delayMs)
{
  if (m_signalingChannel == nullptr)
    {
      return;
    }

  Ptr<StaticDelayModel> delayModel = CreateObject<StaticDelayModel> ();
  delayModel->SetFixedDelay (MilliSeconds (std::max (0.0, delayMs)));
  m_signalingChannel->SetLinkDelayModel (srcNode, dstNode, delayModel);
}

void
QuantumNetworkLayer::PublishTopology ()
{
  for (const auto &instanceEntry : s_instances)
    {
      QuantumNetworkLayer* instance = instanceEntry.second;
      if (instance && instance->m_routingProtocol)
        {
          instance->m_routingProtocol->UpdateTopology (s_topology);
        }
    }
}

void
QuantumNetworkLayer::AddNeighbor (const std::string &neighbor,
                                  Ptr<QuantumChannel> channel,
                                  double linkFidelity,
                                  double linkSuccessRate,
                                  double quantumSetupTimeMs,
                                  double classicalControlDelayMs,
                                  BellPairNoiseFamily noiseFamily)
{
  NS_LOG_FUNCTION (this << neighbor << channel << linkFidelity << linkSuccessRate
                        << quantumSetupTimeMs << classicalControlDelayMs
                        << static_cast<int> (noiseFamily));

  NeighborInfo info;
  info.neighborName = neighbor;
  info.channel = channel;
  info.linkFidelity = linkFidelity;
  info.linkSuccessRate = linkSuccessRate;
  info.noiseFamily = noiseFamily;
  info.quantumSetupTimeMs = quantumSetupTimeMs;
  info.classicalControlDelayMs = classicalControlDelayMs;
  info.isAvailable = true;
  m_neighbors[neighbor] = info;

  LinkMetrics metrics;
  metrics.fidelity = linkFidelity;
  metrics.initialFidelity = linkFidelity;
  metrics.noiseFamily = noiseFamily;
  metrics.successRate = linkSuccessRate;
  metrics.latency = classicalControlDelayMs;
  metrics.quantumSetupTimeMs = quantumSetupTimeMs;
  metrics.classicalControlDelayMs = classicalControlDelayMs;
  metrics.isAvailable = true;

  s_topology[m_owner][neighbor] = metrics;
  if (m_linkLayer)
    {
      m_linkLayer->AddOrUpdateLink (m_owner, neighbor, metrics);
    }
  SeedSignalingDelayModel (m_owner, neighbor, classicalControlDelayMs);
  PublishTopology ();
}

void
QuantumNetworkLayer::RemoveNeighbor (const std::string &neighbor)
{
  NS_LOG_FUNCTION (this << neighbor);
  m_neighbors.erase (neighbor);
  s_topology[m_owner].erase (neighbor);
  if (m_linkLayer)
    {
      m_linkLayer->RemoveLink (m_owner, neighbor);
    }
  PublishTopology ();
}

void
QuantumNetworkLayer::UpdateNeighborAvailability (const std::string &neighbor, bool available)
{
  NS_LOG_FUNCTION (this << neighbor << available);

  auto it = m_neighbors.find (neighbor);
  if (it == m_neighbors.end ())
    {
      return;
    }

  it->second.isAvailable = available;
  auto topoIt = s_topology.find (m_owner);
  if (topoIt != s_topology.end ())
    {
      topoIt->second[neighbor].isAvailable = available;
      if (m_linkLayer)
        {
          m_linkLayer->AddOrUpdateLink (m_owner, neighbor, topoIt->second[neighbor]);
        }
    }

  PublishTopology ();
}

const std::map<std::string, NeighborInfo>&
QuantumNetworkLayer::GetNeighbors () const
{
  return m_neighbors;
}

PathId
QuantumNetworkLayer::SetupPath (const std::string &srcNode,
                                const std::string &dstNode,
                                double minFidelity,
                                PathReadyCallback callback)
{
  NS_LOG_FUNCTION (this << srcNode << dstNode << minFidelity);

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
  m_paths[pathId] = pathInfo;
  m_pathCallbacks[pathId] = callback;

  if (srcNode == dstNode)
    {
      m_paths[pathId].route = {srcNode};
      m_paths[pathId].actualFidelity = 1.0;
      m_paths[pathId].state = PathState::READY;
      Simulator::ScheduleNow (&QuantumNetworkLayer::NotifyPathResult, this, pathId, true);
      return pathId;
    }

  std::vector<std::string> route;
  if (m_routingProtocol)
    {
      route = m_routingProtocol->CalculateRoute (srcNode, dstNode);
    }

  if (route.size () < 2)
    {
      NS_LOG_WARN ("No route found from " << srcNode << " to " << dstNode);
      m_paths[pathId].state = PathState::FAILED;
      Simulator::ScheduleNow (&QuantumNetworkLayer::NotifyPathResult, this, pathId, false);
      return pathId;
    }

  m_paths[pathId].route = route;
  NS_LOG_INFO ("Path " << pathId << " route: " << m_routingProtocol->RouteToString (route));

  StartPathBuilding (pathId);
  return pathId;
}

void
QuantumNetworkLayer::StartPathBuilding (PathId pathId)
{
  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;
  path.state = PathState::PENDING;

  if (m_linkLayer == nullptr)
    {
      NS_LOG_ERROR ("No link layer service available");
      path.state = PathState::FAILED;
      NotifyPathResult (pathId, false);
      return;
    }

  for (size_t i = 0; i + 1 < path.route.size (); ++i)
    {
      EntanglementRequest req;
      req.id = m_nextEntanglementRequestId++;
      req.pathId = pathId;
      req.hopIndex = i;
      req.srcNode = path.route[i];
      req.dstNode = path.route[i + 1];
      req.minFidelity = path.minFidelity;
      req.state = EntanglementRequestState::PENDING;

      EntanglementCallback entCallback =
          MakeCallback (&QuantumNetworkLayer::HandleLinkLayerEntanglement, this);
      req.entanglementId =
          m_linkLayer->RequestEntanglement (req.srcNode, req.dstNode, req.minFidelity, entCallback);

      m_entanglementToRequest[req.entanglementId] = req.id;
      path.entanglementRequestIds.push_back (req.id);
      m_entanglementRequests[req.id] = req;
    }

  EventId timeoutEvent =
      Simulator::Schedule (m_entanglementTimeout,
                           &QuantumNetworkLayer::HandlePathTimeout,
                           this,
                           pathId,
                           path.retryCount);
  m_pathTimers[pathId] = timeoutEvent;
}

void
QuantumNetworkLayer::HandleLinkLayerEntanglement (EntanglementId id, EntanglementState state)
{
  auto reqIdIt = m_entanglementToRequest.find (id);
  if (reqIdIt == m_entanglementToRequest.end ())
    {
      return;
    }

  auto reqIt = m_entanglementRequests.find (reqIdIt->second);
  if (reqIt == m_entanglementRequests.end ())
    {
      return;
    }

  if (state == EntanglementState::READY)
    {
      reqIt->second.state = EntanglementRequestState::READY;
      CheckPathEntanglements (reqIt->second.pathId);
      return;
    }

  if (state == EntanglementState::FAILED)
    {
      reqIt->second.state = EntanglementRequestState::FAILED;
      HandlePathFailure (reqIt->second.pathId);
    }
}

void
QuantumNetworkLayer::CheckPathEntanglements (PathId pathId)
{
  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  bool allReady = true;
  for (EntanglementRequestId reqId : pathIt->second.entanglementRequestIds)
    {
      auto reqIt = m_entanglementRequests.find (reqId);
      if (reqIt == m_entanglementRequests.end () ||
          reqIt->second.state != EntanglementRequestState::READY)
        {
          allReady = false;
          break;
        }
    }

  if (!allReady)
    {
      return;
    }

  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }

  PerformEntanglementSwapping (pathId);
}

void
QuantumNetworkLayer::PerformEntanglementSwapping (PathId pathId)
{
  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end () || m_linkLayer == nullptr)
    {
      return;
    }

  std::vector<PathSegment> segments;
  for (EntanglementRequestId reqId : pathIt->second.entanglementRequestIds)
    {
      auto reqIt = m_entanglementRequests.find (reqId);
      if (reqIt == m_entanglementRequests.end ())
        {
          HandlePathFailure (pathId);
          return;
        }

      EntanglementInfo info = m_linkLayer->GetEntanglementInfo (reqIt->second.entanglementId);
      if (!info.isValid)
        {
          HandlePathFailure (pathId);
          return;
        }

      PathSegment segment;
      segment.leftNode = reqIt->second.srcNode;
      segment.rightNode = reqIt->second.dstNode;
      segment.leftQubit = info.localQubit;
      segment.rightQubit = info.remoteQubit;
      segment.backingEntanglementId = reqIt->second.entanglementId;
      segments.push_back (segment);

      m_entanglementToRequest.erase (reqIt->second.entanglementId);
      reqIt->second.entanglementId = INVALID_ENTANGLEMENT_ID;
    }

  m_pathSegments[pathId] = segments;
  BeginNextSwap (pathId);
}

void
QuantumNetworkLayer::BeginNextSwap (PathId pathId)
{
  auto segmentsIt = m_pathSegments.find (pathId);
  if (segmentsIt == m_pathSegments.end ())
    {
      return;
    }

  if (segmentsIt->second.size () <= 1)
    {
      FinalizePath (pathId);
      return;
    }

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  size_t nodeIndex = pathIt->second.route.size () - segmentsIt->second.size ();
  Simulator::ScheduleNow (&QuantumNetworkLayer::ExecuteSwapAtNode, this, pathId, nodeIndex);
}

void
QuantumNetworkLayer::ExecuteSwapAtNode (PathId pathId, size_t nodeIndex)
{
  auto pathIt = m_paths.find (pathId);
  auto segmentsIt = m_pathSegments.find (pathId);
  if (pathIt == m_paths.end () || segmentsIt == m_pathSegments.end () ||
      segmentsIt->second.size () < 2 || m_qphyent == nullptr || m_signalingChannel == nullptr)
    {
      HandlePathFailure (pathId);
      return;
    }

  PathSegment leftSegment = segmentsIt->second[0];
  PathSegment rightSegment = segmentsIt->second[1];
  const std::string &relayNode = pathIt->second.route[nodeIndex];

  if (leftSegment.rightNode != relayNode || rightSegment.leftNode != relayNode)
    {
      HandlePathFailure (pathId);
      return;
    }

  m_qphyent->ApplyGate (relayNode,
                        QNS_GATE_PREFIX + "CNOT",
                        std::vector<std::complex<double>>{},
                        {rightSegment.leftQubit, leftSegment.rightQubit});
  m_qphyent->ApplyGate (relayNode,
                        QNS_GATE_PREFIX + "H",
                        std::vector<std::complex<double>>{},
                        {leftSegment.rightQubit});

  unsigned leftOutcome = m_qphyent->Measure (relayNode, {leftSegment.rightQubit}).first;
  unsigned rightOutcome = m_qphyent->Measure (relayNode, {rightSegment.leftQubit}).first;
  m_qphyent->PartialTrace ({leftSegment.rightQubit, rightSegment.leftQubit});

  Ptr<QuantumNode> relay = m_qphyent->GetNode (relayNode);
  if (relay)
    {
      relay->RemoveQubit (leftSegment.rightQubit);
      relay->RemoveQubit (rightSegment.leftQubit);
    }

  std::string payload =
      std::to_string (leftOutcome) + "," + std::to_string (rightOutcome);
  SignalingMessageId signalId =
      m_signalingChannel->SendMessage (relayNode,
                                       rightSegment.rightNode,
                                       SignalingMessageType::MEASUREMENT_RESULT,
                                       payload,
                                       static_cast<uint32_t> (nodeIndex),
                                       static_cast<uint32_t> (nodeIndex),
                                       false);
  m_signalingChannel->RegisterCallback (
      signalId, MakeCallback (&QuantumNetworkLayer::HandleSwapSignal, this));

  PendingSwapSignal signal;
  signal.pathId = pathId;
  signal.leftSegment = leftSegment;
  signal.rightSegment = rightSegment;
  signal.leftOutcome = leftOutcome;
  signal.rightOutcome = rightOutcome;
  m_pendingSwapSignals[signalId] = signal;
}

void
QuantumNetworkLayer::HandleSwapSignal (SignalingMessageId id, SignalingMessageState state)
{
  auto signalIt = m_pendingSwapSignals.find (id);
  if (signalIt == m_pendingSwapSignals.end ())
    {
      return;
    }

  PendingSwapSignal signal = signalIt->second;
  m_pendingSwapSignals.erase (signalIt);

  if (state != SignalingMessageState::DELIVERED)
    {
      HandlePathFailure (signal.pathId);
      return;
    }

  auto pathIt = m_paths.find (signal.pathId);
  auto segmentsIt = m_pathSegments.find (signal.pathId);
  if (pathIt == m_paths.end () || segmentsIt == m_pathSegments.end () || m_qphyent == nullptr)
    {
      return;
    }

  ApplyMeasurementCorrections (m_qphyent,
                               signal.rightSegment.rightNode,
                               signal.rightSegment.rightQubit,
                               signal.leftOutcome,
                               signal.rightOutcome);

  if (segmentsIt->second.size () < 2)
    {
      HandlePathFailure (signal.pathId);
      return;
    }

  PathSegment merged;
  merged.leftNode = signal.leftSegment.leftNode;
  merged.rightNode = signal.rightSegment.rightNode;
  merged.leftQubit = signal.leftSegment.leftQubit;
  merged.rightQubit = signal.rightSegment.rightQubit;
  merged.backingEntanglementId = INVALID_ENTANGLEMENT_ID;

  segmentsIt->second.erase (segmentsIt->second.begin (), segmentsIt->second.begin () + 2);
  segmentsIt->second.insert (segmentsIt->second.begin (), merged);

  BeginNextSwap (signal.pathId);
}

void
QuantumNetworkLayer::FinalizePath (PathId pathId)
{
  auto pathIt = m_paths.find (pathId);
  auto segmentsIt = m_pathSegments.find (pathId);
  if (pathIt == m_paths.end () || segmentsIt == m_pathSegments.end () || segmentsIt->second.empty () ||
      m_qphyent == nullptr)
    {
      HandlePathFailure (pathId);
      return;
    }

  PathSegment &segment = segmentsIt->second.front ();
  double fidelity = 0.0;
  m_qphyent->CalculateFidelity ({segment.leftQubit, segment.rightQubit}, fidelity);
  pathIt->second.actualFidelity = fidelity;

  if (fidelity + 1e-12 < pathIt->second.minFidelity)
    {
      NS_LOG_WARN ("Path " << pathId << " failed final fidelity check: " << fidelity
                           << " < " << pathIt->second.minFidelity);
      HandlePathFailure (pathId);
      return;
    }

  pathIt->second.state = PathState::READY;
  NotifyPathResult (pathId, true);
}

void
QuantumNetworkLayer::CleanupPathResources (PathId pathId)
{
  for (auto it = m_pendingSwapSignals.begin (); it != m_pendingSwapSignals.end ();)
    {
      if (it->second.pathId == pathId)
        {
          it = m_pendingSwapSignals.erase (it);
          continue;
        }
      ++it;
    }

  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  if (m_linkLayer)
    {
      for (EntanglementRequestId reqId : pathIt->second.entanglementRequestIds)
        {
          auto reqIt = m_entanglementRequests.find (reqId);
          if (reqIt == m_entanglementRequests.end ())
            {
              continue;
            }

          if (reqIt->second.entanglementId != INVALID_ENTANGLEMENT_ID)
            {
              m_entanglementToRequest.erase (reqIt->second.entanglementId);
              m_linkLayer->ConsumeEntanglement (reqIt->second.entanglementId);
              reqIt->second.entanglementId = INVALID_ENTANGLEMENT_ID;
            }
        }
    }

  auto segmentsIt = m_pathSegments.find (pathId);
  if (segmentsIt != m_pathSegments.end () && m_qphyent != nullptr)
    {
      std::vector<std::string> validQubits;
      std::set<std::string> seen;

      for (const PathSegment &segment : segmentsIt->second)
        {
          const std::pair<std::string, std::string> ownedQubits[] = {
              {segment.leftNode, segment.leftQubit},
              {segment.rightNode, segment.rightQubit},
          };

          for (const auto &ownedQubit : ownedQubits)
            {
              if (seen.count (ownedQubit.second))
                {
                  continue;
                }
              seen.insert (ownedQubit.second);

              if (m_qphyent->CheckValid ({ownedQubit.second}))
                {
                  validQubits.push_back (ownedQubit.second);
                  Ptr<QuantumNode> node = m_qphyent->GetNode (ownedQubit.first);
                  if (node)
                    {
                      node->RemoveQubit (ownedQubit.second);
                    }
                }
            }
        }

      if (!validQubits.empty ())
        {
          m_qphyent->PartialTrace (validQubits);
        }
      m_pathSegments.erase (segmentsIt);
    }
}

void
QuantumNetworkLayer::HandlePathFailure (PathId pathId)
{
  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  PathInfo &path = pathIt->second;

  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }

  CleanupPathResources (pathId);

  if (path.retryCount < m_maxRetries)
    {
      path.retryCount++;
      path.state = PathState::PENDING;

      for (EntanglementRequestId reqId : path.entanglementRequestIds)
        {
          m_entanglementRequests.erase (reqId);
        }
      path.entanglementRequestIds.clear ();

      Simulator::Schedule (m_retryInterval, &QuantumNetworkLayer::StartPathBuilding, this, pathId);
      return;
    }

  path.state = PathState::FAILED;
  NotifyPathResult (pathId, false);
}

void
QuantumNetworkLayer::HandlePathTimeout (PathId pathId, uint32_t retryCount)
{
  auto pathIt = m_paths.find (pathId);
  if (pathIt == m_paths.end ())
    {
      return;
    }

  if (pathIt->second.retryCount != retryCount || pathIt->second.state == PathState::READY)
    {
      return;
    }

  HandlePathFailure (pathId);
}

void
QuantumNetworkLayer::NotifyPathResult (PathId pathId, bool success)
{
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
  return it != m_paths.end () && it->second.state == PathState::READY;
}

void
QuantumNetworkLayer::ReleasePath (PathId pathId)
{
  auto it = m_paths.find (pathId);
  if (it == m_paths.end ())
    {
      return;
    }

  auto timerIt = m_pathTimers.find (pathId);
  if (timerIt != m_pathTimers.end ())
    {
      timerIt->second.Cancel ();
      m_pathTimers.erase (timerIt);
    }

  CleanupPathResources (pathId);

  for (EntanglementRequestId reqId : it->second.entanglementRequestIds)
    {
      m_entanglementRequests.erase (reqId);
    }

  m_pathCallbacks.erase (pathId);
  m_paths.erase (it);
}

} // namespace ns3
