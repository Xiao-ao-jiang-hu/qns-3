#include "ns3/quantum-link-layer-service.h"

#include "ns3/quantum-basis.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-routing-metric.h"
#include "ns3/quantum-node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumLinkLayerService");

NS_OBJECT_ENSURE_REGISTERED (ILinkLayerService);

TypeId
ILinkLayerService::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::ILinkLayerService")
                          .SetParent<Object> ()
                          .SetGroupName ("Quantum");
  return tid;
}

ILinkLayerService::ILinkLayerService ()
{
}

ILinkLayerService::~ILinkLayerService ()
{
}

NS_OBJECT_ENSURE_REGISTERED (IEntanglementManager);

TypeId
IEntanglementManager::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::IEntanglementManager")
                          .SetParent<Object> ()
                          .SetGroupName ("Quantum");
  return tid;
}

IEntanglementManager::IEntanglementManager ()
{
}

IEntanglementManager::~IEntanglementManager ()
{
}

NS_OBJECT_ENSURE_REGISTERED (SimpleEntanglementManager);

EntanglementId SimpleEntanglementManager::s_nextId = 1;

TypeId
SimpleEntanglementManager::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::SimpleEntanglementManager")
                          .SetParent<IEntanglementManager> ()
                          .SetGroupName ("Quantum")
                          .AddConstructor<SimpleEntanglementManager> ();
  return tid;
}

SimpleEntanglementManager::SimpleEntanglementManager ()
{
}

SimpleEntanglementManager::~SimpleEntanglementManager ()
{
}

void
SimpleEntanglementManager::DoDispose ()
{
  m_infos.clear ();
  m_states.clear ();
  m_callbacks.clear ();
  IEntanglementManager::DoDispose ();
}

void
SimpleEntanglementManager::SetOwner (const std::string &owner)
{
  m_owner = owner;
}

std::string
SimpleEntanglementManager::GetOwner () const
{
  return m_owner;
}

EntanglementId
SimpleEntanglementManager::CreateEntanglementRequest (const std::string &neighbor,
                                                      double)
{
  EntanglementId id = s_nextId++;
  EntanglementInfo info;
  info.id = id;
  info.localNode = m_owner;
  info.remoteNode = neighbor;
  info.createdAt = Simulator::Now ();
  info.isValid = false;
  m_infos[id] = info;
  m_states[id] = EntanglementState::PENDING;
  return id;
}

void
SimpleEntanglementManager::NotifyEntanglementReady (EntanglementId id,
                                                    const std::string &localQubit,
                                                    const std::string &remoteQubit,
                                                    double fidelity)
{
  auto infoIt = m_infos.find (id);
  if (infoIt == m_infos.end ())
    {
      return;
    }

  infoIt->second.localQubit = localQubit;
  infoIt->second.remoteQubit = remoteQubit;
  infoIt->second.fidelity = fidelity;
  infoIt->second.createdAt = Simulator::Now ();
  infoIt->second.isValid = true;
  m_states[id] = EntanglementState::READY;

  auto cbIt = m_callbacks.find (id);
  if (cbIt != m_callbacks.end ())
    {
      cbIt->second (id, EntanglementState::READY);
    }
}

void
SimpleEntanglementManager::NotifyEntanglementFailed (EntanglementId id)
{
  m_states[id] = EntanglementState::FAILED;
  auto infoIt = m_infos.find (id);
  if (infoIt != m_infos.end ())
    {
      infoIt->second.isValid = false;
    }

  auto cbIt = m_callbacks.find (id);
  if (cbIt != m_callbacks.end ())
    {
      cbIt->second (id, EntanglementState::FAILED);
    }
}

EntanglementState
SimpleEntanglementManager::GetState (EntanglementId id) const
{
  auto it = m_states.find (id);
  if (it == m_states.end ())
    {
      return EntanglementState::FAILED;
    }
  return it->second;
}

EntanglementInfo
SimpleEntanglementManager::GetInfo (EntanglementId id) const
{
  auto it = m_infos.find (id);
  if (it == m_infos.end ())
    {
      return EntanglementInfo{};
    }
  return it->second;
}

bool
SimpleEntanglementManager::Consume (EntanglementId id)
{
  auto stateIt = m_states.find (id);
  if (stateIt == m_states.end ())
    {
      return false;
    }
  stateIt->second = EntanglementState::CONSUMED;
  auto infoIt = m_infos.find (id);
  if (infoIt != m_infos.end ())
    {
      infoIt->second.isValid = false;
    }
  return true;
}

