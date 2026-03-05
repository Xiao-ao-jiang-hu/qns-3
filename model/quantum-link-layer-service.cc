#include "ns3/quantum-link-layer-service.h"

#include "ns3/log.h"

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

} // namespace ns3