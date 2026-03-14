#ifndef QUANTUM_SIGNALING_CHANNEL_H
#define QUANTUM_SIGNALING_CHANNEL_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

#include <vector>
#include <string>
#include <map>
#include <functional>

namespace ns3 {

class QuantumDelayModel;
class QuantumPhyEntity;

typedef uint32_t SignalingMessageId;
static const SignalingMessageId INVALID_SIGNALING_MESSAGE_ID = 0;

enum class SignalingMessageType
{
    ENTANGLEMENT_REQUEST,
    ENTANGLEMENT_RESPONSE,
    SWAP_REQUEST,
    SWAP_RESPONSE,
    MEASUREMENT_RESULT,
    CORRECTION_COMMAND
};

enum class SignalingMessageState
{
    PENDING,
    DELIVERED,
    LOST,
    TIMEOUT
};

struct SignalingMessage
{
    SignalingMessageId id;
    SignalingMessageType type;
    std::string srcNode;
    std::string dstNode;
    std::string payload;
    Time sendTime;
    Time deliveryTime;
    SignalingMessageState state;
    bool requiresAck;
    uint32_t hopIndex;
    uint32_t roundIndex;
};

typedef Callback<void, SignalingMessageId, SignalingMessageState> SignalingCallback;

struct SwapScheduleEntry
{
    uint32_t round;
    uint32_t nodeIndex;
    std::string nodeName;
    Time scheduledTime;
    std::vector<std::string> qubitsToSwap;
};

class QuantumSignalingChannel : public Object
{
public:
    static TypeId GetTypeId (void);

    QuantumSignalingChannel ();
    ~QuantumSignalingChannel () override;

    void DoDispose (void) override;

    void SetDelayModel (Ptr<QuantumDelayModel> delayModel);
    Ptr<QuantumDelayModel> GetDelayModel (void) const;

    void SetPacketLossProbability (double prob);
    double GetPacketLossProbability (void) const;

    void SetTimeoutDuration (Time timeout);
    Time GetTimeoutDuration (void) const;

    void SetPhysicalEntity (Ptr<QuantumPhyEntity> qphyent);
    Ptr<QuantumPhyEntity> GetPhysicalEntity (void) const;

    SignalingMessageId SendMessage (
        const std::string &srcNode,
        const std::string &dstNode,
        SignalingMessageType type,
        const std::string &payload,
        uint32_t hopIndex = 0,
        uint32_t roundIndex = 0,
        bool requiresAck = true
    );

    void RegisterCallback (SignalingMessageId id, SignalingCallback callback);

    std::vector<SwapScheduleEntry> GenerateLogarithmicSwapSchedule (
        const std::vector<std::string> &pathNodes,
        Time baseSwapDuration = MilliSeconds (10)
    );

    uint32_t GetNumSwapRounds (uint32_t numHops) const;

    Time CalculateTotalSignalingDelay (
        const std::vector<std::string> &pathNodes,
        bool includeAck = true
    );

    Time GetLinkDelay (const std::string &srcNode, const std::string &dstNode);

    void SetLinkDelayModel (const std::string &srcNode, const std::string &dstNode,
                            Ptr<QuantumDelayModel> delayModel);

    std::vector<SignalingMessage> GetMessageHistory (void) const;

    void ClearHistory (void);

    void PrintStatistics (void) const;

    struct Statistics
    {
        uint32_t messagesSent;
        uint32_t messagesDelivered;
        uint32_t messagesLost;
        uint32_t messagesTimedOut;
        Time totalDelay;
        Time maxDelay;
        Time minDelay;
        double averageDelay;
    };

    Statistics GetStatistics (void) const;

private:
    void DoSendMessage (SignalingMessageId id);
    void HandleTimeout (SignalingMessageId id);
    void DeliverMessage (SignalingMessageId id);
    void DropMessage (SignalingMessageId id, SignalingMessageState reason);

    Ptr<QuantumDelayModel> m_delayModel;
    Ptr<QuantumDelayModel> m_defaultDelayModel;
    Ptr<QuantumPhyEntity> m_qphyent;
    Ptr<UniformRandomVariable> m_rng;

    double m_packetLossProbability;
    Time m_timeoutDuration;

    std::map<SignalingMessageId, SignalingMessage> m_messages;
    std::map<SignalingMessageId, SignalingCallback> m_callbacks;
    std::map<SignalingMessageId, EventId> m_timeoutEvents;

    SignalingMessageId m_nextMessageId;
    bool m_initialized;

    Statistics m_stats;

    std::map<std::pair<std::string, std::string>, Ptr<QuantumDelayModel>> m_linkDelayModels;
};

class SwapScheduler : public Object
{
public:
    static TypeId GetTypeId (void);

    SwapScheduler ();
    ~SwapScheduler () override;

    void DoDispose (void) override;

    void SetSignalingChannel (Ptr<QuantumSignalingChannel> channel);
    Ptr<QuantumSignalingChannel> GetSignalingChannel (void) const;

    void SetBaseSwapDuration (Time duration);
    Time GetBaseSwapDuration (void) const;

    std::vector<SwapScheduleEntry> GenerateSchedule (
        const std::vector<std::string> &pathNodes
    );

    Time GetScheduleDuration (uint32_t numHops) const;

    uint32_t GetNumRounds (uint32_t numHops) const;

    void PrintSchedule (const std::vector<SwapScheduleEntry> &schedule) const;

private:
    uint32_t FindRound (uint32_t nodeIndex, uint32_t numNodes) const;

    Ptr<QuantumSignalingChannel> m_signalingChannel;
    Time m_baseSwapDuration;
    bool m_initialized;
};

} // namespace ns3

#endif /* QUANTUM_SIGNALING_CHANNEL_H */