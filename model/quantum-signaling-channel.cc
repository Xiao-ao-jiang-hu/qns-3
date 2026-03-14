#include "ns3/quantum-signaling-channel.h"

#include "ns3/quantum-delay-model.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"

#include <cmath>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumSignalingChannel");

//=============================================================================
// QuantumSignalingChannel Implementation
//=============================================================================

NS_OBJECT_ENSURE_REGISTERED (QuantumSignalingChannel);

TypeId
QuantumSignalingChannel::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QuantumSignalingChannel")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddConstructor<QuantumSignalingChannel> ()
          .AddAttribute ("PacketLossProbability",
                         "Probability of packet loss for signaling messages (0.0 - 1.0)",
                         DoubleValue (0.05),
                         MakeDoubleAccessor (&QuantumSignalingChannel::m_packetLossProbability),
                         MakeDoubleChecker<double> (0.0, 1.0))
          .AddAttribute ("TimeoutDuration",
                         "Timeout duration for signaling message acknowledgment",
                         TimeValue (MilliSeconds (500)),
                         MakeTimeAccessor (&QuantumSignalingChannel::m_timeoutDuration),
                         MakeTimeChecker ());
  return tid;
}

QuantumSignalingChannel::QuantumSignalingChannel ()
    : m_delayModel (nullptr),
      m_defaultDelayModel (nullptr),
      m_qphyent (nullptr),
      m_packetLossProbability (0.05),
      m_timeoutDuration (MilliSeconds (500)),
      m_nextMessageId (1),
      m_initialized (false)
{
  NS_LOG_FUNCTION (this);
  m_rng = CreateObject<UniformRandomVariable> ();
}

QuantumSignalingChannel::~QuantumSignalingChannel ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumSignalingChannel::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  // Cancel all pending timeout events
  for (auto &event : m_timeoutEvents)
    {
      event.second.Cancel ();
    }
  m_timeoutEvents.clear ();

  // Clear callbacks to break reference cycles
  m_callbacks.clear ();
  m_messages.clear ();

  m_delayModel = nullptr;
  m_defaultDelayModel = nullptr;
  m_qphyent = nullptr;
  m_rng = nullptr;

  Object::DoDispose ();
}

void
QuantumSignalingChannel::SetDelayModel (Ptr<QuantumDelayModel> delayModel)
{
  NS_LOG_FUNCTION (this << delayModel);
  m_delayModel = delayModel;
}

Ptr<QuantumDelayModel>
QuantumSignalingChannel::GetDelayModel (void) const
{
  return m_delayModel;
}

void
QuantumSignalingChannel::SetPacketLossProbability (double prob)
{
  NS_LOG_FUNCTION (this << prob);
  NS_ASSERT (prob >= 0.0 && prob <= 1.0);
  m_packetLossProbability = prob;
}

double
QuantumSignalingChannel::GetPacketLossProbability (void) const
{
  return m_packetLossProbability;
}

void
QuantumSignalingChannel::SetTimeoutDuration (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_timeoutDuration = timeout;
}

Time
QuantumSignalingChannel::GetTimeoutDuration (void) const
{
  return m_timeoutDuration;
}

void
QuantumSignalingChannel::SetPhysicalEntity (Ptr<QuantumPhyEntity> qphyent)
{
  NS_LOG_FUNCTION (this << qphyent);
  m_qphyent = qphyent;
}

Ptr<QuantumPhyEntity>
QuantumSignalingChannel::GetPhysicalEntity (void) const
{
  return m_qphyent;
}

SignalingMessageId
QuantumSignalingChannel::SendMessage (const std::string &srcNode,
                                      const std::string &dstNode,
                                      SignalingMessageType type,
                                      const std::string &payload,
                                      uint32_t hopIndex,
                                      uint32_t roundIndex,
                                      bool requiresAck)
{
  NS_LOG_FUNCTION (this << srcNode << dstNode << static_cast<int> (type) << payload
                        << hopIndex << roundIndex << requiresAck);

  SignalingMessageId id = m_nextMessageId++;

  SignalingMessage msg;
  msg.id = id;
  msg.type = type;
  msg.srcNode = srcNode;
  msg.dstNode = dstNode;
  msg.payload = payload;
  msg.sendTime = Simulator::Now ();
  msg.deliveryTime = Time (0);
  msg.state = SignalingMessageState::PENDING;
  msg.requiresAck = requiresAck;
  msg.hopIndex = hopIndex;
  msg.roundIndex = roundIndex;

  m_messages[id] = msg;

  NS_LOG_INFO ("Signaling message " << id << " queued: " << srcNode << " -> " << dstNode
                                     << " type=" << static_cast<int> (type));

  // Schedule the actual send
  Simulator::ScheduleNow (&QuantumSignalingChannel::DoSendMessage, this, id);

  // Update statistics
  m_stats.messagesSent++;

  return id;
}

