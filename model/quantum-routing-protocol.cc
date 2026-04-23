#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-routing-metric.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"

#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumRoutingProtocol");

NS_OBJECT_ENSURE_REGISTERED (QuantumRoutingProtocol);

TypeId
QuantumRoutingProtocol::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::QuantumRoutingProtocol")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddAttribute ("MetricModel",
                         "Path-metric model used to evaluate candidate routes.",
                         PointerValue (),
                         MakePointerAccessor (&QuantumRoutingProtocol::m_metricModel),
                         MakePointerChecker<QuantumRoutingMetric> ());
  return tid;
}

QuantumRoutingProtocol::QuantumRoutingProtocol ()
    : m_localNode (""),
      m_networkLayer (nullptr),
      m_metricModel (nullptr)
{
  NS_LOG_FUNCTION (this);
}

QuantumRoutingProtocol::~QuantumRoutingProtocol ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumRoutingProtocol::SetLocalNode (const std::string &nodeName)
{
  NS_LOG_FUNCTION (this << nodeName);
  m_localNode = nodeName;
}

std::string
QuantumRoutingProtocol::GetLocalNode () const
{
  return m_localNode;
}

void
QuantumRoutingProtocol::Initialize (void)
{
  // Default: nothing to do
}

void
QuantumRoutingProtocol::SetNetworkLayer (QuantumNetworkLayer* netLayer)
{
  NS_LOG_FUNCTION (this);
  m_networkLayer = netLayer;
  // Derive local node name from network layer
  if (m_networkLayer && m_localNode.empty ())
    {
      m_localNode = m_networkLayer->GetOwner ();
    }
}

void
QuantumRoutingProtocol::NotifyTopologyChange (void)
{
  // Default: nothing to do
}

void
QuantumRoutingProtocol::SetMetricModel (Ptr<QuantumRoutingMetric> metricModel)
{
  m_metricModel = metricModel;
}

Ptr<QuantumRoutingMetric>
QuantumRoutingProtocol::GetMetricModel () const
{
  return m_metricModel;
}

std::string
QuantumRoutingProtocol::RouteToString (const std::vector<std::string> &route)
{
  std::ostringstream oss;
  for (size_t i = 0; i < route.size (); ++i)
    {
      oss << route[i];
      if (i + 1 < route.size ())
        oss << " -> ";
    }
  return oss.str ();
}

} // namespace ns3
