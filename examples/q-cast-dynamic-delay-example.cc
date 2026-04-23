/* 
  Q-CAST Random Topology Example with Time-Varying Random Classical Delays
  
  This example demonstrates:
  1. Random network topology generation (4 types)
  2. Q-CAST routing on random topologies
  3. Time-varying random classical network delays (5-25ms, updated every 100ms)
  4. End-to-end fidelity measurement
  
  Topology Types:
  - Random Geometric: Nodes placed in 2D space, edges based on distance
  - Erdos-Renyi: Random edges with fixed probability
  - Scale-Free: Barabasi-Albert preferential attachment
  - Grid-Random: Grid layout with random extra connections
  
  To run:
  ./ns3 run "q-cast-dynamic-delay-example --numNodes=10 --numRequests=3 --topologyType=0"
  
  Parameters:
  --numNodes: Number of nodes in the topology (default: 10)
  --numRequests: Number of random S-D pairs (default: 3)
  --topologyType: 0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree, 3=GridRandom (default: 0)
  --seed: Random seed for reproducibility (default: 42)
  --kHop: k-hop neighborhood for recovery (default: 3)
  --alpha: Swap failure rate constant (default: 0.1)
  --minDelay: Minimum classical delay in ms (default: 5)
  --maxDelay: Maximum classical delay in ms (default: 25)
  --delayUpdateInterval: Delay update interval in ms (default: 100)
*/

#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/core-module.h"
#include "ns3/application.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-fidelity-model.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-network-layer.h"
#include "ns3/q-cast-routing-protocol.h"
#include "ns3/quantum-topology-helper.h"
#include "ns3/quantum-net-stack-helper.h"

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <iomanip>
#include <chrono>

NS_LOG_COMPONENT_DEFINE ("QCastDynamicDelayExample");

using namespace ns3;

/**
 * \brief Structure to hold request results with fidelity measurement
 */
struct RequestResult
{
    uint32_t requestId;
    std::string srcNode;
    std::string dstNode;
    std::vector<std::string> route;
    double expectedThroughput;
    double endToEndFidelity;
    uint32_t numHops;
    uint32_t numRecoveryPaths;
    bool success;
    std::string failureReason;
    double avgPathDelay;  // Average classical delay along the path
};

/**
 * \brief Structure to track link delay information
 */
struct LinkDelayInfo
{
    std::string nodeA;
    std::string nodeB;
    Ptr<PointToPointChannel> channel;
    double currentDelay;  // Current delay in ms
    double minDelay;
    double maxDelay;
    std::vector<double> delayHistory;  // History of delays for statistics
};

/**
 * \brief Manager for time-varying random classical network delays
 * 
 * This class manages dynamic delays on classical network links.
 * Delays are randomly updated at regular intervals to simulate
 * real network conditions.
 */