void
QuantumSignalingChannel::DoSendMessage (SignalingMessageId id)
{
  NS_LOG_FUNCTION (this << id);

  auto it = m_messages.find (id);
  if (it == m_messages.end ())
    {
      NS_LOG_WARN ("Message " << id << " not found");
      return;
    }

  SignalingMessage &msg = it->second;

  // Check for packet loss
  if (m_rng->GetValue () < m_packetLossProbability)
    {
      NS_LOG_INFO ("Signaling message " << id << " LOST (packet loss simulation)");
      DropMessage (id, SignalingMessageState::LOST);
      return;
    }

  // Calculate delay for this link
  Time delay = GetLinkDelay (msg.srcNode, msg.dstNode);

  // Schedule delivery
  Simulator::Schedule (delay, &QuantumSignalingChannel::DeliverMessage, this, id);

  NS_LOG_INFO ("Signaling message " << id << " will be delivered after " << delay.As (Time::MS));

  // Schedule timeout if acknowledgment is required
  if (msg.requiresAck)
    {
      EventId timeoutEvent =
          Simulator::Schedule (delay + m_timeoutDuration, &QuantumSignalingChannel::HandleTimeout,
                               this, id);
      m_timeoutEvents[id] = timeoutEvent;
    }
}

void
QuantumSignalingChannel::DeliverMessage (SignalingMessageId id)
{
  NS_LOG_FUNCTION (this << id);

  auto it = m_messages.find (id);
  if (it == m_messages.end ())
    {
      return;
    }

  SignalingMessage &msg = it->second;

  // Check if already timed out or lost
  if (msg.state != SignalingMessageState::PENDING)
    {
      NS_LOG_DEBUG ("Message " << id << " already in state " << static_cast<int> (msg.state));
      return;
    }

  msg.state = SignalingMessageState::DELIVERED;
  msg.deliveryTime = Simulator::Now ();

  // Calculate delay statistics
  Time actualDelay = msg.deliveryTime - msg.sendTime;
  m_stats.totalDelay += actualDelay;
  if (actualDelay > m_stats.maxDelay)
    m_stats.maxDelay = actualDelay;
  if (m_stats.minDelay == Time (0) || actualDelay < m_stats.minDelay)
    m_stats.minDelay = actualDelay;

  m_stats.messagesDelivered++;

  NS_LOG_INFO ("Signaling message " << id << " DELIVERED to " << msg.dstNode << " after "
                                     << actualDelay.As (Time::MS));

  // Invoke callback if registered
  auto callbackIt = m_callbacks.find (id);
  if (callbackIt != m_callbacks.end ())
    {
      callbackIt->second (id, SignalingMessageState::DELIVERED);
    }

  // Cancel timeout event since message was delivered
  auto timeoutIt = m_timeoutEvents.find (id);
  if (timeoutIt != m_timeoutEvents.end ())
    {
      timeoutIt->second.Cancel ();
      m_timeoutEvents.erase (timeoutIt);
    }
}

void
QuantumSignalingChannel::DropMessage (SignalingMessageId id, SignalingMessageState reason)
{
  NS_LOG_FUNCTION (this << id << static_cast<int> (reason));

  auto it = m_messages.find (id);
  if (it == m_messages.end ())
    {
      return;
    }

  SignalingMessage &msg = it->second;
  msg.state = reason;

  if (reason == SignalingMessageState::LOST)
    {
      m_stats.messagesLost++;
    }

  NS_LOG_INFO ("Signaling message " << id << " DROPPED: " << msg.srcNode << " -> " << msg.dstNode
                                     << " reason=" << static_cast<int> (reason));

  // Invoke callback if registered
  auto callbackIt = m_callbacks.find (id);
  if (callbackIt != m_callbacks.end ())
    {
      callbackIt->second (id, reason);
    }

  // Cancel any pending timeout
  auto timeoutIt = m_timeoutEvents.find (id);
  if (timeoutIt != m_timeoutEvents.end ())
    {
      timeoutIt->second.Cancel ();
      m_timeoutEvents.erase (timeoutIt);
    }
}

