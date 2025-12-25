#ifndef QUANTUM_L3_PROTOCOL_H
#define QUANTUM_L3_PROTOCOL_H

#include "quantum-header.h"
#include "quantum-routing-protocol.h"

#include "ns3/ipv4-address.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/socket.h"

namespace ns3
{

class QuantumNetDevice;
class QuantumPhyEntity;

/**
 * \ingroup quantum
 * \brief The Quantum Network Layer Protocol
 */
class QuantumL3Protocol : public Object
{
  public:
    static TypeId GetTypeId(void);

    QuantumL3Protocol();
    virtual ~QuantumL3Protocol();

    /**
     * \brief Set the routing protocol to be used
     * \param routing The routing protocol
     */
    void SetRoutingProtocol(Ptr<QuantumRoutingProtocol> routing);

    /**
     * \brief Request an end-to-end entanglement
     * \param dest Destination address
     * \param flowId Unique ID for this entanglement flow
     */
    void RequestEntanglement(Ipv4Address dest, uint32_t flowId);

    /**
     * \brief Handle incoming classical control packets
     * \param socket The socket receiving the packet
     */
    void ReceiveClassicalControl(Ptr<Socket> socket);

    /**
     * \brief Send a classical control packet to a neighbor
     * \param nextHop The next hop address
     * \param header The quantum header to include
     */
    void SendControlMessage(Ipv4Address nextHop, QuantumHeader header);

  protected:
    virtual void DoDispose(void);

  private:
    Ptr<Node> m_node;
    Ptr<QuantumRoutingProtocol> m_routingProtocol;
    Ptr<Socket> m_controlSocket;
    uint16_t m_controlPort;

    // Map to track active entanglement contexts
    struct EntanglementContext
    {
        Ipv4Address src;
        Ipv4Address dst;
        uint32_t flowId;
        // Add more state as needed (e.g., current hop, qubits involved)
    };

    std::map<uint32_t, EntanglementContext> m_contexts;
};

} // namespace ns3

#endif /* QUANTUM_L3_PROTOCOL_H */
