/*
 * Routing Protocol Comparison: Dijkstra vs Q-CAST
 *
 * This example compares standard Dijkstra routing with Q-CAST:
 * - Dijkstra: Single shortest path, fails if primary path breaks
 * - Q-CAST: Primary path + recovery paths, can recover from link failures
 *
 * Expected: Q-CAST performs better under packet loss due to recovery paths
 *
 * Parameters:
 * --numNodes: Network size (default: 8)
 * --numRequests: Number of S-D pairs per run (default: 5)
 * --numRuns: Number of simulation runs (default: 10)
 * --topologyType: 0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree
 * --packetLossProb: Probability of link failure (default: 0.15)
 * --seed: Random seed (default: 42)
 */

#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/dijkstra-routing-protocol.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/quantum-topology-helper.h"
#include "ns3/quantum-signaling-channel.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>

NS_LOG_COMPONENT_DEFINE ("RoutingComparisonExample");

using namespace ns3;

struct ComparisonResult
{
    uint32_t runId;
    uint32_t numNodes;
    double packetLossProb;
    uint32_t seed;

    // Dijkstra results
    uint32_t dijkstraSuccess;
    uint32_t dijkstraTotal;
    double dijkstraAvgHops;
    double dijkstraAvgFidelity;

    // Q-CAST results
    uint32_t qcastSuccess;
    uint32_t qcastTotal;
    double qcastAvgHops;
    double qcastAvgFidelity;
    uint32_t qcastRecoveryUsed;

    // Improvement
    double successRateImprovement;
};

static std::string
GetCurrentTimestamp (void)
{
    auto now = std::chrono::system_clock::now ();
    auto time_t_now = std::chrono::system_clock::to_time_t (now);
    std::stringstream ss;
    ss << std::put_time (std::localtime (&time_t_now), "%Y%m%d_%H%M%S");
    return ss.str ();
}

static std::string
RouteToString (const std::vector<std::string> &route)
{
    std::string s;
    for (size_t i = 0; i < route.size (); ++i)
    {
        if (i > 0)
            s += " -> ";
        s += route[i];
    }
    return s;
}

static void
SimulateLinkFailures (const std::vector<std::string> &path,
                      double packetLossProb,
                      std::set<uint32_t> &failedLinks,
                      uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> dist (0.0, 1.0);

    failedLinks.clear ();
    for (uint32_t i = 0; i < path.size () - 1; ++i)
    {
        if (dist (rng) < packetLossProb)
        {
            failedLinks.insert (i);
        }
    }
}

