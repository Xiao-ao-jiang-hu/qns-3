/* 
  Q-CAST Random Topology Example with Fidelity Measurement
  
  This example demonstrates:
  1. Random network topology generation
  2. Q-CAST routing on random topologies
  3. End-to-end fidelity measurement for each request
  
  Topology Types:
  - Random Geometric: Nodes placed in 2D space, edges based on distance
  - Erdos-Renyi: Random edges with fixed probability
  - Scale-Free: Barabasi-Albert preferential attachment
  
  To run:
  ./ns3 run "q-cast-random-topology-example --numNodes=10 --numRequests=3 --topologyType=0"
  
  Parameters:
  --numNodes: Number of nodes in the topology (default: 10)
  --numRequests: Number of random S-D pairs (default: 3)
  --topologyType: 0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree (default: 0)
  --seed: Random seed for reproducibility (default: 42)
  --kHop: k-hop neighborhood for recovery (default: 3)
  --alpha: Swap failure rate constant (default: 0.1)
*/

#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/quantum-topology-helper.h"
#include "ns3/distribute-epr-helper.h"
#include "ns3/quantum-net-stack-helper.h"

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <ctime>

NS_LOG_COMPONENT_DEFINE ("QCastRandomTopologyExample");

using namespace ns3;

/**
 * \brief Structure to hold request results with fidelity and delay measurement
 */
struct RequestResult
{
    uint32_t requestId;
    std::string srcNode;
    std::string dstNode;
    std::vector<std::string> route;
    double expectedThroughput;
    double endToEndFidelity;
    Time totalDelay;              // Total classical network delay along path
    Time avgLinkDelay;            // Average delay per link
    Time maxLinkDelay;            // Maximum link delay
    Time minLinkDelay;            // Minimum link delay
    uint32_t numHops;
    uint32_t numRecoveryPaths;
    bool success;
    std::string failureReason;
    std::string phaseLog;  // physical layer operation log for this request
};

/**
 * \brief Application that runs Q-CAST on random topology and measures fidelity and delay
 */
class QCastRandomTopologyApp : public Application
{
public:
    // Type alias for link properties map
    using LinkPropsMap = std::map<std::pair<std::string, std::string>, std::pair<double, double>>;

    QCastRandomTopologyApp (Ptr<QuantumPhyEntity> qphyent,
                            const std::vector<Ptr<QuantumNetworkLayer>> &netLayers,
                            const std::vector<Ptr<QCastRoutingProtocol>> &routingProtocols,
                            Ptr<BurstDelayModel> delayModel,
                            const LinkPropsMap &linkProps)
        : m_qphyent (qphyent),
          m_netLayers (netLayers),
          m_routingProtocols (routingProtocols),
          m_delayModel (delayModel),
          m_linkProps (linkProps),
          m_numRequests (3)
    {
    }

    void SetNumRequests (uint32_t numRequests)
    {
        m_numRequests = numRequests;
    }

    void SetSeed (uint32_t seed)
    {
        m_rngSeed = seed;
    }

    void StartApplication (void) override
    {
        StartSimulation ();
    }

    void StopApplication (void) override {}

