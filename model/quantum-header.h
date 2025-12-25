#ifndef QUANTUM_HEADER_H
#define QUANTUM_HEADER_H

#include "ns3/address.h"
#include "ns3/header.h"
#include "ns3/ipv4-address.h"

namespace ns3
{

/**
 * \ingroup quantum
 * \brief Header for Quantum Network Layer Control Messages
 */
class QuantumHeader : public Header
{
  public:
    enum OperationType_t
    {
        EPR_GEN_REQUEST = 1,
        EPR_GEN_SUCCESS = 2,
        EPR_GEN_FAILURE = 3,
        ENT_SWAP_REQUEST = 4,
        ENT_SWAP_SUCCESS = 5,
        PURIFY_REQUEST = 6,
        MEASURE_NOTIFICATION = 7
    };

    QuantumHeader();
    virtual ~QuantumHeader();

    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual void Print(std::ostream& os) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(Buffer::Iterator start) const;
    virtual uint32_t Deserialize(Buffer::Iterator start);

    // Setters and Getters
    void SetSource(Ipv4Address src);
    Ipv4Address GetSource(void) const;

    void SetDestination(Ipv4Address dst);
    Ipv4Address GetDestination(void) const;

    void SetFlowId(uint32_t flowId);
    uint32_t GetFlowId(void) const;

    void SetOperationType(OperationType_t type);
    OperationType_t GetOperationType(void) const;

    void SetQubitIndex(uint32_t index);
    uint32_t GetQubitIndex(void) const;

  private:
    Ipv4Address m_source;
    Ipv4Address m_destination;
    uint32_t m_flowId;
    uint8_t m_type;
    uint32_t m_qubitIndex;
};

} // namespace ns3

#endif /* QUANTUM_HEADER_H */
