#ifndef QUANTUM_ROUTING_PROTOCOL_H
#define QUANTUM_ROUTING_PROTOCOL_H

#include "quantum-header.h"

#include "ns3/ipv4-address.h"
#include "ns3/object.h"
#include "ns3/packet.h"

namespace ns3
{

class QuantumL3Protocol;

/**
 * \ingroup quantum
 * \brief Abstract base class for Quantum Routing Protocols
 */
class QuantumRoutingProtocol : public Object
{
  public:
    static TypeId GetTypeId(void);

    /**
     * \brief Query route for an outgoing entanglement request
     * \param dest Destination address
     * \return The next hop address
     */
    virtual Ipv4Address RouteOutput(Ipv4Address dest) = 0;

    /**
     * \brief Handle an incoming entanglement request at an intermediate node
     * \param header The quantum header of the request
     * \return The next hop address
     */
    virtual Ipv4Address RouteInput(const QuantumHeader& header) = 0;

    /**
     * \brief Notify the routing protocol about a link failure or entanglement failure
     * \param neighbor The neighbor node where failure occurred
     */
    virtual void NotifyEntanglementFailure(Ipv4Address neighbor) = 0;

    /**
     * \brief Set the L3 protocol this routing protocol is associated with
     * \param l3 The L3 protocol
     */
    virtual void SetQuantumL3Protocol(Ptr<QuantumL3Protocol> l3) = 0;
};

} // namespace ns3

#endif /* QUANTUM_ROUTING_PROTOCOL_H */