class DynamicDelayManager : public Object
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid =
            TypeId ("ns3::DynamicDelayManager")
                .SetParent<Object> ()
                .SetGroupName ("Quantum")
                .AddConstructor<DynamicDelayManager> ()
                .AddAttribute ("MinDelay", "Minimum delay in ms",
                               DoubleValue (5.0),
                               MakeDoubleAccessor (&DynamicDelayManager::m_minDelay),
                               MakeDoubleChecker<double> (0.1, 1000.0))
                .AddAttribute ("MaxDelay", "Maximum delay in ms",
                               DoubleValue (25.0),
                               MakeDoubleAccessor (&DynamicDelayManager::m_maxDelay),
                               MakeDoubleChecker<double> (0.1, 1000.0))
                .AddAttribute ("UpdateInterval", "Delay update interval",
                               TimeValue (MilliSeconds (100)),
                               MakeTimeAccessor (&DynamicDelayManager::m_updateInterval),
                               MakeTimeChecker ());
        return tid;
    }

    DynamicDelayManager ()
        : m_minDelay (5.0),
          m_maxDelay (25.0),
          m_updateInterval (MilliSeconds (100)),
          m_running (false),
          m_updateCount (0)
    {
        NS_LOG_FUNCTION (this);
        // Create random variable for delay generation
        m_delayRandom = CreateObject<UniformRandomVariable> ();
        m_delayRandom->SetAttribute ("Min", DoubleValue (m_minDelay));
        m_delayRandom->SetAttribute ("Max", DoubleValue (m_maxDelay));
    }

    ~DynamicDelayManager () override
    {
        NS_LOG_FUNCTION (this);
    }

    void DoDispose (void) override
    {
        NS_LOG_FUNCTION (this);
        m_linkDelays.clear ();
        m_delayRandom = nullptr;
        Object::DoDispose ();
    }

    /**
     * \brief Add a link to be managed
     * \param nodeA First node name
     * \param nodeB Second node name
     * \param channel The P2P channel
     */
    void AddLink (const std::string &nodeA, const std::string &nodeB,
                  Ptr<PointToPointChannel> channel)
    {
        NS_LOG_FUNCTION (this << nodeA << nodeB);
        
        LinkDelayInfo info;
        info.nodeA = nodeA;
        info.nodeB = nodeB;
        info.channel = channel;
        info.minDelay = m_minDelay;
        info.maxDelay = m_maxDelay;
        info.currentDelay = m_delayRandom->GetValue ();
        
        // Set initial delay
        channel->SetAttribute ("Delay", TimeValue (MilliSeconds (info.currentDelay)));
        
        m_linkDelays.push_back (info);
        
        NS_LOG_DEBUG ("Added link " << nodeA << "-" << nodeB 
                      << " with initial delay " << info.currentDelay << "ms");
    }

    /**
     * \brief Start the dynamic delay updates
     */
    void Start (void)
    {
        NS_LOG_FUNCTION (this);
        if (!m_running)
        {
            m_running = true;
            ScheduleNextUpdate ();
            NS_LOG_INFO ("DynamicDelayManager started with update interval " 
                         << m_updateInterval.GetMilliSeconds () << "ms");
        }
    }

    /**
     * \brief Stop the dynamic delay updates
     */
    void Stop (void)
    {
        NS_LOG_FUNCTION (this);
        m_running = false;
        if (m_nextUpdateEvent.IsRunning ())
        {
            Simulator::Cancel (m_nextUpdateEvent);
        }
    }

    /**
     * \brief Get current delay for a link
     */
    double GetCurrentDelay (const std::string &nodeA, const std::string &nodeB) const
    {
        for (const auto &link : m_linkDelays)
        {
            if ((link.nodeA == nodeA && link.nodeB == nodeB) ||
                (link.nodeA == nodeB && link.nodeB == nodeA))
            {
                return link.currentDelay;
            }
        }
        return 0.0;
    }

    /**
     * \brief Get average delay for a path
     */
    double GetPathDelay (const std::vector<std::string> &path) const
    {
        if (path.size () < 2)
        {
            return 0.0;
        }

        double totalDelay = 0.0;
        uint32_t linkCount = 0;

        for (size_t i = 0; i < path.size () - 1; ++i)
        {
            double delay = GetCurrentDelay (path[i], path[i + 1]);
            if (delay > 0)
            {
                totalDelay += delay;
                linkCount++;
            }
        }

        return linkCount > 0 ? totalDelay / linkCount : 0.0;
    }

    /**
     * \brief Print delay statistics
     */
    void PrintStatistics (void) const
    {
        NS_LOG_INFO ("\n=== Classical Network Delay Statistics ===");
        NS_LOG_INFO ("Total links managed: " << m_linkDelays.size ());
        NS_LOG_INFO ("Delay update count: " << m_updateCount);
        NS_LOG_INFO ("Delay range: " << m_minDelay << "ms - " << m_maxDelay << "ms");
        
        if (!m_linkDelays.empty ())
        {
            double totalCurrentDelay = 0.0;
            double minCurrent = std::numeric_limits<double>::max ();
            double maxCurrent = 0.0;
            
            for (const auto &link : m_linkDelays)
            {
                totalCurrentDelay += link.currentDelay;
                minCurrent = std::min (minCurrent, link.currentDelay);
                maxCurrent = std::max (maxCurrent, link.currentDelay);
            }
            
            NS_LOG_INFO ("Current average delay: " 
                         << std::fixed << std::setprecision (2) 
                         << (totalCurrentDelay / m_linkDelays.size ()) << "ms");
            NS_LOG_INFO ("Current min delay: " << minCurrent << "ms");
            NS_LOG_INFO ("Current max delay: " << maxCurrent << "ms");
        }
        NS_LOG_INFO ("==========================================");
    }

