/*
 * Signaling Delay Example - Multi-Run Simulation
 *
 * This example demonstrates the quantum signaling delay simulation with multiple runs:
 * 1. Packet loss probability (no retransmission, timeout = failure)
 * 2. Logarithmic swap scheduling following Q-CAST paper
 * 3. Delay model reading from topology configuration
 * 4. Automatic decoherence calculation based on discrete timeline
 *
 * The signaling channel is decoupled from routing protocols - it only simulates
 * classical network communication delays for quantum control messages.
 */

#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-signaling-channel.h"
#include "ns3/quantum-delay-model.h"

#include <iostream>
#include <vector>
#include <iomanip>
#include <fstream>
#include <chrono>

NS_LOG_COMPONENT_DEFINE ("SignalingDelayExample");

using namespace ns3;

struct RunResult
{
    uint32_t runId;
    uint32_t seed;
    uint32_t numNodes;
    double packetLossProb;
    double timeoutMs;
    uint32_t messagesSent;
    uint32_t messagesDelivered;
    uint32_t messagesLost;
    uint32_t messagesTimedOut;
    double deliveryRate;
    double lossRate;
    double timeoutRate;
    double avgDelayMs;
    double minDelayMs;
    double maxDelayMs;
};

struct MessageStats
{
    uint32_t delivered = 0;
    uint32_t lost = 0;
    uint32_t timedOut = 0;
};

static void
MessageStatusCallback (SignalingMessageId id, SignalingMessageState state, MessageStats *stats)
{
    switch (state)
    {
    case SignalingMessageState::DELIVERED:
        stats->delivered++;
        break;
    case SignalingMessageState::LOST:
        stats->lost++;
        break;
    case SignalingMessageState::TIMEOUT:
        stats->timedOut++;
        break;
    default:
        break;
    }
}

static std::vector<std::string>
CreateLinearTopology (uint32_t numNodes, const std::string &prefix,
                      std::map<std::pair<std::string, std::string>, Ptr<QuantumDelayModel>> &linkDelays)
{
    std::vector<std::string> nodeNames;
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeNames.push_back (prefix + "Node" + std::to_string (i));
    }

    for (uint32_t i = 0; i < numNodes - 1; ++i)
    {
        std::string node1 = nodeNames[i];
        std::string node2 = nodeNames[i + 1];

        Ptr<BurstDelayModel> delayModel = CreateObject<BurstDelayModel> ();
        delayModel->SetBaseDelay (MilliSeconds (5 + i * 2));
        delayModel->SetMaxDeviation (MilliSeconds (3));
        delayModel->SetBurstProbability (0.05);
        delayModel->SetBurstDuration (MilliSeconds (20));
        delayModel->SetBurstMultiplier (1.5);
        delayModel->Initialize (MilliSeconds (50));

        linkDelays[{node1, node2}] = delayModel;
        linkDelays[{node2, node1}] = delayModel;
    }

    return nodeNames;
}