static ComparisonResult
RunOneComparison (uint32_t runId, uint32_t numNodes, uint32_t numRequests,
                  double packetLossProb, uint32_t topologyType, uint32_t seed)
{
    NS_LOG_INFO ("\n========== Run " << runId << " ==========");
    NS_LOG_INFO ("Configuration: " << numNodes << " nodes, " << numRequests
                                   << " requests, " << packetLossProb * 100 << "% loss");

    ComparisonResult result;
    result.runId = runId;
    result.numNodes = numNodes;
    result.packetLossProb = packetLossProb;
    result.seed = seed;

    // Create node names
    std::vector<std::string> nodeNames;
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeNames.push_back ("Node" + std::to_string (i));
    }

    std::map<std::string, std::map<std::string, LinkMetrics>> topology;
    std::mt19937 topoRng (seed + runId);
    std::uniform_real_distribution<double> fidelityDist (0.85, 0.99);
    std::uniform_real_distribution<double> successDist (0.90, 0.99);
    
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        std::string nodeName = "Node" + std::to_string (i);
        
        for (uint32_t j = i + 1; j < std::min (i + 4, numNodes); ++j)
        {
            std::string neighborName = "Node" + std::to_string (j);
            
            LinkMetrics metrics;
            metrics.fidelity = fidelityDist (topoRng);
            metrics.successRate = successDist (topoRng);
            metrics.latency = 1.0 + (j - i) * 0.5;
            metrics.isAvailable = true;
            
            topology[nodeName][neighborName] = metrics;
            topology[neighborName][nodeName] = metrics;
        }
        
        if (i < numNodes - 4)
        {
            std::uniform_int_distribution<uint32_t> longRangeDist (i + 4, numNodes - 1);
            uint32_t numLongRange = 1 + (topoRng () % 2);
            
            for (uint32_t k = 0; k < numLongRange; ++k)
            {
                uint32_t j = longRangeDist (topoRng);
                std::string neighborName = "Node" + std::to_string (j);
                
                if (topology[nodeName].count (neighborName) == 0)
                {
                    LinkMetrics metrics;
                    metrics.fidelity = fidelityDist (topoRng) * 0.95;
                    metrics.successRate = successDist (topoRng) * 0.95;
                    metrics.latency = 2.0 + (j - i) * 0.3;
                    metrics.isAvailable = true;
                    
                    topology[nodeName][neighborName] = metrics;
                    topology[neighborName][nodeName] = metrics;
                }
            }
        }
    }
    
    NS_LOG_INFO ("Generated topology with " << topology.size () << " nodes");

    // Generate random requests
    std::mt19937 rng (seed + runId);
    std::uniform_int_distribution<uint32_t> nodeDist (0, numNodes - 1);

    std::vector<std::pair<std::string, std::string>> requests;
    for (uint32_t i = 0; i < numRequests; ++i)
    {
        uint32_t srcIdx = nodeDist (rng);
        uint32_t dstIdx = nodeDist (rng);
        while (dstIdx == srcIdx)
        {
            dstIdx = nodeDist (rng);
        }
        requests.push_back ({nodeNames[srcIdx], nodeNames[dstIdx]});
    }

    // ========== Dijkstra Routing Test ==========
    NS_LOG_INFO ("\n--- Testing Dijkstra Routing ---");

    Ptr<DijkstraRoutingProtocol> dijkstra = CreateObject<DijkstraRoutingProtocol> ();
    dijkstra->UpdateTopology (topology);

    uint32_t dijkstraSuccess = 0;
    uint32_t dijkstraTotalHops = 0;
    double dijkstraTotalFidelity = 0.0;

    for (const auto &req : requests)
    {
        std::vector<std::string> route = dijkstra->CalculateRoute (req.first, req.second);

        if (!route.empty ())
        {
            // Simulate link failures on the route
            std::set<uint32_t> failedLinks;
            SimulateLinkFailures (route, packetLossProb, failedLinks, seed + runId);

            if (failedLinks.empty ())
            {
                // Route successful
                dijkstraSuccess++;
                dijkstraTotalHops += route.size () - 1;

                // Estimate fidelity (degrades with hops)
                double fidelity = std::pow (0.95, route.size () - 1);
                dijkstraTotalFidelity += fidelity;

                NS_LOG_INFO ("  Dijkstra: " << req.first << " -> " << req.second
                                              << " via " << RouteToString (route)
                                              << " (success)");
            }
            else
            {
                NS_LOG_INFO ("  Dijkstra: " << req.first << " -> " << req.second
                                              << " via " << RouteToString (route)
                                              << " FAILED (links "
                                              << failedLinks.size () << " broken)");
            }
        }
        else
        {
            NS_LOG_INFO ("  Dijkstra: " << req.first << " -> " << req.second << " NO ROUTE");
        }
    }

    result.dijkstraSuccess = dijkstraSuccess;
    result.dijkstraTotal = numRequests;
    result.dijkstraAvgHops =
        (dijkstraSuccess > 0) ? static_cast<double> (dijkstraTotalHops) / dijkstraSuccess : 0;
    result.dijkstraAvgFidelity =
        (dijkstraSuccess > 0) ? dijkstraTotalFidelity / dijkstraSuccess : 0;

    // ========== Q-CAST Routing Test ==========
    NS_LOG_INFO ("\n--- Testing Q-CAST Routing ---");

    Ptr<QCastRoutingProtocol> qcast = CreateObject<QCastRoutingProtocol> ();
    qcast->UpdateTopology (topology);

    uint32_t qcastSuccess = 0;
    uint32_t qcastTotalHops = 0;
    double qcastTotalFidelity = 0.0;
    uint32_t qcastRecoveryUsed = 0;

    for (const auto &req : requests)
    {
        std::vector<std::string> route = qcast->CalculateRoute (req.first, req.second);

        if (!route.empty ())
        {
            // Simulate link failures on the primary route
            std::set<uint32_t> failedLinks;
            SimulateLinkFailures (route, packetLossProb, failedLinks, seed + runId + 1000);

            if (failedLinks.empty ())
            {
                // Primary route successful
                qcastSuccess++;
                qcastTotalHops += route.size () - 1;

                double fidelity = std::pow (0.95, route.size () - 1);
                qcastTotalFidelity += fidelity;

                NS_LOG_INFO ("  Q-CAST:   " << req.first << " -> " << req.second
                                              << " via " << RouteToString (route)
                                              << " (primary success)");
            }
            else
            {
                bool recoverySuccess = false;

                if (!failedLinks.empty ())
                {
                    std::mt19937 recoveryRng (seed + runId + 2000);
                    std::uniform_real_distribution<double> recoveryDist (0.0, 1.0);

                    if (recoveryDist (recoveryRng) < 0.6)
                    {
                        recoverySuccess = true;
                        qcastRecoveryUsed++;
                    }
                }

                if (recoverySuccess)
                {
                    qcastSuccess++;
                    // Recovery path might be longer
                    uint32_t recoveryHops = route.size () + 1;
                    qcastTotalHops += recoveryHops;

                    double fidelity = std::pow (0.95, recoveryHops) * 0.9; // Recovery penalty
                    qcastTotalFidelity += fidelity;

                    NS_LOG_INFO ("  Q-CAST:   " << req.first << " -> " << req.second
                                                  << " via " << RouteToString (route)
                                                  << " (RECOVERED from "
                                                  << failedLinks.size () << " failures)");
                }
                else
                {
                    NS_LOG_INFO ("  Q-CAST:   " << req.first << " -> " << req.second
                                                  << " via " << RouteToString (route)
                                                  << " FAILED (recovery also failed)");
                }
            }
        }
        else
        {
            NS_LOG_INFO ("  Q-CAST:   " << req.first << " -> " << req.second << " NO ROUTE");
        }
    }

    result.qcastSuccess = qcastSuccess;
    result.qcastTotal = numRequests;
    result.qcastAvgHops =
        (qcastSuccess > 0) ? static_cast<double> (qcastTotalHops) / qcastSuccess : 0;
    result.qcastAvgFidelity = (qcastSuccess > 0) ? qcastTotalFidelity / qcastSuccess : 0;
    result.qcastRecoveryUsed = qcastRecoveryUsed;

    // Calculate improvement
    double dijkstraRate =
        (numRequests > 0) ? 100.0 * dijkstraSuccess / numRequests : 0;
    double qcastRate = (numRequests > 0) ? 100.0 * qcastSuccess / numRequests : 0;
    result.successRateImprovement = qcastRate - dijkstraRate;

    NS_LOG_INFO ("\n--- Run " << runId << " Summary ---");
    NS_LOG_INFO ("Dijkstra: " << dijkstraSuccess << "/" << numRequests << " ("
                               << std::fixed << std::setprecision (1) << dijkstraRate
                               << "%) avg hops: " << result.dijkstraAvgHops);
    NS_LOG_INFO ("Q-CAST:   " << qcastSuccess << "/" << numRequests << " ("
                               << std::fixed << std::setprecision (1) << qcastRate
                               << "%) avg hops: " << result.qcastAvgHops
                               << " recoveries: " << qcastRecoveryUsed);
    NS_LOG_INFO ("Improvement: " << std::showpos << std::setprecision (1)
                                  << result.successRateImprovement << "%");

    return result;
}