private:
    void ScheduleNextUpdate (void)
    {
        if (!m_running)
        {
            return;
        }
        
        m_nextUpdateEvent = Simulator::Schedule (m_updateInterval, 
                                                  &DynamicDelayManager::UpdateDelays, 
                                                  this);
    }

    void UpdateDelays (void)
    {
        NS_LOG_FUNCTION (this);
        
        m_updateCount++;
        
        // Update delay for each link
        for (auto &link : m_linkDelays)
        {
            // Generate new random delay
            double newDelay = m_delayRandom->GetValue ();
            link.currentDelay = newDelay;
            link.delayHistory.push_back (newDelay);
            
            // Apply to channel
            if (link.channel)
            {
                link.channel->SetAttribute ("Delay", 
                                            TimeValue (MilliSeconds (newDelay)));
            }
            
            NS_LOG_DEBUG ("Updated delay for link " << link.nodeA << "-" << link.nodeB 
                          << " to " << newDelay << "ms");
        }
        
        // Log every 10 updates
        if (m_updateCount % 10 == 0)
        {
            NS_LOG_INFO ("Delay update #" << m_updateCount 
                         << ": Updated " << m_linkDelays.size () << " links");
        }
        
        ScheduleNextUpdate ();
    }

    double m_minDelay;
    double m_maxDelay;
    Time m_updateInterval;
    Ptr<UniformRandomVariable> m_delayRandom;
    std::vector<LinkDelayInfo> m_linkDelays;
    bool m_running;
    EventId m_nextUpdateEvent;
    uint32_t m_updateCount;
};

/**
 * \brief Application that runs Q-CAST on random topology with dynamic delays
 */
class QCastDynamicDelayApp : public Application
{
public:
    QCastDynamicDelayApp (Ptr<QuantumPhyEntity> qphyent,
                          const std::vector<Ptr<QuantumNetworkLayer>> &netLayers,
                          const std::vector<Ptr<QCastRoutingProtocol>> &routingProtocols,
                          Ptr<DynamicDelayManager> delayManager)
        : m_qphyent (qphyent),
          m_netLayers (netLayers),
          m_routingProtocols (routingProtocols),
          m_delayManager (delayManager),
          m_numRequests (3)
    {
    }

    void SetNumRequests (uint32_t numRequests)
    {
        m_numRequests = numRequests;
    }

    void StartApplication (void) override
    {
        StartSimulation ();
    }

    void StopApplication (void) override {}

    void StartSimulation (void)
    {
        NS_LOG_INFO ("\n========================================");
        NS_LOG_INFO ("Starting Q-CAST Dynamic Delay Simulation");
        NS_LOG_INFO ("========================================\n");
        
        // Generate random requests
        std::vector<QCastRequest> requests = GenerateRandomRequests ();
        
        NS_LOG_INFO ("Generated " << requests.size () << " random requests:");
        for (const auto &req : requests)
        {
            NS_LOG_INFO ("  Request " << req.requestId << ": " << req.srcNode << " -> " 
                         << req.dstNode);
        }
        
        // Run Q-CAST using the first node's routing protocol
        Ptr<QCastRoutingProtocol> qcast = m_routingProtocols[0];
        auto paths = qcast->CalculateRoutesGEDA (requests);
        
        NS_LOG_INFO ("\n=== Q-CAST Routing Results ===");
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
            
            // Get average classical delay for this path
            result.avgPathDelay = m_delayManager->GetPathDelay (path.primaryPath);
            
            results.push_back (result);
            
            // Print results
            NS_LOG_INFO ("\n--- Request " << result.requestId << " ---");
            NS_LOG_INFO ("Source: " << result.srcNode);
            NS_LOG_INFO ("Destination: " << result.dstNode);
            NS_LOG_INFO ("Route: " << qcast->RouteToString (result.route));
            NS_LOG_INFO ("Hops: " << result.numHops);
            NS_LOG_INFO ("Expected Throughput: " << std::fixed << std::setprecision (4) 
                         << result.expectedThroughput);
            NS_LOG_INFO ("End-to-End Fidelity: " << std::setprecision (4) 
                         << result.endToEndFidelity);
            NS_LOG_INFO ("Average Path Delay: " << std::setprecision (2) 
                         << result.avgPathDelay << "ms");
            NS_LOG_INFO ("Recovery Paths: " << result.numRecoveryPaths);
            
            // Generate and display swap schedule
            auto schedule = qcast->GenerateSwapSchedule (path.primaryPath);
            NS_LOG_INFO ("Swap Schedule Rounds: " << schedule.size ());
        }
        