static RunResult
RunOneSimulation (uint32_t runId, uint32_t seed, uint32_t numNodes,
                  double packetLossProb, double timeoutMs, uint32_t numMessages)
{
    Simulator::Destroy ();
    SeedManager::SetSeed (seed);

    std::string runPrefix = "run" + std::to_string (runId) + "_";
    std::vector<std::string> owners;
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        owners.push_back (runPrefix + "Node" + std::to_string (i));
    }
    owners.push_back (runPrefix + "God");
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);

    std::map<std::pair<std::string, std::string>, Ptr<QuantumDelayModel>> linkDelays;
    std::vector<std::string> pathNodes = CreateLinearTopology (numNodes, runPrefix, linkDelays);

    Ptr<QuantumSignalingChannel> signalingChannel = CreateObject<QuantumSignalingChannel> ();
    signalingChannel->SetPhysicalEntity (qphyent);
    signalingChannel->SetPacketLossProbability (packetLossProb);
    signalingChannel->SetTimeoutDuration (MilliSeconds (timeoutMs));

    for (const auto &entry : linkDelays)
    {
        signalingChannel->SetLinkDelayModel (entry.first.first, entry.first.second, entry.second);
    }

    Ptr<BurstDelayModel> defaultDelayModel = CreateObject<BurstDelayModel> ();
    defaultDelayModel->SetBaseDelay (MilliSeconds (10));
    defaultDelayModel->SetMaxDeviation (MilliSeconds (5));
    defaultDelayModel->SetBurstProbability (0.05);
    defaultDelayModel->SetBurstDuration (MilliSeconds (50));
    defaultDelayModel->SetBurstMultiplier (2.0);
    defaultDelayModel->Initialize (MilliSeconds (100));
    signalingChannel->SetDelayModel (defaultDelayModel);

    MessageStats stats;

    for (uint32_t i = 0; i < numMessages; ++i)
    {
        std::string srcNode = pathNodes[i % (pathNodes.size () - 1)];
        std::string dstNode = pathNodes[(i + 1) % (pathNodes.size () - 1)];

        SignalingMessageId id =
            signalingChannel->SendMessage (srcNode, dstNode, SignalingMessageType::SWAP_REQUEST,
                                           "Swap request " + std::to_string (i), i, 0, true);

        signalingChannel->RegisterCallback (
            id, [&stats] (SignalingMessageId, SignalingMessageState state) {
                MessageStatusCallback (0, state, &stats);
            });
    }

    Simulator::Stop (Seconds (2.0));
    Simulator::Run ();

    auto channelStats = signalingChannel->GetStatistics ();

    RunResult result;
    result.runId = runId;
    result.seed = seed;
    result.numNodes = numNodes;
    result.packetLossProb = packetLossProb;
    result.timeoutMs = timeoutMs;
    result.messagesSent = channelStats.messagesSent;
    result.messagesDelivered = stats.delivered;
    result.messagesLost = stats.lost;
    result.messagesTimedOut = stats.timedOut;
    result.deliveryRate = channelStats.messagesSent > 0 ? (100.0 * stats.delivered / channelStats.messagesSent) : 0.0;
    result.lossRate = channelStats.messagesSent > 0 ? (100.0 * stats.lost / channelStats.messagesSent) : 0.0;
    result.timeoutRate = channelStats.messagesSent > 0 ? (100.0 * stats.timedOut / channelStats.messagesSent) : 0.0;
    result.avgDelayMs = channelStats.messagesDelivered > 0 ? channelStats.averageDelay : 0.0;
    result.minDelayMs = channelStats.minDelay.GetMilliSeconds ();
    result.maxDelayMs = channelStats.maxDelay.GetMilliSeconds ();

    Simulator::Destroy ();

    return result;
}

static void
PrintSummary (const std::vector<RunResult> &results)
{
    if (results.empty ())
        return;

    double avgDelivery = 0.0, avgLoss = 0.0, avgTimeout = 0.0;
    double avgDelay = 0.0, minDelay = results[0].minDelayMs, maxDelay = results[0].maxDelayMs;

    for (const auto &r : results)
    {
        avgDelivery += r.deliveryRate;
        avgLoss += r.lossRate;
        avgTimeout += r.timeoutRate;
        avgDelay += r.avgDelayMs;
        if (r.minDelayMs < minDelay)
            minDelay = r.minDelayMs;
        if (r.maxDelayMs > maxDelay)
            maxDelay = r.maxDelayMs;
    }

    size_t n = results.size ();
    avgDelivery /= n;
    avgLoss /= n;
    avgTimeout /= n;
    avgDelay /= n;

    std::cout << "\n=== Summary Statistics ===\n";
    std::cout << "Total runs: " << n << "\n";
    std::cout << std::fixed << std::setprecision (2);
    std::cout << "Average delivery rate: " << avgDelivery << "%\n";
    std::cout << "Average loss rate: " << avgLoss << "%\n";
    std::cout << "Average timeout rate: " << avgTimeout << "%\n";
    std::cout << "Average delay: " << avgDelay << " ms\n";
    std::cout << "Min delay: " << minDelay << " ms\n";
    std::cout << "Max delay: " << maxDelay << " ms\n";
    std::cout << "==========================\n";
}

