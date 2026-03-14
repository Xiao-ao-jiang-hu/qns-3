#include "quantum-header.h"

#include "ns3/address-utils.h"

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(QuantumHeader);

QuantumHeader::QuantumHeader()
    : m_source(Ipv4Address::GetAny()),
      m_destination(Ipv4Address::GetAny()),
      m_flowId(0),
      m_type(0),
      m_qubitIndex(0)
{
}

QuantumHeader::~QuantumHeader()
{
}

TypeId
QuantumHeader::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::QuantumHeader")
                            .SetParent<Header>()
                            .SetGroupName("Quantum")
                            .AddConstructor<QuantumHeader>();
    return tid;
}

TypeId
QuantumHeader::GetInstanceTypeId(void) const
{
    return GetTypeId();
}

void
QuantumHeader::Print(std::ostream& os) const
{
    os << "src=" << m_source << " dst=" << m_destination << " flowId=" << m_flowId
       << " type=" << (uint32_t)m_type;
}

uint32_t
QuantumHeader::GetSerializedSize(void) const
{
    return 4 + 4 + 4 + 1 + 4; // src + dst + flowId + type + qubitIndex
}

void
QuantumHeader::Serialize(Buffer::Iterator start) const
{
    WriteTo(start, m_source);
    WriteTo(start, m_destination);
    start.WriteHtonU32(m_flowId);
    start.WriteU8(m_type);
    start.WriteHtonU32(m_qubitIndex);
}

uint32_t
QuantumHeader::Deserialize(Buffer::Iterator start)
{
    ReadFrom(start, m_source);
    ReadFrom(start, m_destination);
    m_flowId = start.ReadNtohU32();
    m_type = start.ReadU8();
    m_qubitIndex = start.ReadNtohU32();
    return GetSerializedSize();
}

void
QuantumHeader::SetSource(Ipv4Address src)
{
    m_source = src;
}

Ipv4Address
QuantumHeader::GetSource(void) const
{
    return m_source;
}

void
QuantumHeader::SetDestination(Ipv4Address dst)
{
    m_destination = dst;
}

Ipv4Address
QuantumHeader::GetDestination(void) const
{
    return m_destination;
}

void
QuantumHeader::SetFlowId(uint32_t flowId)
{
    m_flowId = flowId;
}

uint32_t
QuantumHeader::GetFlowId(void) const
{
    return m_flowId;
}

void
QuantumHeader::SetOperationType(OperationType_t type)
{
    m_type = (uint8_t)type;
}

QuantumHeader::OperationType_t
QuantumHeader::GetOperationType(void) const
{
    return (OperationType_t)m_type;
}

void
QuantumHeader::SetQubitIndex(uint32_t index)
{
    m_qubitIndex = index;
}

uint32_t
QuantumHeader::GetQubitIndex(void) const
{
    return m_qubitIndex;
}

} // namespace ns3