void
QuantumSignalingChannel::HandleTimeout (SignalingMessageId id)
{
  NS_LOG_FUNCTION (this << id);

  auto it = m_messages.find (id);
  if (it == m_messages.end ())
    {
      return;
    }

  SignalingMessage &msg = it->second;

  // Only handle timeout if still pending
  if (msg.state == SignalingMessageState::PENDING)
    {
      NS_LOG_WARN ("Signaling message " << id << " TIMED OUT after "
                                         << m_timeoutDuration.As (Time::MS));
      DropMessage (id, SignalingMessageState::TIMEOUT);
      m_stats.messagesTimedOut++;
    }

  // Remove from timeout events map
  m_timeoutEvents.erase (id);
}

void
QuantumSignalingChannel::RegisterCallback (SignalingMessageId id, SignalingCallback callback)
{
  NS_LOG_FUNCTION (this << id);
  m_callbacks[id] = callback;
}

Time
QuantumSignalingChannel::GetLinkDelay (const std::string &srcNode,
                                       const std::string &dstNode)
{
  NS_LOG_FUNCTION (this << srcNode << dstNode);

  // First check if there's a per-link delay model
  auto linkKey = std::make_pair (srcNode, dstNode);
  auto linkIt = m_linkDelayModels.find (linkKey);
  if (linkIt != m_linkDelayModels.end () && linkIt->second)
    {
      Time delay = linkIt->second->GetCurrentDelay ();
      NS_LOG_DEBUG ("Using per-link delay model for " << srcNode << " -> " << dstNode << ": "
                                                       << delay.As (Time::MS));
      return delay;
    }

  // Fall back to the general delay model
  if (m_delayModel)
    {
      Time delay = m_delayModel->GetCurrentDelay ();
      NS_LOG_DEBUG ("Using global delay model for " << srcNode << " -> " << dstNode << ": "
                                                     << delay.As (Time::MS));
      return delay;
    }

  // Use default delay if no model is set
  NS_LOG_DEBUG ("Using default delay for " << srcNode << " -> " << dstNode);
  return MilliSeconds (10);
}

void
QuantumSignalingChannel::SetLinkDelayModel (const std::string &srcNode,
                                            const std::string &dstNode,
                                            Ptr<QuantumDelayModel> delayModel)
{
  NS_LOG_FUNCTION (this << srcNode << dstNode << delayModel);
  auto linkKey = std::make_pair (srcNode, dstNode);
  m_linkDelayModels[linkKey] = delayModel;
}

std::vector<SwapScheduleEntry>
QuantumSignalingChannel::GenerateLogarithmicSwapSchedule (
    const std::vector<std::string> &pathNodes, Time baseSwapDuration)
{
  NS_LOG_FUNCTION (this << pathNodes.size () << baseSwapDuration);

  std::vector<SwapScheduleEntry> schedule;

  if (pathNodes.size () < 3)
    {
      NS_LOG_DEBUG ("Path has less than 3 nodes, no swapping needed");
      return schedule;
    }

  uint32_t numHops = static_cast<uint32_t> (pathNodes.size () - 1);
  uint32_t numRounds = GetNumSwapRounds (numHops);

  NS_LOG_INFO ("Generating logarithmic swap schedule: " << numHops << " hops, " << numRounds
                                                          << " rounds");

  // Intermediate nodes are at indices 1 to n-2 (0-indexed)
  // In each round r, certain nodes perform swapping
  // Round 1: nodes at positions 2, 4, 6, ... (even positions from start)
  // Round 2: nodes at positions 3, 7, 11, ... (every 4th from position 3)
  // etc.

  for (uint32_t round = 1; round <= numRounds; ++round)
    {
      Time roundStartTime = baseSwapDuration * (round - 1);

      // Calculate which nodes swap in this round
      // Using binary-tree-like scheduling
      uint32_t step = static_cast<uint32_t> (1 << round); // 2^round
      uint32_t offset = static_cast<uint32_t> (1 << (round - 1)); // 2^(round-1)

      for (uint32_t nodeIdx = offset; nodeIdx < numHops; nodeIdx += step)
        {
          if (nodeIdx >= pathNodes.size () - 1)
            break; // Skip if beyond path length

          // nodeIdx is the index in the path (1 to n-2 for intermediate nodes)
          // This represents the node that holds qubits from links (nodeIdx-1, nodeIdx) and (nodeIdx, nodeIdx+1)
          SwapScheduleEntry entry;
          entry.round = round;
          entry.nodeIndex = nodeIdx;
          entry.nodeName = pathNodes[nodeIdx];
          entry.scheduledTime = roundStartTime;

          // Identify qubits to be swapped at this node
          // These would be the qubits from the two adjacent links
          entry.qubitsToSwap.push_back (pathNodes[nodeIdx - 1] + "_" + pathNodes[nodeIdx]);
          entry.qubitsToSwap.push_back (pathNodes[nodeIdx] + "_" + pathNodes[nodeIdx + 1]);

          schedule.push_back (entry);

          NS_LOG_DEBUG ("Round " << round << ": Node " << pathNodes[nodeIdx]
                                  << " (index " << nodeIdx << ") scheduled at "
                                  << roundStartTime.As (Time::MS));
        }
    }

  return schedule;
}