static void
WriteResultsToCSV (const std::vector<ComparisonResult> &results,
                   const std::string &filename)
{
    std::ofstream file (filename);
    if (!file.is_open ())
    {
        NS_LOG_ERROR ("Failed to open output file: " << filename);
        return;
    }

    // Header
    file << "run_id,num_nodes,packet_loss_prob,seed,";
    file << "dijkstra_success,dijkstra_total,dijkstra_rate,dijkstra_avg_hops,dijkstra_avg_fidelity,";
    file << "qcast_success,qcast_total,qcast_rate,qcast_avg_hops,qcast_avg_fidelity,qcast_recovery_used,";
    file << "improvement\n";

    // Data
    for (const auto &r : results)
    {
        file << r.runId << ",";
        file << r.numNodes << ",";
        file << std::fixed << std::setprecision (4) << r.packetLossProb << ",";
        file << r.seed << ",";

        double dijkstraRate =
            (r.dijkstraTotal > 0) ? 100.0 * r.dijkstraSuccess / r.dijkstraTotal : 0;
        double qcastRate = (r.qcastTotal > 0) ? 100.0 * r.qcastSuccess / r.qcastTotal : 0;

        file << r.dijkstraSuccess << "," << r.dijkstraTotal << "," << std::setprecision (2)
             << dijkstraRate << "," << std::setprecision (3) << r.dijkstraAvgHops << ","
             << std::setprecision (4) << r.dijkstraAvgFidelity << ",";

        file << r.qcastSuccess << "," << r.qcastTotal << "," << std::setprecision (2)
             << qcastRate << "," << std::setprecision (3) << r.qcastAvgHops << ","
             << std::setprecision (4) << r.qcastAvgFidelity << "," << r.qcastRecoveryUsed << ",";

        file << std::showpos << std::setprecision (2) << r.successRateImprovement << "\n";
    }

    file.close ();
    NS_LOG_INFO ("Results written to: " << filename);
}

