#include "ns3/quantum-basis.h"
#include "ns3/quantum-packet.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumPacket");

NS_OBJECT_ENSURE_REGISTERED (QuantumPacket);

// 静态成员初始化
uint32_t QuantumPacket::s_nextSequenceNumber = 1;

QuantumPacket::QuantumPacket(const std::string& src, const std::string& dst,
                           const std::vector<std::string>& qubitRefs,
                           Ptr<Packet> classicalPayload,
                           const QuantumRoute& route)
    : m_sourceAddress(src),
      m_destinationAddress(dst),
      m_qubitRefs(qubitRefs),
      m_classicalPayload(classicalPayload),
      m_sequenceNumber(s_nextSequenceNumber++),
      m_timestamp(Simulator::Now()),
      m_expirationTime(m_timestamp + Seconds(10.0)), // 默认10秒过期
      m_type(DATA),
      m_protocol(PROTO_UNKNOWN),
      m_packetSize(0),
      m_route(route)
{
  NS_LOG_LOGIC("Creating QuantumPacket from " << src << " to " << dst);
}

QuantumPacket::~QuantumPacket()
{
  NS_LOG_LOGIC("Destroying QuantumPacket seq=" << m_sequenceNumber);
}

QuantumPacket::QuantumPacket()
    : m_sourceAddress(""),
      m_destinationAddress(""),
      m_classicalPayload(nullptr),
      m_sequenceNumber(s_nextSequenceNumber++),
      m_timestamp(Simulator::Now()),
      m_expirationTime(m_timestamp + Seconds(10.0)),
      m_type(DATA),
      m_protocol(PROTO_UNKNOWN),
      m_packetSize(0)
{
}

TypeId
QuantumPacket::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::QuantumPacket")
    .SetParent<Object>()
    .AddConstructor<QuantumPacket>()
    .AddAttribute("SourceAddress", "Source address of the packet",
                  StringValue(""),
                  MakeStringAccessor(&QuantumPacket::m_sourceAddress),
                  MakeStringChecker())
    .AddAttribute("DestinationAddress", "Destination address of the packet",
                  StringValue(""),
                  MakeStringAccessor(&QuantumPacket::m_destinationAddress),
                  MakeStringChecker())
    .AddAttribute("SequenceNumber", "Packet sequence number",
                  UintegerValue(0),
                  MakeUintegerAccessor(&QuantumPacket::m_sequenceNumber),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("Type", "Packet type",
                  UintegerValue(DATA),
                  MakeUintegerAccessor(&QuantumPacket::m_type),
                  MakeUintegerChecker<uint8_t>())
    .AddAttribute("Protocol", "Protocol type",
                  UintegerValue(PROTO_UNKNOWN),
                  MakeUintegerAccessor(&QuantumPacket::m_protocol),
                  MakeUintegerChecker<uint8_t>());
  return tid;
}

// Getter方法实现
std::string QuantumPacket::GetSourceAddress() const
{
  return m_sourceAddress;
}

std::string QuantumPacket::GetDestinationAddress() const
{
  return m_destinationAddress;
}

std::vector<std::string> QuantumPacket::GetQubitReferences() const
{
  return m_qubitRefs;
}

Ptr<Packet> QuantumPacket::GetClassicalPayload() const
{
  return m_classicalPayload;
}

uint32_t QuantumPacket::GetSequenceNumber() const
{
  return m_sequenceNumber;
}

Time QuantumPacket::GetTimestamp() const
{
  return m_timestamp;
}

Time QuantumPacket::GetExpirationTime() const
{
  return m_expirationTime;
}

uint8_t QuantumPacket::GetType() const
{
  return m_type;
}

uint8_t QuantumPacket::GetProtocol() const
{
  return m_protocol;
}

uint32_t QuantumPacket::GetSize() const
{
  return m_packetSize;
}

const QuantumRoute& QuantumPacket::GetRoute() const
{
  return m_route;
}

// Setter方法实现
void QuantumPacket::SetSourceAddress(const std::string& addr)
{
  m_sourceAddress = addr;
}

void QuantumPacket::SetDestinationAddress(const std::string& addr)
{
  m_destinationAddress = addr;
}

void QuantumPacket::SetQubitReferences(const std::vector<std::string>& qubitRefs)
{
  m_qubitRefs = qubitRefs;
}

void QuantumPacket::AddQubitReference(const std::string& qubitRef)
{
  m_qubitRefs.push_back(qubitRef);
}

void QuantumPacket::SetClassicalPayload(Ptr<Packet> payload)
{
  m_classicalPayload = payload;
}

void QuantumPacket::SetSequenceNumber(uint32_t seq)
{
  m_sequenceNumber = seq;
}

void QuantumPacket::SetTimestamp(Time timestamp)
{
  m_timestamp = timestamp;
}

void QuantumPacket::SetExpirationTime(Time expiration)
{
  m_expirationTime = expiration;
}

void QuantumPacket::SetType(uint8_t type)
{
  m_type = type;
}

void QuantumPacket::SetProtocol(uint8_t protocol)
{
  m_protocol = protocol;
}

void QuantumPacket::SetRoute(const QuantumRoute& route)
{
  m_route = route;
}

// Packet操作
bool QuantumPacket::HasExpired() const
{
  return Simulator::Now() > m_expirationTime;
}

bool QuantumPacket::HasQubitReferences() const
{
  return !m_qubitRefs.empty();
}

bool QuantumPacket::HasClassicalPayload() const
{
  return m_classicalPayload != nullptr;
}

void QuantumPacket::RemoveQubitReference(const std::string& qubitRef)
{
  auto it = std::find(m_qubitRefs.begin(), m_qubitRefs.end(), qubitRef);
  if (it != m_qubitRefs.end())
  {
    m_qubitRefs.erase(it);
  }
}

void QuantumPacket::ClearQubitReferences()
{
  m_qubitRefs.clear();
}

// 工具方法
std::string QuantumPacket::ToString() const
{
  std::stringstream ss;
  ss << "QuantumPacket[src=" << m_sourceAddress
     << ", dst=" << m_destinationAddress
     << ", seq=" << m_sequenceNumber
     << ", type=" << (int)m_type
     << ", proto=" << (int)m_protocol
     << ", qubits=" << m_qubitRefs.size()
     << ", timestamp=" << m_timestamp.GetSeconds()
     << ", expired=" << (HasExpired() ? "yes" : "no")
     << "]";
  return ss.str();
}

QuantumPacket* QuantumPacket::Copy() const
{
  // 创建新包，复制所有字段
  QuantumPacket* copy = new QuantumPacket(m_sourceAddress, m_destinationAddress,
                                        m_qubitRefs, m_classicalPayload, m_route);
  copy->m_sequenceNumber = m_sequenceNumber;
  copy->m_timestamp = m_timestamp;
  copy->m_expirationTime = m_expirationTime;
  copy->m_type = m_type;
  copy->m_protocol = m_protocol;
  copy->m_packetSize = m_packetSize;
  return copy;
}

} // namespace ns3