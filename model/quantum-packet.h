#ifndef QUANTUM_PACKET_H
#define QUANTUM_PACKET_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/address.h"
#include "ns3/quantum-route-types.h"

#include <string>
#include <vector>

namespace ns3 {

// 前向声明
class QuantumRoute;

/**
 * \brief Packet for quantum network communication.
 * 
 * This class represents a quantum network packet that contains both
 * classical information and references to quantum bits.
 * 
 * Quantum packets are used to coordinate the transmission of quantum
 * information through the network. The actual quantum state is managed
 * by the QuantumNetworkSimulator, while this packet contains the
 * metadata needed to route and process the quantum information.
 */
class QuantumPacket : public Object
{
public:
  /**
   * \brief Create a quantum packet.
   * 
   * \param src Source address
   * \param dst Destination address
   * \param qubitRefs List of qubit names referenced by this packet
   * \param classicalPayload Classical payload (can be null)
   * \param route The route to use for this packet (optional)
   */
  QuantumPacket(const std::string& src, const std::string& dst,
                const std::vector<std::string>& qubitRefs = {},
                Ptr<Packet> classicalPayload = nullptr,
                const QuantumRoute& route = QuantumRoute());
  
  virtual ~QuantumPacket();
  
  QuantumPacket();
  static TypeId GetTypeId(void);
  
  // Getters
  std::string GetSourceAddress() const;
  std::string GetDestinationAddress() const;
  std::vector<std::string> GetQubitReferences() const;
  Ptr<Packet> GetClassicalPayload() const;
  uint32_t GetSequenceNumber() const;
  Time GetTimestamp() const;
  Time GetExpirationTime() const;
  uint8_t GetType() const;
  uint8_t GetProtocol() const;
  uint32_t GetSize() const;
  const QuantumRoute& GetRoute() const;
  
  // Setters
  void SetSourceAddress(const std::string& addr);
  void SetDestinationAddress(const std::string& addr);
  void SetQubitReferences(const std::vector<std::string>& qubitRefs);
  void AddQubitReference(const std::string& qubitRef);
  void SetClassicalPayload(Ptr<Packet> payload);
  void SetSequenceNumber(uint32_t seq);
  void SetTimestamp(Time timestamp);
  void SetExpirationTime(Time expiration);
  void SetType(uint8_t type);
  void SetProtocol(uint8_t protocol);
  void SetRoute(const QuantumRoute& route);
  
  // Packet operations
  bool HasExpired() const;
  bool HasQubitReferences() const;
  bool HasClassicalPayload() const;
  void RemoveQubitReference(const std::string& qubitRef);
  void ClearQubitReferences();
  
  // Utility methods
  std::string ToString() const;
  QuantumPacket* Copy() const;
  
  // Packet types
  enum PacketType
  {
    DATA = 0,           /**< Data packet carrying quantum information */
    ROUTE_REQUEST,      /**< Route request packet */
    ROUTE_REPLY,        /**< Route reply packet */
    ROUTE_ERROR,        /**< Route error packet */
    HELLO,              /**< Hello packet for neighbor discovery */
    ACK,                /**< Acknowledgment packet */
    EPR_REQUEST,        /**< Request for EPR pair distribution */
    EPR_RESPONSE,       /**< Response for EPR pair distribution */
    ENTANGLEMENT_SWAP,  /**< Entanglement swap request */
    RESOURCE_REQUEST,   /**< Resource reservation request */
    RESOURCE_RESPONSE,  /**< Resource reservation response */
    CONTROL             /**< General control packet */
  };
  
  // Protocol types
  enum ProtocolType
  {
    PROTO_UNKNOWN = 0,
    PROTO_QUANTUM_ROUTING,
    PROTO_QUANTUM_FORWARDING,
    PROTO_EPR_DISTRIBUTION,
    PROTO_ENTANGLEMENT_SWAP,
    PROTO_RESOURCE_MANAGEMENT
  };

private:
  std::string m_sourceAddress;              /**< Source node address */
  std::string m_destinationAddress;         /**< Destination node address */
  std::vector<std::string> m_qubitRefs;     /**< Names of qubits referenced by this packet */
  Ptr<Packet> m_classicalPayload;           /**< Classical payload packet */
  uint32_t m_sequenceNumber;                /**< Packet sequence number */
  Time m_timestamp;                         /**< Packet creation timestamp */
  Time m_expirationTime;                    /**< Packet expiration time */
  uint8_t m_type;                           /**< Packet type (DATA, ROUTE_REQUEST, etc.) */
  uint8_t m_protocol;                       /**< Protocol type */
  uint32_t m_packetSize;                    /**< Total packet size in bytes */
  QuantumRoute m_route;                     /**< Route information (if known) */
  static uint32_t s_nextSequenceNumber;     /**< Static counter for sequence numbers */
};

} // namespace ns3

#endif /* QUANTUM_PACKET_H */