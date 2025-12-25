#include "quantum-routing-protocol.h"

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(QuantumRoutingProtocol);

TypeId
QuantumRoutingProtocol::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::QuantumRoutingProtocol").SetParent<Object>().SetGroupName("Quantum");
    return tid;
}

} // namespace ns3