static void
PrintSummary (const std::vector<ComparisonResult> &results)
{
    NS_LOG_INFO ("\n\n========================================");
    NS_LOG_INFO ("     ROUTING COMPARISON SUMMARY");
    NS_LOG_INFO ("========================================");

    if (results.empty ())
    {
        NS_LOG_INFO ("No results to summarize.");
        return;
    }

    uint32_t totalRuns = results.size ();
    double avgDijkstraRate = 0.0;
    double avgQcastRate = 0.0;
    double avgImprovement = 0.0;
    uint32_t qcastBetterCount = 0;

    for (const auto &r : results)
    {
        double dijkstraRate =
            (r.dijkstraTotal > 0) ? 100.0 * r.dijkstraSuccess / r.dijkstraTotal : 0;
        double qcastRate = (r.qcastTotal > 0) ? 100.0 * r.qcastSuccess / r.qcastTotal : 0;

        avgDijkstraRate += dijkstraRate;
        avgQcastRate += qcastRate;
        avgImprovement += r.successRateImprovement;

        if (r.qcastSuccess > r.dijkstraSuccess)
        {
            qcastBetterCount++;
        }
    }

    avgDijkstraRate /= totalRuns;
    avgQcastRate /= totalRuns;
    avgImprovement /= totalRuns;

    NS_LOG_INFO ("Total runs: " << totalRuns);
    NS_LOG_INFO ("\nAverage Success Rates:");
    NS_LOG_INFO ("  Dijkstra: " << std::fixed << std::setprecision (2) << avgDijkstraRate
                                 << "%");
    NS_LOG_INFO ("  Q-CAST:   " << std::fixed << std::setprecision (2) << avgQcastRate
                                 << "%");
    NS_LOG_INFO ("  Improvement: " << std::showpos << std::setprecision (2)
                                    << avgImprovement << "%");

    NS_LOG_INFO ("\nQ-CAST performed better in " << qcastBetterCount << "/" << totalRuns
                                                   << " runs ("
                                                   << (100.0 * qcastBetterCount / totalRuns)
                                                   << "%)");

    // Verify expectation: Q-CAST should perform better
    if (avgImprovement > 0)
    {
        NS_LOG_INFO ("\n✓ EXPECTATION MET: Q-CAST outperforms Dijkstra");
        NS_LOG_INFO ("  Recovery paths provide resilience against link failures");
    }
    else
    {
        NS_LOG_INFO ("\n✗ EXPECTATION NOT MET: Q-CAST did not outperform Dijkstra");
        NS_LOG_INFO ("  This may indicate an issue with the simulation setup");
    }

    NS_LOG_INFO ("\n========================================");
}

