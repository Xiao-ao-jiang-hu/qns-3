#include "quantum-l3-protocol.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/udp-socket-factory.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("QuantumL3Protocol");
NS_OBJECT_ENSURE_REGISTERED(QuantumL3Protocol);

TypeId
QuantumL3Protocol::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::QuantumL3Protocol")
                            .SetParent<Object>()
                            .SetGroupName("Quantum")
                            .AddConstructor<QuantumL3Protocol>();
    return tid;
}

QuantumL3Protocol::QuantumL3Protocol()
    : m_controlPort(9999) // Default port for quantum control signaling
{
}

QuantumL3Protocol::~QuantumL3Protocol()
{
}

void
QuantumL3Protocol::DoDispose(void)
{
    m_node = nullptr;
    m_routingProtocol = nullptr;
    if (m_controlSocket)
    {
        m_controlSocket->Close();
        m_controlSocket = nullptr;
    }
    Object::DoDispose();
}

void
QuantumL3Protocol::SetRoutingProtocol(Ptr<QuantumRoutingProtocol> routing)
{
    m_routingProtocol = routing;
    m_routingProtocol->SetQuantumL3Protocol(this);
}

void
QuantumL3Protocol::RequestEntanglement(Ipv4Address dest, uint32_t flowId)
{
    NS_LOG_FUNCTION(this << dest << flowId);

    Ipv4Address nextHop = m_routingProtocol->RouteOutput(dest);

    if (nextHop != Ipv4Address::GetAny())
    {
        QuantumHeader header;
        header.SetOperationType(QuantumHeader::EPR_GEN_REQUEST);
        header.SetSource(m_node->GetObject<Ipv4L3Protocol>()->GetAddress(1, 0).GetLocal());
        header.SetDestination(dest);
        header.SetFlowId(flowId);

        SendControlMessage(nextHop, header);
    }
}

void
QuantumL3Protocol::SendControlMessage(Ipv4Address nextHop, QuantumHeader header)
{
    NS_LOG_FUNCTION(this << nextHop);

    if (!m_controlSocket)
    {
        m_controlSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
        m_controlSocket->Bind();
    }

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(header);

    m_controlSocket->Connect(InetSocketAddress(nextHop, m_controlPort));
    m_controlSocket->Send(packet);
}

void
QuantumL3Protocol::ReceiveClassicalControl(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        QuantumHeader header;
        packet->RemoveHeader(header);

        NS_LOG_INFO("Received Quantum Control Message: " << header.GetOperationType());

        // Logic to handle different operation types
        switch (header.GetOperationType())
        {
        case QuantumHeader::EPR_GEN_REQUEST: {
            // If this is the destination, send success back
            // Otherwise, route to next hop
            if (header.GetDestination() ==
                m_node->GetObject<Ipv4L3Protocol>()->GetAddress(1, 0).GetLocal())
            {
                // Handle at destination
            }
            else
            {
                Ipv4Address nextHop = m_routingProtocol->RouteInput(header);
                SendControlMessage(nextHop, header);
            }
            break;
        }
        default:
            break;
        }
    }
}

} // namespace ns3