    void StartSimulation (void)
    {
        NS_LOG_INFO ("Starting Q-CAST Random Topology Simulation");
        
        // Generate random requests
        std::vector<QCastRequest> requests = GenerateRandomRequests ();
        
        NS_LOG_INFO ("Generated " << requests.size () << " random requests:");
        for (const auto &req : requests)
        {
            NS_LOG_INFO ("  Request " << req.requestId << ": " << req.srcNode << " -> " 
                         << req.dstNode);
        }
        
        // Run Q-CAST using the first node's routing protocol
        // In a real distributed implementation, this would be coordinated
        Ptr<QCastRoutingProtocol> qcast = m_routingProtocols[0];
        auto paths = qcast->CalculateRoutesGEDA (requests);
        
        NS_LOG_INFO ("\n=== Q-CAST Results ===");
        NS_LOG_INFO ("Selected " << paths.size () << " paths out of " << requests.size () 
                     << " requests");
        
        // Process results and measure fidelity
        std::vector<RequestResult> results;
        
        for (const auto &entry : paths)
        {
            uint32_t pathId = entry.first;
            const QCastPath &path = entry.second;
            
            // Find corresponding request
            QCastRequest req;
            for (const auto &r : requests)
            {
                if (r.srcNode == path.primaryPath.front () && 
                    r.dstNode == path.primaryPath.back ())
                {
                    req = r;
                    break;
                }
            }
            
            RequestResult result;
            result.requestId = req.requestId;
            result.srcNode = req.srcNode;
            result.dstNode = req.dstNode;
            result.route = path.primaryPath;
            result.expectedThroughput = path.primaryEt;
            result.numHops = static_cast<uint32_t> (path.primaryPath.size () - 1);
            result.numRecoveryPaths = static_cast<uint32_t> (path.recoveryPaths.size ());
            result.success = true;
            
            // Calculate end-to-end fidelity
            result.endToEndFidelity = CalculateEndToEndFidelity (path);
            result.phaseLog = m_physLogStream.str ();  // capture physical layer log
            
            // Calculate delay statistics along the path
            Time totalDelay = MilliSeconds (0);
            Time maxDelay = MilliSeconds (0);
            Time minDelay = MilliSeconds (1000000);  // Large initial value
            
            if (m_delayModel && path.primaryPath.size () > 1)
            {
                for (size_t i = 0; i < path.primaryPath.size () - 1; ++i)
                {
                    // Get current delay from delay model
                    // In real scenario, each link would have its own delay model
                    // Here we use the global delay model for demonstration
                    Time linkDelay = m_delayModel->GetCurrentDelay ();
                    totalDelay += linkDelay;
                    
                    if (linkDelay > maxDelay)
                        maxDelay = linkDelay;
                    if (linkDelay < minDelay)
                        minDelay = linkDelay;
                }
                
                result.totalDelay = totalDelay;
                result.maxLinkDelay = maxDelay;
                result.minLinkDelay = minDelay;
                result.avgLinkDelay = totalDelay / (path.primaryPath.size () - 1);
            }
            else
            {
                result.totalDelay = MilliSeconds (10 * (path.primaryPath.size () - 1));
                result.avgLinkDelay = MilliSeconds (10);
                result.maxLinkDelay = MilliSeconds (10);
                result.minLinkDelay = MilliSeconds (10);
            }
            
            results.push_back (result);
            
            // Print results
            NS_LOG_INFO ("\n--- Request " << result.requestId << " ---");
            NS_LOG_INFO ("Source: " << result.srcNode);
            NS_LOG_INFO ("Destination: " << result.dstNode);
            NS_LOG_INFO ("Route: " << qcast->RouteToString (result.route));
            NS_LOG_INFO ("Hops: " << result.numHops);
            NS_LOG_INFO ("Expected Throughput: " << result.expectedThroughput);
            NS_LOG_INFO ("End-to-End Fidelity: " << result.endToEndFidelity);
            NS_LOG_INFO ("Total Classical Delay: " << result.totalDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("Avg Link Delay: " << result.avgLinkDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("Recovery Paths: " << result.numRecoveryPaths);
            
            // Generate and display swap schedule
            auto schedule = qcast->GenerateSwapSchedule (path.primaryPath);
            NS_LOG_INFO ("Swap Schedule Rounds: " << schedule.size ());
        }
        
        // Print summary statistics
        PrintStatistics (results);
        
        // Store results for later access
        m_results = results;
    }

    std::vector<RequestResult> GetResults (void) const
    {
        return m_results;
    }

private:
    std::vector<QCastRequest> GenerateRandomRequests (void)
    {
        std::vector<QCastRequest> requests;
        
        uint32_t numNodes = static_cast<uint32_t> (m_netLayers.size ());
        std::mt19937 rng (m_rngSeed); // per-run seed for reproducibility
        std::uniform_int_distribution<uint32_t> dist (0, numNodes - 1);
        
        for (uint32_t i = 0; i < m_numRequests; ++i)
        {
            QCastRequest req;
            req.requestId = i + 1;
            
            // Select random source and destination (ensure they're different)
            uint32_t srcIdx, dstIdx;
            do
            {
                srcIdx = dist (rng);
                dstIdx = dist (rng);
            } while (srcIdx == dstIdx);
            
            req.srcNode = "Node" + std::to_string (srcIdx);
            req.dstNode = "Node" + std::to_string (dstIdx);
            req.minFidelity = 0.85;
            
            requests.push_back (req);
        }
        
        return requests;
    }

    double CalculateEndToEndFidelity (const QCastPath &path)
    {
        if (path.primaryPath.size () < 2)
        {
            return 0.0;
        }

        // Use physical layer to simulate actual entanglement distribution + swapping
        return CalculatePhysicalFidelity (path);
    }

    // Dual-output logger for physical layer steps: NS_LOG + captured stream (phaseLog)
    void Phys (const std::string &msg)
    {
        NS_LOG_INFO (msg);
        m_physLogStream << msg << "\n";
    }

    double CalculatePhysicalFidelity (const QCastPath &path)
    {
        const auto &nodes = path.primaryPath;
        uint32_t numHops = static_cast<uint32_t> (nodes.size () - 1);

        if (numHops == 0)
            return 0.0;

        // Reset per-request physical log
        m_physLogStream.str ("");
        m_physLogStream.clear ();

        // Unique qubit name prefix per request to avoid name collisions
        static uint32_t s_reqCounter = 0;
        s_reqCounter++;
        std::string pfx = "r" + std::to_string (s_reqCounter) + "_";

        // linkQubits[i] = {qubit at nodes[i], qubit at nodes[i+1]}
        std::vector<std::pair<std::string, std::string>> linkQubits (numHops);

        // ── Step 1: generate EPR pairs on each link + apply depolar noise ──────
        for (uint32_t i = 0; i < numHops; ++i)
        {
            std::string qa = pfx + nodes[i] + "_" + nodes[i + 1] + "_a";
            std::string qb = pfx + nodes[i] + "_" + nodes[i + 1] + "_b";
            linkQubits[i] = {qa, qb};

            // Perfect Bell state
            m_qphyent->GenerateQubitsPure ("God", q_bell, {qa, qb});

            // Look up link fidelity from topology
            double fidelity = 0.95; // safe default
            auto it = m_linkProps.find ({nodes[i], nodes[i + 1]});
            if (it == m_linkProps.end ())
                it = m_linkProps.find ({nodes[i + 1], nodes[i]});
            if (it != m_linkProps.end ())
                fidelity = it->second.first;

            // Apply depolarizing noise matching the link fidelity
            m_qphyent->SetDepolarModel ({nodes[i], nodes[i + 1]}, fidelity);
            m_qphyent->ApplyErrorModel ({nodes[i], nodes[i + 1]}, {qa, qb});

            {
                std::ostringstream _s;
                _s << "  [Phys] Link " << nodes[i] << "->" << nodes[i + 1]
                   << "  link-fidelity=" << std::fixed << std::setprecision (4)
                   << fidelity << "  qubits=(" << qa << ", " << qb << ")";
                Phys (_s.str ());
            }
        }

        // ── Step 2: entanglement swapping at each intermediate node ───────────
        for (uint32_t hop = 1; hop < nodes.size () - 1; ++hop)
        {
            // At nodes[hop]: holds linkQubits[hop-1].second (arrived from left)
            //                and  linkQubits[hop].first   (to be sent right)
            std::string q_left  = linkQubits[hop - 1].second; // qubit from left link
            std::string q_right = linkQubits[hop].first;      // qubit of right link
            std::string q_far   = linkQubits[hop].second;     // far-end qubit (to correct)

            {
                std::ostringstream _s;
                _s << "  [Phys] Swap at " << nodes[hop]
                   << ": Bell-meas (" << q_right << ", " << q_left << ")";
                Phys (_s.str ());
            }

            // Bell measurement: CNOT(control=q_right, target=q_left) then H(q_left)
            m_qphyent->ApplyGate ("God", QNS_GATE_PREFIX + "CNOT", {}, {q_right, q_left});
            m_qphyent->ApplyGate ("God", QNS_GATE_PREFIX + "H",    {}, {q_left});

            auto [m_l, _pl] = m_qphyent->Measure ("God", {q_left});
            auto [m_r, _pr] = m_qphyent->Measure ("God", {q_right});

            {
                std::ostringstream _s;
                _s << "  [Phys]   outcome left=" << m_l << " right=" << m_r;
                Phys (_s.str ());
            }

            // Classical corrections on far-end qubit
            if (m_r == 1)
                m_qphyent->ApplyGate ("God", QNS_GATE_PREFIX + "PX", {}, {q_far});
            if (m_l == 1)
                m_qphyent->ApplyGate ("God", QNS_GATE_PREFIX + "PZ", {}, {q_far});

            // Trace out the two measured intermediate qubits
            m_qphyent->PartialTrace ({q_left, q_right});
        }

        // ── Step 3: measure end-to-end fidelity ───────────────────────────────
        std::string q_src = linkQubits[0].first;
        std::string q_dst = linkQubits[numHops - 1].second;
        double fidel = 0.0;
        m_qphyent->CalculateFidelity ({q_src, q_dst}, fidel);

        {
            std::ostringstream _s;
            _s << "  [Phys] End-to-end fidelity " << nodes.front ()
               << "->" << nodes.back () << " = " << std::fixed << std::setprecision (4) << fidel;
            Phys (_s.str ());
        }

        // Cleanup: trace out endpoint qubits so they don't pollute next request
        m_qphyent->PartialTrace ({q_src, q_dst});

        return fidel;
    }

    void PrintStatistics (const std::vector<RequestResult> &results)
    {
        NS_LOG_INFO ("\n=== Summary Statistics ===");
        
        if (results.empty ())
        {
            NS_LOG_INFO ("No successful requests");
            return;
        }
        
        uint32_t successful = 0;
        uint32_t failed = 0;
        double totalFidelity = 0.0;
        double totalThroughput = 0.0;
        uint32_t totalHops = 0;
        uint32_t totalRecoveryPaths = 0;
        double totalDelay = 0.0;
        double totalAvgLinkDelay = 0.0;
        double minTotalDelay = std::numeric_limits<double>::max ();
        double maxTotalDelay = 0.0;
        
        for (const auto &result : results)
        {
            if (result.success)
            {
                successful++;
                totalFidelity += result.endToEndFidelity;
                totalThroughput += result.expectedThroughput;
                totalHops += result.numHops;
                totalRecoveryPaths += result.numRecoveryPaths;
                totalDelay += result.totalDelay.GetMilliSeconds ();
                totalAvgLinkDelay += result.avgLinkDelay.GetMilliSeconds ();
                minTotalDelay = std::min (minTotalDelay, static_cast<double> (result.totalDelay.GetMilliSeconds ()));
                maxTotalDelay = std::max (maxTotalDelay, static_cast<double> (result.totalDelay.GetMilliSeconds ()));
            }
            else
            {
                failed++;
            }
        }
        
        NS_LOG_INFO ("Total Requests: " << results.size ());
        NS_LOG_INFO ("Successful: " << successful);
        NS_LOG_INFO ("Failed: " << failed);
        
        if (successful > 0)
        {
            NS_LOG_INFO ("\n--- Quantum Performance ---");
            NS_LOG_INFO ("Average Fidelity: " << (totalFidelity / successful));
            NS_LOG_INFO ("Average Throughput: " << (totalThroughput / successful));
            NS_LOG_INFO ("Average Hops: " << (static_cast<double> (totalHops) / successful));
            NS_LOG_INFO ("Average Recovery Paths: " << (static_cast<double> (totalRecoveryPaths) / successful));
            
            NS_LOG_INFO ("\n--- Classical Network Delay ---");
            NS_LOG_INFO ("Average Total Delay: " << (totalDelay / successful) << " ms");
            NS_LOG_INFO ("Average Link Delay: " << (totalAvgLinkDelay / successful) << " ms");
            NS_LOG_INFO ("Min Total Delay: " << minTotalDelay << " ms");
            NS_LOG_INFO ("Max Total Delay: " << maxTotalDelay << " ms");
            NS_LOG_INFO ("Delay Variance: " << (maxTotalDelay - minTotalDelay) << " ms");
        }
        
        // Find best and worst paths by fidelity
        auto bestFidelityIt = std::max_element (results.begin (), results.end (),
                                        [] (const RequestResult &a, const RequestResult &b)
                                        {
                                            return a.endToEndFidelity < b.endToEndFidelity;
                                        });
        
        auto worstFidelityIt = std::min_element (results.begin (), results.end (),
                                         [] (const RequestResult &a, const RequestResult &b)
                                         {
                                             return a.endToEndFidelity < b.endToEndFidelity;
                                         });
        
        if (bestFidelityIt != results.end ())
        {
            NS_LOG_INFO ("\nBest Fidelity Path:");
            NS_LOG_INFO ("  Request " << bestFidelityIt->requestId << ": " << bestFidelityIt->srcNode 
                         << " -> " << bestFidelityIt->dstNode);
            NS_LOG_INFO ("  Fidelity: " << bestFidelityIt->endToEndFidelity);
            NS_LOG_INFO ("  Total Delay: " << bestFidelityIt->totalDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("  Hops: " << bestFidelityIt->numHops);
        }
        
        if (worstFidelityIt != results.end () && results.size () > 1)
        {
            NS_LOG_INFO ("\nWorst Fidelity Path:");
            NS_LOG_INFO ("  Request " << worstFidelityIt->requestId << ": " << worstFidelityIt->srcNode 
                         << " -> " << worstFidelityIt->dstNode);
            NS_LOG_INFO ("  Fidelity: " << worstFidelityIt->endToEndFidelity);
            NS_LOG_INFO ("  Total Delay: " << worstFidelityIt->totalDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("  Hops: " << worstFidelityIt->numHops);
        }
        
        // Find best and worst paths by delay
        auto bestDelayIt = std::min_element (results.begin (), results.end (),
                                        [] (const RequestResult &a, const RequestResult &b)
                                        {
                                            return a.totalDelay < b.totalDelay;
                                        });
        
        auto worstDelayIt = std::max_element (results.begin (), results.end (),
                                         [] (const RequestResult &a, const RequestResult &b)
                                         {
                                             return a.totalDelay < b.totalDelay;
                                         });
        
        if (bestDelayIt != results.end ())
        {
            NS_LOG_INFO ("\nLowest Delay Path:");
            NS_LOG_INFO ("  Request " << bestDelayIt->requestId << ": " << bestDelayIt->srcNode 
                         << " -> " << bestDelayIt->dstNode);
            NS_LOG_INFO ("  Total Delay: " << bestDelayIt->totalDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("  Fidelity: " << bestDelayIt->endToEndFidelity);
            NS_LOG_INFO ("  Hops: " << bestDelayIt->numHops);
        }
        
        if (worstDelayIt != results.end () && results.size () > 1)
        {
            NS_LOG_INFO ("\nHighest Delay Path:");
            NS_LOG_INFO ("  Request " << worstDelayIt->requestId << ": " << worstDelayIt->srcNode 
                         << " -> " << worstDelayIt->dstNode);
            NS_LOG_INFO ("  Total Delay: " << worstDelayIt->totalDelay.GetMilliSeconds () << " ms");
            NS_LOG_INFO ("  Fidelity: " << worstDelayIt->endToEndFidelity);
            NS_LOG_INFO ("  Hops: " << worstDelayIt->numHops);
        }
    }

    Ptr<QuantumPhyEntity> m_qphyent;
    std::vector<Ptr<QuantumNetworkLayer>> m_netLayers;
    std::vector<Ptr<QCastRoutingProtocol>> m_routingProtocols;
    Ptr<BurstDelayModel> m_delayModel;
    LinkPropsMap m_linkProps;     // link fidelity / success_rate from topology helper
    uint32_t m_numRequests{3};
    uint32_t m_rngSeed{42};              // per-run seed for request generation
    std::ostringstream m_physLogStream;  // captures per-request physical layer log
    std::vector<RequestResult> m_results;
};

// ═════════════════════════════════════════════════════════════════════════════
// Helper: route vector → "A -> B -> C" string
// ═════════════════════════════════════════════════════════════════════════════
static std::string
RouteToStr (const std::vector<std::string> &route)
{
    std::string s;
    for (size_t i = 0; i < route.size (); ++i)
    {
        if (i) s += " -> ";
        s += route[i];
    }
    return s;
}

// ═════════════════════════════════════════════════════════════════════════════
// WriteRunFiles – produce all output files for one simulation run
// ═════════════════════════════════════════════════════════════════════════════
static void
WriteRunFiles (uint32_t runId, uint32_t seed,
               uint32_t numNodes, uint32_t numRequests,
               uint32_t topologyType, uint32_t kHop, double alpha, double edgeProb,
               double tCoherence,
               const std::vector<std::pair<std::string, std::string>> &edges,
               const std::map<std::pair<std::string, std::string>,
                              std::pair<double, double>> &linkProps,
               const std::vector<RequestResult> &results,
               Ptr<BurstDelayModel> delayModel,
               const std::string &outDir)
{
    const char *tname[] = {"RandomGeometric", "ErdosRenyi", "ScaleFree"};
    const char *tn = tname[topologyType < 3 ? topologyType : 0];

    // ── parameters.txt ───────────────────────────────────────────────────────
    {
        std::ofstream f (outDir + "/parameters.txt");
        f << "=== Q-CAST Simulation Parameters ===\n"
          << "Run ID             : " << runId      << "\n"
          << "Seed               : " << seed       << "\n"
          << "Nodes              : " << numNodes   << "\n"
          << "Edges              : " << edges.size () << "\n"
          << "Requests           : " << numRequests << "\n"
          << "Topology Type      : " << tn         << "\n"
          << "Edge Probability   : " << edgeProb   << "\n"
          << "k-Hop              : " << kHop       << "\n"
          << "Alpha              : " << alpha      << "\n"
          << "Coherence Time     : " << tCoherence << " ms\n"
          << "Node Capacity      : 5\n"
          << "Max Hops           : 10\n"
          << "\n=== Delay Model Parameters ===\n"
          << "Type               : BurstDelayModel\n"
          << "Base Delay         : 10 ms\n"
          << "Max Deviation      : +/-20 ms\n"
          << "Update Interval    : 100 ms\n"
          << "Burst Probability  : 8% per update\n"
          << "Burst Duration     : ~600 ms\n"
          << "Burst Multiplier   : 1.8x\n";
    }

    // ── topology.csv ─────────────────────────────────────────────────────────
    {
        std::ofstream f (outDir + "/topology.csv");
        f << "# Q-CAST Topology  Run=" << runId << "  Seed=" << seed << "\n"
          << "\n# [NODES]\nname\n";
        for (uint32_t i = 0; i < numNodes; ++i)
            f << "Node" << i << "\n";
        f << "\n# [EDGES]\nnodeA,nodeB,fidelity,success_rate\n";
        for (const auto &e : edges)
        {
            auto it = linkProps.find (e);
            if (it != linkProps.end ())
                f << e.first << "," << e.second << ","
                  << std::fixed << std::setprecision (6)
                  << it->second.first << "," << it->second.second << "\n";
        }
    }

    // ── delay_log.csv ────────────────────────────────────────────────────────
    // Every row is one BurstDelayModel update event (every 100 ms)
    {
        std::ofstream f (outDir + "/delay_log.csv");
        f << "# Burst Delay Change Log  Run=" << runId << "  Seed=" << seed << "\n"
          << "timestamp_s,delay_ms,is_burst\n";
        auto hist = delayModel->GetDelayHistory ();
        for (const auto &h : hist)
            f << std::fixed << std::setprecision (3)
              << h.timestamp.GetSeconds () << ","
              << h.delay.GetMilliSeconds () << ","
              << (h.isBurst ? 1 : 0) << "\n";
        if (!hist.empty ())
        {
            auto [bc, bd] = delayModel->GetBurstStatistics ();
            f << "\n# --- Summary ---\n"
              << "# avg_delay_ms,"   << delayModel->GetAverageDelay ().GetMilliSeconds () << "\n"
              << "# min_delay_ms,"   << delayModel->GetMinDelay ().GetMilliSeconds ()     << "\n"
              << "# max_delay_ms,"   << delayModel->GetMaxDelay ().GetMilliSeconds ()     << "\n"
              << "# burst_count,"    << bc                                                << "\n"
              << "# total_burst_ms," << bd.GetMilliSeconds ()                             << "\n";
        }
    }

    // ── results.csv ──────────────────────────────────────────────────────────
    {
        std::ofstream f (outDir + "/results.csv");
        f << "# Q-CAST Request Results  Run=" << runId << "  Seed=" << seed << "\n"
          << "request_id,src,dst,route,hops,expected_throughput,fidelity,"
          << "total_delay_ms,avg_link_delay_ms,min_link_delay_ms,max_link_delay_ms,"
          << "recovery_paths,swap_rounds,success\n";
        for (const auto &r : results)
        {
            uint32_t sr = r.numHops <= 1 ? 0
                        : static_cast<uint32_t> (std::ceil (std::log2 (static_cast<double>(r.numHops))));
            f << r.requestId << ","
              << r.srcNode << "," << r.dstNode << ","
              << RouteToStr (r.route) << ","
              << r.numHops << ","
              << std::fixed << std::setprecision (6)
              << r.expectedThroughput << "," << r.endToEndFidelity << ","
              << r.totalDelay.GetMilliSeconds () << ","
              << r.avgLinkDelay.GetMilliSeconds () << ","
              << r.minLinkDelay.GetMilliSeconds () << ","
              << r.maxLinkDelay.GetMilliSeconds () << ","
              << r.numRecoveryPaths << "," << sr << ","
              << (r.success ? "true" : "false") << "\n";
        }
    }

    // ── sim_report.txt ───────────────────────────────────────────────────────
    {
        std::ofstream f (outDir + "/sim_report.txt");
        f << "=======================================================\n"
          << " Q-CAST Simulation Report  Run " << runId << "  Seed " << seed << "\n"
          << "=======================================================\n\n"
          << "=== Configuration ===\n"
          << "Run ID            : " << runId       << "\n"
          << "Seed              : " << seed        << "\n"
          << "Nodes             : " << numNodes    << "\n"
          << "Edges             : " << edges.size () << "\n"
          << "Requests          : " << numRequests  << "\n"
          << "Topology Type     : " << tn          << "\n"
          << "Edge Probability  : " << edgeProb    << "\n"
          << "k-Hop             : " << kHop        << "\n"
          << "Alpha             : " << alpha       << "\n"
          << "Coherence Time    : " << tCoherence  << " ms\n\n";

        // Per-topology statistics
        std::map<std::string, int> deg;
        double sumF = 0, sumS = 0;
        for (const auto &e : edges)
        {
            deg[e.first]++; deg[e.second]++;
            auto it = linkProps.find (e);
            if (it != linkProps.end ()) { sumF += it->second.first; sumS += it->second.second; }
        }
        int maxD = 0; double sumD = 0;
        for (auto &d : deg) { sumD += d.second; maxD = std::max (maxD, d.second); }
        f << "=== Topology ===\n"
          << std::fixed << std::setprecision (3)
          << "Nodes=" << numNodes << "  Edges=" << edges.size () << "\n"
          << "Avg Degree=" << (edges.empty () ? 0.0 : sumD / numNodes)
          << "  Max Degree=" << maxD << "\n"
          << "Avg Link Fidelity=" << (edges.empty () ? 0.0 : sumF / edges.size ())
          << "  Avg Link SuccessRate=" << (edges.empty () ? 0.0 : sumS / edges.size ()) << "\n\n";

        f << "=== Delay Model ===\n";
        auto hist = delayModel->GetDelayHistory ();
        if (!hist.empty ())
        {
            auto [bc, bd] = delayModel->GetBurstStatistics ();
            f << "History records : " << hist.size () << "\n"
              << "Avg Delay       : " << delayModel->GetAverageDelay ().GetMilliSeconds () << " ms\n"
              << "Min Delay       : " << delayModel->GetMinDelay ().GetMilliSeconds ()     << " ms\n"
              << "Max Delay       : " << delayModel->GetMaxDelay ().GetMilliSeconds ()     << " ms\n"
              << "Burst Events    : " << bc << "\n"
              << "Total Burst Dur : " << bd.GetMilliSeconds ()                             << " ms\n";
        }
        f << "\n";

        f << "=== Q-CAST Results ===\n"
          << "Selected " << results.size () << " paths out of " << numRequests << " requests\n\n";
        for (const auto &r : results)
        {
            uint32_t sr = r.numHops <= 1 ? 0
                        : static_cast<uint32_t> (std::ceil (std::log2 (static_cast<double>(r.numHops))));
            f << "--- Request " << r.requestId << " ---\n"
              << "Source      : " << r.srcNode << "  Destination: " << r.dstNode << "\n"
              << "Route       : " << RouteToStr (r.route) << "\n"
              << "Hops        : " << r.numHops << "\n"
              << "E_t         : " << std::fixed << std::setprecision (6) << r.expectedThroughput << "\n"
              << "Fidelity    : " << r.endToEndFidelity << "\n"
              << "Total Delay : " << r.totalDelay.GetMilliSeconds () << " ms"
              << "  (avg " << r.avgLinkDelay.GetMilliSeconds ()
              << " ms / link, min " << r.minLinkDelay.GetMilliSeconds ()
              << " ms, max " << r.maxLinkDelay.GetMilliSeconds () << " ms)\n"
              << "Recovery Paths     : " << r.numRecoveryPaths << "\n"
              << "Swap Schedule Rounds: " << sr << "\n";
            if (!r.phaseLog.empty ())
                f << "Physical Layer Log:\n" << r.phaseLog;
            f << "\n";
        }

        if (!results.empty ())
        {
            double sf = 0, st = 0, sd = 0; uint32_t sh = 0, srp = 0;
            double minD = 1e9, maxD2 = 0;
            const RequestResult *bestF  = &results[0], *worstF = &results[0];
            const RequestResult *bestD  = &results[0], *worstD = &results[0];
            for (const auto &r : results)
            {
                sf += r.endToEndFidelity; st += r.expectedThroughput;
                sd += r.totalDelay.GetMilliSeconds ();
                sh += r.numHops; srp += r.numRecoveryPaths;
                double d = r.totalDelay.GetMilliSeconds ();
                if (d < minD) { minD = d; bestD  = &r; }
                if (d > maxD2){ maxD2 = d; worstD = &r; }
                if (r.endToEndFidelity > bestF->endToEndFidelity)  bestF  = &r;
                if (r.endToEndFidelity < worstF->endToEndFidelity) worstF = &r;
            }
            size_t n = results.size ();
            f << "=== Summary ===\n"
              << "Paths Found    : " << n << " / " << numRequests
              << "  (" << std::fixed << std::setprecision (1) << (100.0 * n / numRequests) << "%)\n\n"
              << "Quantum Performance:\n"
              << std::setprecision (6)
              << "  Avg Fidelity        : " << sf / n << "\n"
              << "  Avg Throughput      : " << st / n << "\n"
              << "  Avg Hops            : " << std::setprecision (2) << (double)sh / n << "\n"
              << "  Avg Recovery Paths  : " << (double)srp / n << "\n\n"
              << "Classical Delay:\n"
              << std::setprecision (1)
              << "  Avg Total Delay : " << sd / n   << " ms\n"
              << "  Min Total Delay : " << minD      << " ms\n"
              << "  Max Total Delay : " << maxD2     << " ms\n\n"
              << "Best  Fidelity: Req " << bestF->requestId
              << " (" << bestF->srcNode << "->" << bestF->dstNode << ")"
              << "  F=" << std::setprecision (6) << bestF->endToEndFidelity
              << "  Hops=" << bestF->numHops << "\n"
              << "Worst Fidelity: Req " << worstF->requestId
              << " (" << worstF->srcNode << "->" << worstF->dstNode << ")"
              << "  F=" << worstF->endToEndFidelity
              << "  Hops=" << worstF->numHops << "\n"
              << "Best  Delay   : Req " << bestD->requestId
              << "  D=" << std::setprecision (1) << minD  << " ms\n"
              << "Worst Delay   : Req " << worstD->requestId
              << "  D=" << maxD2 << " ms\n";
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// RunOneSimulation – complete lifecycle for one simulation run
// ═════════════════════════════════════════════════════════════════════════════
static void
RunOneSimulation (uint32_t runId, uint32_t seed,
                  uint32_t numNodes, uint32_t numRequests,
                  uint32_t topologyType, uint32_t kHop,
                  double alpha, double edgeProb, double avgDegree,
                  double tCoherence,
                  const std::string &baseOutDir,
                  std::ofstream &summaryFile)
{
    std::ostringstream dirOss;
    dirOss << baseOutDir << "/run_" << std::setw (3) << std::setfill ('0') << runId;
    std::string outDir = dirOss.str ();
    system (("mkdir -p " + outDir).c_str ());

    const char *tname[] = {"RandomGeo", "ErdosRenyi", "ScaleFree"};
    printf ("\n[Run %u/%s  seed=%u  nodes=%u  requests=%u  tCoh=%.1fms]\n",
            runId, tname[topologyType < 3 ? topologyType : 0], seed, numNodes, numRequests, tCoherence);

    // ── Step 1: topology helper ────────────────────────────────────────────
    Ptr<QuantumTopologyHelper> topoHelper = CreateObject<QuantumTopologyHelper> ();
    topoHelper->SetNumNodes (numNodes);
    topoHelper->SetRandomSeed (seed);
    topoHelper->SetTopologyType (
        static_cast<QuantumTopologyHelper::TopologyType> (topologyType));
    if (topologyType == 0)      topoHelper->SetAverageDegree (avgDegree);
    else if (topologyType == 1) topoHelper->SetEdgeProbability (edgeProb);
    // Slightly noisier links than default to make recovery paths matter more
    topoHelper->SetLinkQualityRange (0.92, 0.99, 0.90, 0.97);

    // ── Step 2: physical entity ────────────────────────────────────────────
    std::vector<std::string> owners;
    for (uint32_t i = 0; i < numNodes; ++i)
        owners.push_back ("Node" + std::to_string (i));
    owners.push_back ("God");
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);

    // Set coherence time for all nodes
    for (const auto &owner : owners)
    {
        if (owner != "God")
            qphyent->SetTimeModel (owner, tCoherence / 1000.0);
    }

    // ── Step 3: generate topology & edges ─────────────────────────────────
    NodeContainer nodes = topoHelper->GenerateTopology (qphyent);
    std::vector<std::pair<std::string, std::string>> edges = topoHelper->GetEdges ();
    auto linkProps = topoHelper->GetLinkProperties ();

    NS_LOG_INFO ("Topology: nodes=" << numNodes << "  edges=" << edges.size ());
    topoHelper->PrintStatistics ();

    // ── Step 4: classical network stack ───────────────────────────────────
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("1000kbps")));
    csmaHelper.SetChannelAttribute ("Delay",    TimeValue (MilliSeconds (10)));
    NetDeviceContainer devices = csmaHelper.Install (nodes);

    InternetStackHelper stack;
    stack.Install (nodes);
    Ipv6AddressHelper address;
    address.SetBase ("2001:1::", Ipv6Prefix (64));
    Ipv6InterfaceContainer interfaces = address.Assign (devices);
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        qphyent->SetOwnerAddress ("Node" + std::to_string (i), interfaces.GetAddress (i, 1));
        qphyent->SetOwnerRank   ("Node" + std::to_string (i), i);
    }

    // ── Step 5: quantum routing ────────────────────────────────────────────
    QuantumNetStackHelper qstack;
    qstack.Install (nodes);

    std::vector<Ptr<QuantumNetworkLayer>>   netLayers;
    std::vector<Ptr<QCastRoutingProtocol>>  routingProtocols;

    for (uint32_t i = 0; i < numNodes; ++i)
    {
        std::string nodeName = "Node" + std::to_string (i);

        Ptr<QuantumNetworkLayer>  netLayer    = CreateObject<QuantumNetworkLayer> ();
        Ptr<QCastRoutingProtocol> qcastRouting= CreateObject<QCastRoutingProtocol> ();
        netLayer->SetOwner (nodeName);
        netLayer->SetPhyEntity (qphyent);
        qcastRouting->SetKHop (kHop);
        qcastRouting->SetAlpha (alpha);
        qcastRouting->SetNodeCapacity (5);
        netLayer->SetRoutingProtocol (qcastRouting);

        for (const auto &edge : edges)
        {
            auto propIt = linkProps.find (edge);
            if (propIt == linkProps.end ()) continue;
            double fid = propIt->second.first;
            double sr  = propIt->second.second;
            if (edge.first == nodeName)
            {
                Ptr<QuantumChannel> ch = CreateObject<QuantumChannel> (edge.first, edge.second);
                netLayer->AddNeighbor (edge.second, ch, fid, sr);
            }
            else if (edge.second == nodeName)
            {
                Ptr<QuantumChannel> ch = CreateObject<QuantumChannel> (edge.second, edge.first);
                netLayer->AddNeighbor (edge.first, ch, fid, sr);
            }
        }
        netLayer->Initialize ();
        netLayers.push_back (netLayer);
        routingProtocols.push_back (qcastRouting);
    }

    // Step 5b: inject full global topology for multi-hop Dijkstra
    {
        std::map<std::string, std::map<std::string, LinkMetrics>> fullTopo;
        for (const auto &edge : edges)
        {
            auto it = linkProps.find (edge);
            if (it == linkProps.end ()) continue;
            LinkMetrics m;
            m.fidelity    = it->second.first;
            m.successRate = it->second.second;
            m.latency     = 10.0;
            m.isAvailable = true;
            fullTopo[edge.first][edge.second] = m;
            fullTopo[edge.second][edge.first] = m;
        }
        routingProtocols[0]->UpdateTopology (fullTopo);
        NS_LOG_INFO ("Global topology injected: " << edges.size () << " edges");
    }

    // ── Step 6: burst delay model (5-second simulation) ────────────────────
    Ptr<BurstDelayModel> burstDelayModel = CreateObject<BurstDelayModel> ();
    burstDelayModel->SetBaseDelay      (MilliSeconds (10));
    burstDelayModel->SetMaxDeviation   (MilliSeconds (20));
    burstDelayModel->SetUpdateInterval (MilliSeconds (100));
    burstDelayModel->SetBurstProbability (0.08);     // 8% per 100ms = moderate bursts
    burstDelayModel->SetBurstDuration  (MilliSeconds (600));
    burstDelayModel->SetBurstMultiplier (1.8);       // 1.8x delay during burst
    burstDelayModel->Initialize (MilliSeconds (100));

    // ── Step 7: application ────────────────────────────────────────────────
    Ptr<QCastRandomTopologyApp> app = CreateObject<QCastRandomTopologyApp> (
        qphyent, netLayers, routingProtocols, burstDelayModel, linkProps);
    app->SetNumRequests (numRequests);
    app->SetSeed (seed);
    nodes.Get (0)->AddApplication (app);
    app->SetStartTime (Seconds (0.5));

    Simulator::Stop (Seconds (5.0));   // 5 s → ~50 delay history records

    auto t0 = std::chrono::high_resolution_clock::now ();
    Simulator::Run ();
    auto t1 = std::chrono::high_resolution_clock::now ();
    long wallMs = std::chrono::duration_cast<std::chrono::milliseconds> (t1 - t0).count ();

    auto results = app->GetResults ();
    printf ("  -> %zu paths found  (wall time: %ld ms)\n", results.size (), wallMs);

    // ── Step 8: write per-run files ────────────────────────────────────────
    WriteRunFiles (runId, seed, numNodes, numRequests, topologyType, kHop, alpha, edgeProb,
                   tCoherence,
                   edges, linkProps, results, burstDelayModel, outDir);

    // ── Step 9: append one row to cross-run summary ────────────────────────
    {
        double sf = 0, st = 0, sd = 0; uint32_t sh = 0, srp = 0;
        for (const auto &r : results)
        {
            sf += r.endToEndFidelity; st += r.expectedThroughput;
            sd += r.totalDelay.GetMilliSeconds ();
            sh += r.numHops; srp += r.numRecoveryPaths;
        }
        size_t n = results.size ();
        auto [bc, bd] = burstDelayModel->GetBurstStatistics ();
        summaryFile << runId << "," << seed << "," << numNodes << ","
                    << edges.size () << "," << numRequests << ","
                    << n << ","
                    << std::fixed << std::setprecision (2)
                    << (n ? 100.0 * n / numRequests : 0.0) << ","
                    << std::setprecision (6)
                    << (n ? sf / n : 0) << ","
                    << (n ? st / n : 0) << ","
                    << std::setprecision (2)
                    << (n ? (double)sh  / n : 0) << ","
                    << (n ? (double)srp / n : 0) << ","
                    << std::setprecision (1)
                    << (n ? sd / n : 0) << ","
                    << burstDelayModel->GetMinDelay ().GetMilliSeconds () << ","
                    << burstDelayModel->GetMaxDelay ().GetMilliSeconds () << ","
                    << bc << ","
                    << burstDelayModel->GetDelayHistory ().size () << "\n";
        summaryFile.flush ();
    }

    topoHelper->ExportToFile (outDir + "/topology_raw.txt");
    printf ("  -> files written to %s/\n", outDir.c_str ());

    Simulator::Destroy ();
}

// ═════════════════════════════════════════════════════════════════════════════
// main – two modes:
//   CONTROLLER (numRuns>1): spawns a subprocess per run to isolate ExaTN state
//   WORKER     (runId>0)  : executes exactly one simulation and appends to summary
// ═════════════════════════════════════════════════════════════════════════════
int
main (int argc, char *argv[])
{
    LogComponentEnable ("QCastRandomTopologyExample", LOG_LEVEL_INFO);

    // ── Parameters ────────────────────────────────────────────────────────
    uint32_t numRuns      = 10;
    uint32_t numNodes     = 15;
    uint32_t numRequests  = 12;
    uint32_t topologyType = 1;    // 1 = ErdosRenyi
    uint32_t baseSeed     = 1;
    uint32_t kHop         = 3;
    double   alpha        = 0.1;
    double   edgeProb     = 0.40;
    double   avgDegree    = 4.0;
    double   tCoherence   = 100.0; // Decoherence time in ms
    std::string outDir    = "qcast_results";
    // Worker-mode parameter (set by controller, not exposed in user help)
    uint32_t workerRunId  = 0;
    uint32_t workerSeed   = 0;

    CommandLine cmd;
    cmd.AddValue ("numRuns",      "Number of independent simulation runs",     numRuns);
    cmd.AddValue ("numNodes",     "Number of nodes in topology",               numNodes);
    cmd.AddValue ("numRequests",  "Number of S-D requests per run",            numRequests);
    cmd.AddValue ("topologyType", "0=RandomGeo  1=ErdosRenyi  2=ScaleFree",   topologyType);
    cmd.AddValue ("baseSeed",     "Base random seed (run k uses baseSeed+k-1)",baseSeed);
    cmd.AddValue ("kHop",         "k-hop neighbourhood for recovery paths",    kHop);
    cmd.AddValue ("alpha",        "Swap failure rate constant",                alpha);
    cmd.AddValue ("edgeProb",     "Edge probability (ErdosRenyi)",             edgeProb);
    cmd.AddValue ("avgDegree",    "Average degree (RandomGeometric)",          avgDegree);
    cmd.AddValue ("tCoherence",   "Coherence time of quantum memory (ms)",     tCoherence);
    cmd.AddValue ("outDir",       "Output directory",                          outDir);
    cmd.AddValue ("workerRunId",  "(internal) run id for worker mode",         workerRunId);
    cmd.AddValue ("workerSeed",   "(internal) seed for worker mode",           workerSeed);
    cmd.Parse (argc, argv);

    system (("mkdir -p " + outDir).c_str ());

    // ── WORKER MODE ───────────────────────────────────────────────────────
    // Called by the controller subprocess; runs exactly one simulation and
    // appends one CSV row to outDir/summary.csv.
    if (workerRunId > 0)
    {
        // Open summary for append (may already exist from previous workers)
        std::ofstream summaryFile (outDir + "/summary.csv",
                                   std::ios::app);
        RunOneSimulation (workerRunId, workerSeed,
                          numNodes, numRequests, topologyType,
                          kHop, alpha, edgeProb, avgDegree, tCoherence,
                          outDir, summaryFile);
        summaryFile.close ();
        return 0;
    }

    // ── CONTROLLER MODE ─────────────────────────────────────────────────
    // Write summary header, then spawn one subprocess per run.
    // Each subprocess gets fresh ExaTN state → no tensor-name collisions.
    {
        std::ofstream summaryFile (outDir + "/summary.csv");
        summaryFile << "run_id,seed,num_nodes,num_edges,num_requests,"
                    << "paths_found,success_rate_pct,"
                    << "avg_fidelity,avg_throughput,avg_hops,avg_recovery_paths,"
                    << "avg_total_delay_ms,min_delay_ms,max_delay_ms,"
                    << "burst_events,delay_history_records\n";
    }

    printf ("\n=== Q-CAST Multi-Run Simulation ===\n");
    printf ("Runs=%u  Nodes=%u  Requests=%u  Topology=%s  edgeProb=%.2f  k=%u  tCoherence=%.1fms\n",
            numRuns, numNodes, numRequests,
            (topologyType == 0 ? "RandomGeo" : topologyType == 1 ? "ErdosRenyi" : "ScaleFree"),
            edgeProb, kHop, tCoherence);
    printf ("Output directory: %s/\n", outDir.c_str ());

    std::string binPath = argv[0];   // path to this binary
    for (uint32_t run = 1; run <= numRuns; ++run)
    {
        uint32_t seed = baseSeed + run - 1;
        std::ostringstream oss;
        oss << binPath
            << " --numNodes="     << numNodes
            << " --numRequests="  << numRequests
            << " --topologyType=" << topologyType
            << " --kHop="         << kHop
            << " --alpha="        << alpha
            << " --edgeProb="     << edgeProb
            << " --avgDegree="    << avgDegree
            << " --tCoherence="   << tCoherence
            << " --outDir="       << outDir
            << " --workerRunId="  << run
            << " --workerSeed="   << seed;
        printf ("\n[Spawning run %u/%u  seed=%u]\n", run, numRuns, seed);
        fflush (stdout);
        int rc = system (oss.str ().c_str ());
        if (rc != 0)
            fprintf (stderr, "WARNING: run %u exited with code %d\n", run, rc);
    }

    printf ("\n=== All %u runs complete ===\n", numRuns);
    printf ("Summary       : %s/summary.csv\n",  outDir.c_str ());
    printf ("Per-run files : %s/run_NNN/\n",      outDir.c_str ());
    printf ("  parameters.txt   - configuration and delay model params\n");
    printf ("  topology.csv     - nodes, edges, link fidelity/success_rate\n");
    printf ("  topology_raw.txt - topology helper export\n");
    printf ("  delay_log.csv    - full burst delay change log (every 100 ms)\n");
    printf ("  results.csv      - per-request routing results table\n");
    printf ("  sim_report.txt   - full human-readable simulation report\n");

    return 0;
}