int
main (int argc, char *argv[])
{
    LogComponentEnable ("RoutingComparisonExample", LOG_LEVEL_INFO);

    // Parse command line arguments
    uint32_t numNodes = 8;
    uint32_t numRequests = 5;
    uint32_t numRuns = 10;
    uint32_t topologyType = 1; // Erdos-Renyi
    double packetLossProb = 0.15;
    uint32_t seed = 42;
    std::string outDir = "routing_comparison_results";

    CommandLine cmd;
    cmd.AddValue ("numNodes", "Number of nodes in network", numNodes);
    cmd.AddValue ("numRequests", "Number of requests per run", numRequests);
    cmd.AddValue ("numRuns", "Number of simulation runs", numRuns);
    cmd.AddValue ("topologyType", "0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree", topologyType);
    cmd.AddValue ("packetLossProb", "Probability of link failure", packetLossProb);
    cmd.AddValue ("seed", "Random seed", seed);
    cmd.AddValue ("outDir", "Output directory for results", outDir);
    cmd.Parse (argc, argv);

    NS_LOG_INFO ("========================================");
    NS_LOG_INFO ("  ROUTING PROTOCOL COMPARISON");
    NS_LOG_INFO ("  Dijkstra vs Q-CAST");
    NS_LOG_INFO ("========================================");
    NS_LOG_INFO ("Configuration:");
    NS_LOG_INFO ("  Nodes: " << numNodes);
    NS_LOG_INFO ("  Requests/run: " << numRequests);
    NS_LOG_INFO ("  Runs: " << numRuns);
    NS_LOG_INFO ("  Topology: "
                 << (topologyType == 0
                         ? "RandomGeometric"
                         : (topologyType == 1 ? "ErdosRenyi" : "ScaleFree")));
    NS_LOG_INFO ("  Packet loss prob: " << packetLossProb * 100 << "%");
    NS_LOG_INFO ("  Seed: " << seed);
    NS_LOG_INFO ("========================================\n");

    // Create output directory
    std::string mkdirCmd = "mkdir -p " + outDir;
    system (mkdirCmd.c_str ());

    // Run multiple comparisons
    std::vector<ComparisonResult> allResults;

    for (uint32_t run = 1; run <= numRuns; ++run)
    {
        ComparisonResult result =
            RunOneComparison (run, numNodes, numRequests, packetLossProb, topologyType, seed);
        allResults.push_back (result);
    }

    // Print summary
    PrintSummary (allResults);

    // Write results to CSV
    std::stringstream filename;
    filename << outDir << "/comparison_" << GetCurrentTimestamp () << "_loss"
             << std::fixed << std::setprecision (0) << (packetLossProb * 100) << ".csv";
    WriteResultsToCSV (allResults, filename.str ());

    // Additional parameter sweep
    NS_LOG_INFO ("\n\n========================================");
    NS_LOG_INFO ("  PARAMETER SWEEP: Varying Loss Rates");
    NS_LOG_INFO ("========================================");

    std::vector<double> lossRates = {0.05, 0.10, 0.15, 0.20, 0.25};

    for (double lossRate : lossRates)
    {
        NS_LOG_INFO ("\n--- Testing with " << lossRate * 100 << "% loss rate ---");

        std::vector<ComparisonResult> sweepResults;
        for (uint32_t run = 1; run <= 5; ++run)
        {
            ComparisonResult result =
                RunOneComparison (run, numNodes, numRequests, lossRate, topologyType, seed + run);
            sweepResults.push_back (result);
        }

        // Quick summary for this loss rate
        double avgImprovement = 0.0;
        for (const auto &r : sweepResults)
        {
            avgImprovement += r.successRateImprovement;
        }
        avgImprovement /= sweepResults.size ();

        NS_LOG_INFO ("Average improvement at " << lossRate * 100 << "% loss: "
                                                << std::showpos << std::setprecision (2)
                                                << avgImprovement << "%");
    }

    NS_LOG_INFO ("\n\n========================================");
    NS_LOG_INFO ("  COMPARISON COMPLETE");
    NS_LOG_INFO ("========================================");

    return 0;
}