uint32_t
QuantumSignalingChannel::GetNumSwapRounds (uint32_t numHops) const
{
  if (numHops <= 1)
    return 0;

  // Number of rounds is ceil(log2(numHops))
  return static_cast<uint32_t> (std::ceil (std::log2 (static_cast<double> (numHops))));
}

Time
QuantumSignalingChannel::CalculateTotalSignalingDelay (const std::vector<std::string> &pathNodes,
                                                       bool includeAck)
{
  NS_LOG_FUNCTION (this << pathNodes.size () << includeAck);

  if (pathNodes.size () < 2)
    {
      return Time (0);
    }

  Time totalDelay = Time (0);

  // Calculate one-way delay along the path
  for (size_t i = 0; i < pathNodes.size () - 1; ++i)
    {
      totalDelay += GetLinkDelay (pathNodes[i], pathNodes[i + 1]);
    }

  // If acknowledgment is required, add return path delay
  if (includeAck)
    {
      for (size_t i = pathNodes.size () - 1; i > 0; --i)
        {
          totalDelay += GetLinkDelay (pathNodes[i], pathNodes[i - 1]);
        }
    }

  NS_LOG_INFO ("Total signaling delay for path with " << pathNodes.size () << " nodes: "
                                                        << totalDelay.As (Time::MS));

  return totalDelay;
}

std::vector<SignalingMessage>
QuantumSignalingChannel::GetMessageHistory (void) const
{
  std::vector<SignalingMessage> history;
  for (const auto &entry : m_messages)
    {
      history.push_back (entry.second);
    }
  return history;
}

void
QuantumSignalingChannel::ClearHistory (void)
{
  NS_LOG_FUNCTION (this);
  m_messages.clear ();
  m_callbacks.clear ();
  // Note: timeout events are handled in DoDispose
}

QuantumSignalingChannel::Statistics
QuantumSignalingChannel::GetStatistics (void) const
{
  Statistics stats = m_stats;

  // Calculate average delay
  if (stats.messagesDelivered > 0)
    {
      stats.averageDelay =
          stats.totalDelay.GetDouble () / static_cast<double> (stats.messagesDelivered);
    }
  else
    {
      stats.averageDelay = 0.0;
    }

  return stats;
}

void
QuantumSignalingChannel::PrintStatistics (void) const
{
  Statistics stats = GetStatistics ();

  std::cout << "\n=== Quantum Signaling Channel Statistics ===\n";
  std::cout << "Messages Sent:      " << stats.messagesSent << "\n";
  std::cout << "Messages Delivered: " << stats.messagesDelivered << "\n";
  std::cout << "Messages Lost:      " << stats.messagesLost << "\n";
  std::cout << "Messages Timed Out: " << stats.messagesTimedOut << "\n";

  if (stats.messagesDelivered > 0)
    {
      std::cout << "Total Delay:        " << stats.totalDelay.As (Time::MS) << " ms\n";
      std::cout << "Average Delay:      " << stats.averageDelay << " ms\n";
      std::cout << "Min Delay:          " << stats.minDelay.As (Time::MS) << " ms\n";
      std::cout << "Max Delay:          " << stats.maxDelay.As (Time::MS) << " ms\n";
    }

  std::cout << "============================================\n";
}

//=============================================================================
// SwapScheduler Implementation
//=============================================================================

NS_OBJECT_ENSURE_REGISTERED (SwapScheduler);