void
SimpleEntanglementManager::RegisterCallback (EntanglementId id, EntanglementCallback callback)
{
  m_callbacks[id] = callback;
}

NS_OBJECT_ENSURE_REGISTERED (QuantumLinkLayerService);

TypeId
QuantumLinkLayerService::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::QuantumLinkLayerService")
                          .SetParent<ILinkLayerService> ()
                          .SetGroupName ("Quantum")
                          .AddConstructor<QuantumLinkLayerService> ();
  return tid;
}

QuantumLinkLayerService::QuantumLinkLayerService ()
    : m_qphyent (nullptr)
{
  m_rng = CreateObject<UniformRandomVariable> ();
}

QuantumLinkLayerService::~QuantumLinkLayerService ()
{
}

void
QuantumLinkLayerService::DoDispose ()
{
  for (auto &entry : m_pendingEvents)
    {
      entry.second.Cancel ();
    }
  m_pendingEvents.clear ();
  m_pendingRequests.clear ();
  m_links.clear ();
  m_managers.clear ();
  m_entanglementOwners.clear ();
  m_qphyent = nullptr;
  m_rng = nullptr;
  ILinkLayerService::DoDispose ();
}

void
QuantumLinkLayerService::SetOwner (const std::string &owner)
{
  m_owner = owner;
}

std::string
QuantumLinkLayerService::GetOwner () const
{
  return m_owner;
}

Ptr<SimpleEntanglementManager>
QuantumLinkLayerService::GetOrCreateManager (const std::string &owner)
{
  auto it = m_managers.find (owner);
  if (it != m_managers.end ())
    {
      return it->second;
    }

  Ptr<SimpleEntanglementManager> manager = CreateObject<SimpleEntanglementManager> ();
  manager->SetOwner (owner);
  m_managers[owner] = manager;
  return manager;
}

void
QuantumLinkLayerService::AddOrUpdateLink (const std::string &srcNode,
                                          const std::string &dstNode,
                                          const LinkMetrics &metrics)
{
  m_links[std::make_pair (srcNode, dstNode)] = metrics;
}

void
QuantumLinkLayerService::RemoveLink (const std::string &srcNode,
                                     const std::string &dstNode)
{
  m_links.erase (std::make_pair (srcNode, dstNode));
}

LinkMetrics
QuantumLinkLayerService::ResolveMetrics (const std::string &srcNode,
                                         const std::string &dstNode) const
{
  auto directIt = m_links.find (std::make_pair (srcNode, dstNode));
  if (directIt != m_links.end ())
    {
      return directIt->second;
    }

  auto reverseIt = m_links.find (std::make_pair (dstNode, srcNode));
  if (reverseIt != m_links.end ())
    {
      return reverseIt->second;
    }

  return LinkMetrics{};
}

EntanglementId
QuantumLinkLayerService::RequestEntanglement (const std::string &srcNode,
                                              const std::string &dstNode,
                                              double minFidelity,
                                              EntanglementCallback callback)
{
  Ptr<SimpleEntanglementManager> manager = GetOrCreateManager (srcNode);
  EntanglementId id = manager->CreateEntanglementRequest (dstNode, minFidelity);
  manager->RegisterCallback (id, callback);
  m_entanglementOwners[id] = srcNode;

  PendingRequest request;
  request.srcNode = srcNode;
  request.dstNode = dstNode;
  request.minFidelity = minFidelity;
  m_pendingRequests[id] = request;

  LinkMetrics metrics = ResolveMetrics (srcNode, dstNode);
  double readyDelayMs = metrics.quantumSetupTimeMs + metrics.classicalControlDelayMs;
  if (metrics.quantumSetupTimeMs <= 0.0 && metrics.classicalControlDelayMs <= 0.0)
    {
      readyDelayMs = metrics.latency;
    }

  if (!metrics.isAvailable)
    {
      Simulator::ScheduleNow (&QuantumLinkLayerService::CompleteRequest, this, id);
      return id;
    }

  if (readyDelayMs < 0.0)
    {
      readyDelayMs = 0.0;
    }

  m_pendingEvents[id] =
      Simulator::Schedule (MilliSeconds (readyDelayMs),
                           &QuantumLinkLayerService::CompleteRequest,
                           this,
                           id);
  return id;
}