static void
WriteResults (const std::string &filename, const std::vector<RunResult> &results,
              uint32_t numRuns, double packetLossProb, double timeoutMs, uint32_t numMessages)
{
    std::ofstream file (filename);
    if (!file.is_open ())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }

    file << "# Signaling Delay Simulation Results\n";
    file << "# Runs: " << numRuns << "  PacketLossProb: " << packetLossProb
         << "  Timeout: " << timeoutMs << "ms  Messages: " << numMessages << "\n\n";

    file << "run_id,seed,num_nodes,packet_loss_prob,timeout_ms,"
         << "messages_sent,messages_delivered,messages_lost,messages_timed_out,"
         << "delivery_rate_pct,loss_rate_pct,timeout_rate_pct,"
         << "avg_delay_ms,min_delay_ms,max_delay_ms\n";

    for (const auto &r : results)
    {
        file << r.runId << "," << r.seed << "," << r.numNodes << ","
             << std::fixed << std::setprecision (4)
             << r.packetLossProb << "," << r.timeoutMs << ","
             << r.messagesSent << "," << r.messagesDelivered << ","
             << r.messagesLost << "," << r.messagesTimedOut << ","
             << std::setprecision (2)
             << r.deliveryRate << "," << r.lossRate << "," << r.timeoutRate << ","
             << r.avgDelayMs << "," << r.minDelayMs << "," << r.maxDelayMs << "\n";
    }

    file.close ();
    std::cout << "Results written to: " << filename << std::endl;
}

int
main (int argc, char *argv[])
{
    LogComponentEnable ("SignalingDelayExample", LOG_LEVEL_INFO);

    uint32_t numRuns = 10;
    uint32_t numNodes = 5;
    double packetLossProb = 0.05;
    double timeoutMs = 100.0;
    uint32_t baseSeed = 1;
    uint32_t numMessages = 20;
    std::string outDir = "signaling_results";

    CommandLine cmd;
    cmd.AddValue ("numRuns", "Number of simulation runs", numRuns);
    cmd.AddValue ("numNodes", "Number of nodes in the path", numNodes);
    cmd.AddValue ("packetLossProb", "Packet loss probability (default: 0.05)", packetLossProb);
    cmd.AddValue ("timeoutMs", "Timeout duration in milliseconds", timeoutMs);
    cmd.AddValue ("baseSeed", "Base random seed", baseSeed);
    cmd.AddValue ("numMessages", "Number of messages per run", numMessages);
    cmd.AddValue ("outDir", "Output directory", outDir);
    cmd.Parse (argc, argv);

    system (("mkdir -p " + outDir).c_str ());

    std::cout << "\n=== Signaling Delay Multi-Run Simulation ===\n";
    std::cout << "Runs: " << numRuns << "  Nodes: " << numNodes
              << "  Messages: " << numMessages << "\n";
    std::cout << "Packet Loss Prob: " << packetLossProb
              << "  Timeout: " << timeoutMs << " ms\n";
    std::cout << "Output directory: " << outDir << "/\n\n";

    std::vector<RunResult> allResults;

    for (uint32_t run = 1; run <= numRuns; ++run)
    {
        uint32_t seed = baseSeed + run - 1;

        std::cout << "[Run " << run << "/" << numRuns << "  seed=" << seed << "] ";
        std::cout.flush ();

        auto t0 = std::chrono::high_resolution_clock::now ();

        RunResult result = RunOneSimulation (run, seed, numNodes, packetLossProb,
                                             timeoutMs, numMessages);
        allResults.push_back (result);

        auto t1 = std::chrono::high_resolution_clock::now ();
        long wallMs = std::chrono::duration_cast<std::chrono::milliseconds> (t1 - t0).count ();

        std::cout << "Delivered: " << result.messagesDelivered << "/" << result.messagesSent
                  << " (" << std::fixed << std::setprecision (1) << result.deliveryRate << "%)"
                  << "  Wall time: " << wallMs << " ms\n";
    }

    PrintSummary (allResults);

    std::ostringstream filename;
    filename << outDir << "/results_loss" << std::fixed << std::setprecision (2)
             << (packetLossProb * 100) << "_timeout" << static_cast<int> (timeoutMs)
             << ".csv";
    WriteResults (filename.str (), allResults, numRuns, packetLossProb, timeoutMs, numMessages);

    return 0;
}