TypeId
SwapScheduler::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::SwapScheduler")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddConstructor<SwapScheduler> ()
          .AddAttribute ("BaseSwapDuration",
                         "Base duration for each swap operation",
                         TimeValue (MilliSeconds (10)),
                         MakeTimeAccessor (&SwapScheduler::m_baseSwapDuration),
                         MakeTimeChecker ());
  return tid;
}

SwapScheduler::SwapScheduler ()
    : m_signalingChannel (nullptr),
      m_baseSwapDuration (MilliSeconds (10)),
      m_initialized (false)
{
  NS_LOG_FUNCTION (this);
}

SwapScheduler::~SwapScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
SwapScheduler::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_signalingChannel = nullptr;
  Object::DoDispose ();
}

void
SwapScheduler::SetSignalingChannel (Ptr<QuantumSignalingChannel> channel)
{
  NS_LOG_FUNCTION (this << channel);
  m_signalingChannel = channel;
}

Ptr<QuantumSignalingChannel>
SwapScheduler::GetSignalingChannel (void) const
{
  return m_signalingChannel;
}

void
SwapScheduler::SetBaseSwapDuration (Time duration)
{
  NS_LOG_FUNCTION (this << duration);
  m_baseSwapDuration = duration;
}

Time
SwapScheduler::GetBaseSwapDuration (void) const
{
  return m_baseSwapDuration;
}

std::vector<SwapScheduleEntry>
SwapScheduler::GenerateSchedule (const std::vector<std::string> &pathNodes)
{
  NS_LOG_FUNCTION (this << pathNodes.size ());

  if (m_signalingChannel)
    {
      return m_signalingChannel->GenerateLogarithmicSwapSchedule (pathNodes, m_baseSwapDuration);
    }

  // Fallback: generate schedule without signaling channel
  std::vector<SwapScheduleEntry> schedule;

  if (pathNodes.size () < 3)
    {
      return schedule;
    }

  uint32_t numHops = static_cast<uint32_t> (pathNodes.size () - 1);
  uint32_t numRounds = GetNumRounds (numHops);

  for (uint32_t round = 1; round <= numRounds; ++round)
    {
      Time roundStartTime = m_baseSwapDuration * (round - 1);
      uint32_t step = static_cast<uint32_t> (1 << round);
      uint32_t offset = static_cast<uint32_t> (1 << (round - 1));

      for (uint32_t nodeIdx = offset; nodeIdx < numHops; nodeIdx += step)
        {
          if (nodeIdx >= pathNodes.size () - 1)
            break;

          SwapScheduleEntry entry;
          entry.round = round;
          entry.nodeIndex = nodeIdx;
          entry.nodeName = pathNodes[nodeIdx];
          entry.scheduledTime = roundStartTime;
          entry.qubitsToSwap.push_back (pathNodes[nodeIdx - 1] + "_" + pathNodes[nodeIdx]);
          entry.qubitsToSwap.push_back (pathNodes[nodeIdx] + "_" + pathNodes[nodeIdx + 1]);

          schedule.push_back (entry);
        }
    }

  return schedule;
}

Time
SwapScheduler::GetScheduleDuration (uint32_t numHops) const
{
  uint32_t numRounds = GetNumRounds (numHops);
  return m_baseSwapDuration * numRounds;
}

uint32_t
SwapScheduler::GetNumRounds (uint32_t numHops) const
{
  if (numHops <= 1)
    return 0;

  return static_cast<uint32_t> (std::ceil (std::log2 (static_cast<double> (numHops))));
}

void
SwapScheduler::PrintSchedule (const std::vector<SwapScheduleEntry> &schedule) const
{
  std::cout << "\n=== Swap Schedule ===\n";
  std::cout << "Total entries: " << schedule.size () << "\n\n";

  uint32_t currentRound = 0;
  for (const auto &entry : schedule)
    {
      if (entry.round != currentRound)
        {
          currentRound = entry.round;
          std::cout << "Round " << currentRound << " (t=" << entry.scheduledTime.As (Time::MS)
                    << " ms):\n";
        }

      std::cout << "  Node " << entry.nodeName << " (index " << entry.nodeIndex << ")\n";
      std::cout << "    Qubits: ";
      for (const auto &qubit : entry.qubitsToSwap)
        {
          std::cout << qubit << " ";
        }
      std::cout << "\n";
    }

  std::cout << "=====================\n";
}

} // namespace ns3