        // Print summary statistics
        PrintStatistics (results);
        
        // Print delay manager statistics
        m_delayManager->PrintStatistics ();
        
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
        std::mt19937 rng (42); // Fixed seed for reproducibility
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

        if (path.linkProbabilities.empty ())
        {
            return 0.0;
        }

        // This example still maps success probability to a surrogate link fidelity,
        // but the end-to-end combination now uses the correct Bell-diagonal
        // entanglement-swapping rule instead of multiplying by an ad hoc swap factor.
        BellDiagonalState state = MakeWernerState (0.5 + 0.5 * path.linkProbabilities[0]);
        for (size_t i = 1; i < path.linkProbabilities.size (); ++i)
        {
            BellDiagonalState nextLink = MakeWernerState (0.5 + 0.5 * path.linkProbabilities[i]);
            state = EntanglementSwapBellDiagonal (state, nextLink);
        }

        return GetBellFidelity (state);
    }

    void PrintStatistics (const std::vector<RequestResult> &results)
    {
        NS_LOG_INFO ("\n=== Routing Summary Statistics ===");
        
        if (results.empty ())
        {
            NS_LOG_INFO ("No successful requests");
            return;
        }
        
        uint32_t successful = 0;
        uint32_t failed = 0;
        double totalFidelity = 0.0;
        double totalThroughput = 0.0;
        double totalDelay = 0.0;
        uint32_t totalHops = 0;
        uint32_t totalRecoveryPaths = 0;
        
        for (const auto &result : results)
        {
            if (result.success)
            {
                successful++;
                totalFidelity += result.endToEndFidelity;
                totalThroughput += result.expectedThroughput;
                totalDelay += result.avgPathDelay;
                totalHops += result.numHops;
                totalRecoveryPaths += result.numRecoveryPaths;
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
            NS_LOG_INFO ("Average Fidelity: " << std::fixed << std::setprecision (4) 
                         << (totalFidelity / successful));
            NS_LOG_INFO ("Average Throughput: " << std::setprecision (4) 
                         << (totalThroughput / successful));
            NS_LOG_INFO ("Average Path Delay: " << std::setprecision (2) 
                         << (totalDelay / successful) << "ms");
            NS_LOG_INFO ("Average Hops: " << std::setprecision (2)
                         << (static_cast<double> (totalHops) / successful));
            NS_LOG_INFO ("Average Recovery Paths: " << std::setprecision (2)
                         << (static_cast<double> (totalRecoveryPaths) / successful));
        }
        
        // Find best and worst fidelity paths
        auto bestIt = std::max_element (results.begin (), results.end (),
                                        [] (const RequestResult &a, const RequestResult &b)
                                        {
                                            return a.endToEndFidelity < b.endToEndFidelity;
                                        });
        
        auto worstIt = std::min_element (results.begin (), results.end (),
                                         [] (const RequestResult &a, const RequestResult &b)
                                         {
                                             return a.endToEndFidelity < b.endToEndFidelity;
                                         });
        
        if (bestIt != results.end ())
        {
            NS_LOG_INFO ("\nBest Fidelity Path:");
            NS_LOG_INFO ("  Request " << bestIt->requestId << ": " << bestIt->srcNode 
                         << " -> " << bestIt->dstNode);
            NS_LOG_INFO ("  Fidelity: " << std::setprecision (4) << bestIt->endToEndFidelity);
            NS_LOG_INFO ("  Hops: " << bestIt->numHops);
            NS_LOG_INFO ("  Avg Delay: " << std::setprecision (2) << bestIt->avgPathDelay << "ms");
        }
        
        if (worstIt != results.end () && results.size () > 1)
        {
            NS_LOG_INFO ("\nWorst Fidelity Path:");
            NS_LOG_INFO ("  Request " << worstIt->requestId << ": " << worstIt->srcNode 
                         << " -> " << worstIt->dstNode);
            NS_LOG_INFO ("  Fidelity: " << std::setprecision (4) << worstIt->endToEndFidelity);
            NS_LOG_INFO ("  Hops: " << worstIt->numHops);
            NS_LOG_INFO ("  Avg Delay: " << std::setprecision (2) << worstIt->avgPathDelay << "ms");
        }
    }

    Ptr<QuantumPhyEntity> m_qphyent;
    std::vector<Ptr<QuantumNetworkLayer>> m_netLayers;
    std::vector<Ptr<QCastRoutingProtocol>> m_routingProtocols;
    Ptr<DynamicDelayManager> m_delayManager;
    uint32_t m_numRequests;
    std::vector<RequestResult> m_results;
};