void
QuantumLinkLayerService::CompleteRequest (EntanglementId id)
{
  auto requestIt = m_pendingRequests.find (id);
  if (requestIt == m_pendingRequests.end ())
    {
      return;
    }

  LinkMetrics metrics = ResolveMetrics (requestIt->second.srcNode, requestIt->second.dstNode);
  Ptr<SimpleEntanglementManager> manager = GetOrCreateManager (requestIt->second.srcNode);

  double successRate = metrics.successRate;
  if (successRate <= 0.0)
    {
      successRate = 1.0;
    }

  if (!metrics.isAvailable || m_qphyent == nullptr || m_rng->GetValue () > successRate)
    {
      manager->NotifyEntanglementFailed (id);
      m_pendingRequests.erase (requestIt);
      m_pendingEvents.erase (id);
      return;
    }

  double fidelity = metrics.initialFidelity > 0.0 ? metrics.initialFidelity : metrics.fidelity;
  if (fidelity <= 0.0)
    {
      fidelity = 1e-12;
    }
  std::string localQubit = "ent_" + std::to_string (id) + "_" + requestIt->second.srcNode + "_l";
  std::string remoteQubit = "ent_" + std::to_string (id) + "_" + requestIt->second.dstNode + "_r";

  bool generated =
      m_qphyent->GenerateQubitsMixed (requestIt->second.srcNode,
                                     GetEPRwithNoiseFamily (metrics.noiseFamily, fidelity),
                                     {localQubit, remoteQubit});
  if (!generated ||
      !m_qphyent->TransferQubit (requestIt->second.srcNode,
                                 requestIt->second.dstNode,
                                 remoteQubit))
    {
      manager->NotifyEntanglementFailed (id);
      m_pendingRequests.erase (requestIt);
      m_pendingEvents.erase (id);
      return;
    }

  manager->NotifyEntanglementReady (id, localQubit, remoteQubit, fidelity);
  m_pendingRequests.erase (requestIt);
  m_pendingEvents.erase (id);
}

EntanglementInfo
QuantumLinkLayerService::GetEntanglementInfo (EntanglementId id) const
{
  auto ownerIt = m_entanglementOwners.find (id);
  if (ownerIt == m_entanglementOwners.end ())
    {
      return EntanglementInfo{};
    }

  auto managerIt = m_managers.find (ownerIt->second);
  if (managerIt == m_managers.end ())
    {
      return EntanglementInfo{};
    }

  return managerIt->second->GetInfo (id);
}

bool
QuantumLinkLayerService::ConsumeEntanglement (EntanglementId id)
{
  EntanglementInfo info = GetEntanglementInfo (id);
  auto ownerIt = m_entanglementOwners.find (id);
  if (ownerIt == m_entanglementOwners.end ())
    {
      return false;
    }

  auto managerIt = m_managers.find (ownerIt->second);
  if (managerIt == m_managers.end ())
    {
      return false;
    }

  if (info.isValid && m_qphyent != nullptr)
    {
      std::vector<std::string> validQubits;
      if (m_qphyent->CheckValid ({info.localQubit}))
        {
          validQubits.push_back (info.localQubit);
          m_qphyent->GetNode (info.localNode)->RemoveQubit (info.localQubit);
        }
      if (m_qphyent->CheckValid ({info.remoteQubit}))
        {
          validQubits.push_back (info.remoteQubit);
          m_qphyent->GetNode (info.remoteNode)->RemoveQubit (info.remoteQubit);
        }
      if (!validQubits.empty ())
        {
          m_qphyent->PartialTrace (validQubits);
        }
    }

  return managerIt->second->Consume (id);
}

bool
QuantumLinkLayerService::IsEntanglementReady (EntanglementId id) const
{
  auto ownerIt = m_entanglementOwners.find (id);
  if (ownerIt == m_entanglementOwners.end ())
    {
      return false;
    }

  auto managerIt = m_managers.find (ownerIt->second);
  if (managerIt == m_managers.end ())
    {
      return false;
    }

  return managerIt->second->GetState (id) == EntanglementState::READY;
}

void
QuantumLinkLayerService::SetPhyEntity (Ptr<QuantumPhyEntity> qphyent)
{
  m_qphyent = qphyent;
}

Ptr<QuantumPhyEntity>
QuantumLinkLayerService::GetPhyEntity () const
{
  return m_qphyent;
}

} // namespace ns3