int
main (int argc, char *argv[])
{
    NS_LOG_INFO ("Q-CAST Random Topology with Dynamic Classical Delays");

    // Parse command line arguments
    CommandLine cmd;
    
    uint32_t numNodes = 10;
    uint32_t numRequests = 3;
    uint32_t topologyType = 0; // 0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree, 3=GridRandom
    uint32_t seed = 42;
    uint32_t kHop = 3;
    double alpha = 0.1;
    double edgeProb = 0.3;
    double avgDegree = 3.0;
    double minDelay = 5.0;     // Minimum classical delay in ms
    double maxDelay = 25.0;    // Maximum classical delay in ms
    uint32_t delayUpdateInterval = 100;  // Delay update interval in ms
    
    cmd.AddValue ("numNodes", "Number of nodes in topology", numNodes);
    cmd.AddValue ("numRequests", "Number of S-D requests", numRequests);
    cmd.AddValue ("topologyType", "Topology type (0=RandomGeo, 1=ErdosRenyi, 2=ScaleFree, 3=GridRandom)", 
                  topologyType);
    cmd.AddValue ("seed", "Random seed", seed);
    cmd.AddValue ("kHop", "k-hop neighborhood", kHop);
    cmd.AddValue ("alpha", "Swap failure rate constant", alpha);
    cmd.AddValue ("edgeProb", "Edge probability for Erdos-Renyi", edgeProb);
    cmd.AddValue ("avgDegree", "Average degree for Random Geometric", avgDegree);
    cmd.AddValue ("minDelay", "Minimum classical delay in ms", minDelay);
    cmd.AddValue ("maxDelay", "Maximum classical delay in ms", maxDelay);
    cmd.AddValue ("delayUpdateInterval", "Delay update interval in ms", delayUpdateInterval);
    
    cmd.Parse (argc, argv);

    // Validate topology type
    if (topologyType > 3)
    {
        NS_LOG_ERROR ("Invalid topology type. Using Random Geometric (0).");
        topologyType = 0;
    }

    std::string topologyName;
    switch (topologyType)
    {
        case 0: topologyName = "RandomGeometric"; break;
        case 1: topologyName = "ErdosRenyi"; break;
        case 2: topologyName = "ScaleFree"; break;
        case 3: topologyName = "GridRandom"; break;
    }

    NS_LOG_INFO ("\n========== Configuration ==========");
    NS_LOG_INFO ("Nodes: " << numNodes);
    NS_LOG_INFO ("Requests: " << numRequests);
    NS_LOG_INFO ("Topology: " << topologyName);
    NS_LOG_INFO ("Seed: " << seed);
    NS_LOG_INFO ("Q-CAST Parameters:");
    NS_LOG_INFO ("  k-Hop: " << kHop);
    NS_LOG_INFO ("  Alpha: " << alpha);
    NS_LOG_INFO ("Classical Network:");
    NS_LOG_INFO ("  Delay Range: " << minDelay << "ms - " << maxDelay << "ms");
    NS_LOG_INFO ("  Update Interval: " << delayUpdateInterval << "ms");
    NS_LOG_INFO ("===================================\n");

    //
    // Step 1: Generate random topology using helper
    //
    Ptr<QuantumTopologyHelper> topoHelper = CreateObject<QuantumTopologyHelper> ();
    topoHelper->SetNumNodes (numNodes);
    topoHelper->SetRandomSeed (seed);
    topoHelper->SetTopologyType (static_cast<QuantumTopologyHelper::TopologyType> (topologyType));
    
    if (topologyType == 0)
    {
        topoHelper->SetAverageDegree (avgDegree);
    }
    else if (topologyType == 1)
    {
        topoHelper->SetEdgeProbability (edgeProb);
    }
    
    // Set high-quality quantum links
    topoHelper->SetLinkQualityRange (0.94, 0.995, 0.92, 0.98);

    //
    // Step 2: Create quantum physical entity with nodes from topology
    //
    std::vector<std::string> owners;
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        owners.push_back ("Node" + std::to_string (i));
    }
    
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);
    
    //
    // Step 3: Generate topology and get edge information
    //
    NodeContainer nodes = topoHelper->GenerateTopology (qphyent);
    std::vector<std::pair<std::string, std::string>> edges = topoHelper->GetEdges ();
    auto linkProps = topoHelper->GetLinkProperties ();
    
    NS_LOG_INFO ("\n=== Topology Generated ===");
    NS_LOG_INFO ("Nodes: " << numNodes);
    NS_LOG_INFO ("Edges: " << edges.size ());
    topoHelper->PrintStatistics ();

    //
    // Step 4: Setup classical network connections with dynamic random delays
    //
    // Use Point-to-Point for individual link delays
    PointToPointHelper p2pHelper;
    p2pHelper.SetDeviceAttribute ("DataRate", StringValue ("1000kbps"));
    
    // Create delay manager
    Ptr<DynamicDelayManager> delayManager = CreateObject<DynamicDelayManager> ();
    delayManager->SetAttribute ("MinDelay", DoubleValue (minDelay));
    delayManager->SetAttribute ("MaxDelay", DoubleValue (maxDelay));
    delayManager->SetAttribute ("UpdateInterval", TimeValue (MilliSeconds (delayUpdateInterval)));
    
    // Install Internet stack first
    InternetStackHelper stack;
    stack.Install (nodes);
    
    // Create a map to track node indices
    std::map<std::string, uint32_t> nodeNameToIndex;
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeNameToIndex["Node" + std::to_string (i)] = i;
    }
    
    // Create P2P connections for each edge with random initial delays
    Ipv6AddressHelper address;
    address.SetBase ("2001:1::", Ipv6Prefix (64));
    
    Ptr<UniformRandomVariable> initialDelayRandom = CreateObject<UniformRandomVariable> ();
    initialDelayRandom->SetAttribute ("Min", DoubleValue (minDelay));
    initialDelayRandom->SetAttribute ("Max", DoubleValue (maxDelay));
    
    for (const auto &edge : edges)
    {
        uint32_t idx1 = nodeNameToIndex[edge.first];
        uint32_t idx2 = nodeNameToIndex[edge.second];
        
        // Set initial random delay for this link
        double initialDelay = initialDelayRandom->GetValue ();
        p2pHelper.SetChannelAttribute ("Delay", 
                                       TimeValue (MilliSeconds (initialDelay)));
        
        // Install P2P link between the two nodes
        NodeContainer linkNodes;
        linkNodes.Add (nodes.Get (idx1));
        linkNodes.Add (nodes.Get (idx2));
        
        NetDeviceContainer devices = p2pHelper.Install (linkNodes);
        
        // Assign addresses
        Ipv6InterfaceContainer interfaces = address.Assign (devices);
        
        // Get the channel and register with delay manager
        Ptr<PointToPointChannel> channel = 
            DynamicCast<PointToPointChannel> (devices.Get (0)->GetChannel ());
        if (channel)
        {
            delayManager->AddLink (edge.first, edge.second, channel);
        }
    }

    // Set addresses in quantum entity
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        // Use loopback address for now (actual addressing done per link)
        qphyent->SetOwnerAddress ("Node" + std::to_string (i), 
                                   Ipv6Address ("::1"));
        qphyent->SetOwnerRank ("Node" + std::to_string (i), i);
    }

    //
    // Step 5: Install quantum network stack and configure Q-CAST
    //
    QuantumNetStackHelper qstack;
    qstack.Install (nodes);

    std::vector<Ptr<QuantumNetworkLayer>> netLayers;
    std::vector<Ptr<QCastRoutingProtocol>> routingProtocols;

    // Create network layers for all nodes
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        std::string nodeName = "Node" + std::to_string (i);
        
        Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer> ();
        netLayer->SetOwner (nodeName);
        netLayer->SetPhyEntity (qphyent);
        
        // Create Q-CAST routing protocol
        Ptr<QCastRoutingProtocol> qcastRouting = CreateObject<QCastRoutingProtocol> ();
        qcastRouting->SetKHop (kHop);
        qcastRouting->SetAlpha (alpha);
        qcastRouting->SetNodeCapacity (5);
        
        netLayer->SetRoutingProtocol (qcastRouting);
        
        // Add neighbors based on topology edges
        for (const auto &edge : edges)
        {
            if (edge.first == nodeName)
            {
                auto propIt = linkProps.find (edge);
                if (propIt != linkProps.end ())
                {
                    double fidelity = propIt->second.first;
                    double successRate = propIt->second.second;
                    Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (edge.first, edge.second);
                    netLayer->AddNeighbor (edge.second, channel, fidelity, successRate);
                }
            }
            else if (edge.second == nodeName)
            {
                // Add reverse direction
                auto propIt = linkProps.find (edge);
                if (propIt != linkProps.end ())
                {
                    double fidelity = propIt->second.first;
                    double successRate = propIt->second.second;
                    Ptr<QuantumChannel> channel = CreateObject<QuantumChannel> (edge.second, edge.first);
                    netLayer->AddNeighbor (edge.first, channel, fidelity, successRate);
                }
            }
        }
        
        netLayer->Initialize ();
        
        netLayers.push_back (netLayer);
        routingProtocols.push_back (qcastRouting);
        // QuantumNetworkLayer is not an Application - do not AddApplication
    }

    //
    // Step 6: Create and run Q-CAST application
    //
    Ptr<QCastDynamicDelayApp> app = CreateObject<QCastDynamicDelayApp> (
        qphyent, netLayers, routingProtocols, delayManager);
    app->SetNumRequests (numRequests);
    nodes.Get (0)->AddApplication (app);
    app->SetStartTime (Seconds (0.5));

    //
    // Step 7: Run simulation
    //
    // Start dynamic delay updates
    delayManager->Start ();
    
    Simulator::Stop (Seconds (2.0));
    
    NS_LOG_INFO ("\n=== Starting Simulation ===");
    auto start = std::chrono::high_resolution_clock::now ();
    Simulator::Run ();
    auto end = std::chrono::high_resolution_clock::now ();
    
    // Stop delay manager
    delayManager->Stop ();
    
    printf ("\n=== Simulation Complete ===\n");
    printf ("Total simulation time: %ld ms\n",
            std::chrono::duration_cast<std::chrono::milliseconds> (end - start).count ());
    
    // Export topology for visualization
    topoHelper->ExportToFile ("qcast_dynamic_topology.txt");
    NS_LOG_INFO ("\nTopology exported to qcast_dynamic_topology.txt");
    
    Simulator::Destroy ();

    return 0;
}
