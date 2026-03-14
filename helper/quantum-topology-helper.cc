#include "ns3/quantum-topology-helper.h"

#include "ns3/log.h"
#include "ns3/quantum-node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/enum.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumTopologyHelper");

NS_OBJECT_ENSURE_REGISTERED (QuantumTopologyHelper);

TypeId
QuantumTopologyHelper::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QuantumTopologyHelper")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddConstructor<QuantumTopologyHelper> ()
          .AddAttribute ("TopologyType", "Type of topology to generate",
                         EnumValue (RANDOM_GEOMETRIC),
                         MakeEnumAccessor<QuantumTopologyHelper::TopologyType> (&QuantumTopologyHelper::m_topologyType),
                         MakeEnumChecker (RANDOM_GEOMETRIC, "RandomGeometric", ERDOS_RENYI,
                                          "ErdosRenyi", SCALE_FREE, "ScaleFree", GRID_RANDOM,
                                          "GridRandom"))
          .AddAttribute ("NumNodes", "Number of nodes in the topology",
                         UintegerValue (20),
                         MakeUintegerAccessor (&QuantumTopologyHelper::m_numNodes),
                         MakeUintegerChecker<uint32_t> (2, 1000))
          .AddAttribute ("RandomSeed", "Random seed for reproducibility",
                         UintegerValue (1),
                         MakeUintegerAccessor (&QuantumTopologyHelper::m_randomSeed),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("AverageDegree", "Average node degree",
                         DoubleValue (4.0),
                         MakeDoubleAccessor (&QuantumTopologyHelper::m_averageDegree),
                         MakeDoubleChecker<double> (1.0, 20.0))
          .AddAttribute ("EdgeProbability", "Edge probability for Erdos-Renyi",
                         DoubleValue (0.2),
                         MakeDoubleAccessor (&QuantumTopologyHelper::m_edgeProbability),
                         MakeDoubleChecker<double> (0.0, 1.0))
          .AddAttribute ("GridWidth", "Grid width for GRID_RANDOM",
                         UintegerValue (5),
                         MakeUintegerAccessor (&QuantumTopologyHelper::m_gridWidth),
                         MakeUintegerChecker<uint32_t> (1))
          .AddAttribute ("GridHeight", "Grid height for GRID_RANDOM",
                         UintegerValue (5),
                         MakeUintegerAccessor (&QuantumTopologyHelper::m_gridHeight),
                         MakeUintegerChecker<uint32_t> (1))
           .AddAttribute ("MinFidelity", "Minimum link fidelity",
                          DoubleValue (0.92),
                          MakeDoubleAccessor (&QuantumTopologyHelper::m_minFidelity),
                          MakeDoubleChecker<double> (0.5, 1.0))
           .AddAttribute ("MaxFidelity", "Maximum link fidelity",
                          DoubleValue (0.995),
                          MakeDoubleAccessor (&QuantumTopologyHelper::m_maxFidelity),
                          MakeDoubleChecker<double> (0.5, 1.0))
           .AddAttribute ("MinSuccessRate", "Minimum link success rate",
                          DoubleValue (0.88),
                          MakeDoubleAccessor (&QuantumTopologyHelper::m_minSuccessRate),
                          MakeDoubleChecker<double> (0.0, 1.0))
           .AddAttribute ("MaxSuccessRate", "Maximum link success rate",
                          DoubleValue (0.98),
                          MakeDoubleAccessor (&QuantumTopologyHelper::m_maxSuccessRate),
                          MakeDoubleChecker<double> (0.0, 1.0));
  return tid;
}

QuantumTopologyHelper::QuantumTopologyHelper ()
    : m_topologyType (RANDOM_GEOMETRIC),
      m_numNodes (20),
      m_randomSeed (1),
      m_averageDegree (4.0),
      m_edgeProbability (0.2),
      m_gridWidth (5),
      m_gridHeight (5),
      m_minFidelity (0.92),
      m_maxFidelity (0.995),
      m_minSuccessRate (0.88),
      m_maxSuccessRate (0.98),
      m_rng (m_randomSeed),
      m_generated (false)
{
  NS_LOG_FUNCTION (this);
}

QuantumTopologyHelper::~QuantumTopologyHelper ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumTopologyHelper::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_nodes = NodeContainer ();
  m_nodeNames.clear ();
  m_edges.clear ();
  m_nodePositions.clear ();
  m_linkProperties.clear ();
  Object::DoDispose ();
}

void
QuantumTopologyHelper::SetTopologyType (TopologyType type)
{
  m_topologyType = type;
}

void
QuantumTopologyHelper::SetRandomSeed (uint32_t seed)
{
  m_randomSeed = seed;
  m_rng.seed (seed);
}

void
QuantumTopologyHelper::SetNumNodes (uint32_t numNodes)
{
  m_numNodes = numNodes;
}

void
QuantumTopologyHelper::SetAverageDegree (double avgDegree)
{
  m_averageDegree = avgDegree;
}

void
QuantumTopologyHelper::SetEdgeProbability (double probability)
{
  m_edgeProbability = probability;
}

void
QuantumTopologyHelper::SetGridDimensions (uint32_t width, uint32_t height)
{
  m_gridWidth = width;
  m_gridHeight = height;
  m_numNodes = width * height;
}

void
QuantumTopologyHelper::SetLinkQualityRange (double minFidelity, double maxFidelity,
                                            double minSuccessRate, double maxSuccessRate)
{
  m_minFidelity = minFidelity;
  m_maxFidelity = maxFidelity;
  m_minSuccessRate = minSuccessRate;
  m_maxSuccessRate = maxSuccessRate;
}

NodeContainer
QuantumTopologyHelper::GenerateTopology (Ptr<QuantumPhyEntity> qphyent)
{
  NS_LOG_FUNCTION (this << qphyent);

  if (m_generated)
    {
      NS_LOG_WARN ("Topology already generated, returning existing nodes");
      return m_nodes;
    }

  // Reset RNG with seed
  m_rng.seed (m_randomSeed);

  // Generate node names
  m_nodeNames.clear ();
  for (uint32_t i = 0; i < m_numNodes; ++i)
    {
      m_nodeNames.push_back ("Node" + std::to_string (i));
    }

  // Generate topology based on type
  switch (m_topologyType)
    {
    case RANDOM_GEOMETRIC:
      GenerateRandomGeometric ();
      break;
    case ERDOS_RENYI:
      GenerateErdosRenyi ();
      break;
    case SCALE_FREE:
      GenerateScaleFree ();
      break;
    case GRID_RANDOM:
      GenerateGridRandom ();
      break;
    default:
      GenerateRandomGeometric ();
    }

  // Create nodes if physical entity provided
  if (qphyent)
    {
      // Create nodes
      for (uint32_t i = 0; i < m_numNodes; ++i)
        {
          Ptr<QuantumNode> node = qphyent->GetNode (m_nodeNames[i]);
          if (node)
            {
              m_nodes.Add (node);
            }
        }
    }

  m_generated = true;

  NS_LOG_INFO ("Generated topology with " << m_numNodes << " nodes and " << m_edges.size ()
                                           << " edges");

  return m_nodes;
}

void
QuantumTopologyHelper::GenerateRandomGeometric (void)
{
  NS_LOG_FUNCTION (this);

  // Place nodes randomly in unit square [0,1] x [0,1]
  std::uniform_real_distribution<double> uniformDist (0.0, 1.0);

  for (const auto &name : m_nodeNames)
    {
      double x = uniformDist (m_rng);
      double y = uniformDist (m_rng);
      m_nodePositions[name] = {x, y};
    }

  // Calculate connection radius for desired average degree
  // Approximate: πr² × n ≈ avg_degree
  double area = 1.0;
  double radius = std::sqrt (m_averageDegree / (M_PI * m_numNodes / area));

  // Create edges based on distance
  for (uint32_t i = 0; i < m_numNodes; ++i)
    {
      for (uint32_t j = i + 1; j < m_numNodes; ++j)
        {
          double dist = CalculateDistance (m_nodeNames[i], m_nodeNames[j]);
          if (dist <= radius)
            {
              m_edges.push_back ({m_nodeNames[i], m_nodeNames[j]});
              m_linkProperties[{m_nodeNames[i], m_nodeNames[j]}] = GenerateLinkProperties ();
            }
        }
    }
}

void
QuantumTopologyHelper::GenerateErdosRenyi (void)
{
  NS_LOG_FUNCTION (this);

  std::uniform_real_distribution<double> uniformDist (0.0, 1.0);

  // For each pair of nodes, create edge with probability p
  for (uint32_t i = 0; i < m_numNodes; ++i)
    {
      for (uint32_t j = i + 1; j < m_numNodes; ++j)
        {
          if (uniformDist (m_rng) < m_edgeProbability)
            {
              m_edges.push_back ({m_nodeNames[i], m_nodeNames[j]});
              m_linkProperties[{m_nodeNames[i], m_nodeNames[j]}] = GenerateLinkProperties ();
            }
        }
    }
}

void
QuantumTopologyHelper::GenerateScaleFree (void)
{
  NS_LOG_FUNCTION (this);

  // Barabasi-Albert model
  // Start with m0 fully connected nodes
  uint32_t m0 = 3;
  if (m0 >= m_numNodes)
    {
      m0 = std::min (static_cast<uint32_t> (2), m_numNodes - 1);
    }

  // Create initial clique
  for (uint32_t i = 0; i < m0; ++i)
    {
      for (uint32_t j = i + 1; j < m0; ++j)
        {
          m_edges.push_back ({m_nodeNames[i], m_nodeNames[j]});
          m_linkProperties[{m_nodeNames[i], m_nodeNames[j]}] = GenerateLinkProperties ();
        }
    }

  // Add remaining nodes with preferential attachment
  std::vector<uint32_t> degrees (m_numNodes, 0);
  for (uint32_t i = 0; i < m0; ++i)
    {
      degrees[i] = m0 - 1;
    }

  uint32_t m = 2; // Number of edges to add per new node

  for (uint32_t i = m0; i < m_numNodes; ++i)
    {
      uint32_t edgesAdded = 0;
      std::vector<uint32_t> candidates;

      for (uint32_t j = 0; j < i; ++j)
        {
          for (uint32_t k = 0; k < degrees[j]; ++k)
            {
              candidates.push_back (j);
            }
        }

      while (edgesAdded < m && edgesAdded < i)
        {
          if (candidates.empty ())
            break;

          std::uniform_int_distribution<uint32_t> intDist (0, (uint32_t)(candidates.size () - 1));
          uint32_t targetIdx = intDist (m_rng);
          uint32_t target = candidates[targetIdx];

          // Check if edge already exists
          bool exists = false;
          for (const auto &edge : m_edges)
            {
              if ((edge.first == m_nodeNames[i] && edge.second == m_nodeNames[target]) ||
                  (edge.first == m_nodeNames[target] && edge.second == m_nodeNames[i]))
                {
                  exists = true;
                  break;
                }
            }

          if (!exists)
            {
              m_edges.push_back ({m_nodeNames[i], m_nodeNames[target]});
              m_linkProperties[{m_nodeNames[i], m_nodeNames[target]}] =
                  GenerateLinkProperties ();
              degrees[i]++;
              degrees[target]++;
              edgesAdded++;
            }

          // Remove used candidate
          candidates.erase (candidates.begin () + targetIdx);
        }
    }
}

void
QuantumTopologyHelper::GenerateGridRandom (void)
{
  NS_LOG_FUNCTION (this);

  m_numNodes = m_gridWidth * m_gridHeight;
  m_nodeNames.clear ();
  for (uint32_t i = 0; i < m_numNodes; ++i)
    {
      m_nodeNames.push_back ("Node" + std::to_string (i));
    }

  // Assign grid positions
  for (uint32_t y = 0; y < m_gridHeight; ++y)
    {
      for (uint32_t x = 0; x < m_gridWidth; ++x)
        {
          uint32_t idx = y * m_gridWidth + x;
          m_nodePositions[m_nodeNames[idx]] = {static_cast<double> (x),
                                               static_cast<double> (y)};
        }
    }

  // Add grid edges (horizontal and vertical)
  for (uint32_t y = 0; y < m_gridHeight; ++y)
    {
      for (uint32_t x = 0; x < m_gridWidth; ++x)
        {
          uint32_t idx = y * m_gridWidth + x;

          // Horizontal edge
          if (x < m_gridWidth - 1)
            {
              uint32_t rightIdx = y * m_gridWidth + (x + 1);
              m_edges.push_back ({m_nodeNames[idx], m_nodeNames[rightIdx]});
              m_linkProperties[{m_nodeNames[idx], m_nodeNames[rightIdx]}] =
                  GenerateLinkProperties ();
            }

          // Vertical edge
          if (y < m_gridHeight - 1)
            {
              uint32_t downIdx = (y + 1) * m_gridWidth + x;
              m_edges.push_back ({m_nodeNames[idx], m_nodeNames[downIdx]});
              m_linkProperties[{m_nodeNames[idx], m_nodeNames[downIdx]}] =
                  GenerateLinkProperties ();
            }
        }
    }

  // Add random diagonal/long-range edges
  std::uniform_real_distribution<double> uniformDist (0.0, 1.0);
  uint32_t numRandomEdges = m_numNodes / 2;

  for (uint32_t e = 0; e < numRandomEdges; ++e)
    {
      std::uniform_int_distribution<uint32_t> nodeDist (0, m_numNodes - 1);
      uint32_t i = nodeDist (m_rng);
      uint32_t j = nodeDist (m_rng);

      if (i == j)
        continue;

      // Check if edge already exists
      bool exists = false;
      for (const auto &edge : m_edges)
        {
          if ((edge.first == m_nodeNames[i] && edge.second == m_nodeNames[j]) ||
              (edge.first == m_nodeNames[j] && edge.second == m_nodeNames[i]))
            {
              exists = true;
              break;
            }
        }

      if (!exists)
        {
          m_edges.push_back ({m_nodeNames[i], m_nodeNames[j]});
          m_linkProperties[{m_nodeNames[i], m_nodeNames[j]}] = GenerateLinkProperties ();
        }
    }
}

double
QuantumTopologyHelper::CalculateDistance (const std::string &node1,
                                          const std::string &node2) const
{
  auto it1 = m_nodePositions.find (node1);
  auto it2 = m_nodePositions.find (node2);

  if (it1 == m_nodePositions.end () || it2 == m_nodePositions.end ())
    {
      return std::numeric_limits<double>::max ();
    }

  double dx = it1->second.first - it2->second.first;
  double dy = it1->second.second - it2->second.second;

  return std::sqrt (dx * dx + dy * dy);
}

std::pair<double, double>
QuantumTopologyHelper::GenerateLinkProperties (void)
{
  std::uniform_real_distribution<double> fidelityDist (m_minFidelity, m_maxFidelity);
  std::uniform_real_distribution<double> successDist (m_minSuccessRate, m_maxSuccessRate);

  return {fidelityDist (m_rng), successDist (m_rng)};
}

std::vector<std::pair<std::string, std::string>>
QuantumTopologyHelper::GetEdges (void) const
{
  return m_edges;
}

std::map<std::string, std::pair<double, double>>
QuantumTopologyHelper::GetNodePositions (void) const
{
  return m_nodePositions;
}

std::map<std::pair<std::string, std::string>, std::pair<double, double>>
QuantumTopologyHelper::GetLinkProperties (void) const
{
  return m_linkProperties;
}

void
QuantumTopologyHelper::PrintStatistics (void) const
{
  if (!m_generated)
    {
      NS_LOG_WARN ("Topology not yet generated");
      return;
    }

  std::cout << "\n=== Topology Statistics ===\n";
  std::cout << "Type: ";
  switch (m_topologyType)
    {
    case RANDOM_GEOMETRIC:
      std::cout << "Random Geometric";
      break;
    case ERDOS_RENYI:
      std::cout << "Erdos-Renyi";
      break;
    case SCALE_FREE:
      std::cout << "Scale-Free (Barabasi-Albert)";
      break;
    case GRID_RANDOM:
      std::cout << "Grid with Random Connections";
      break;
    }
  std::cout << "\n";

  std::cout << "Nodes: " << m_numNodes << "\n";
  std::cout << "Edges: " << m_edges.size () << "\n";

  // Calculate average degree
  std::map<std::string, uint32_t> degrees;
  for (const auto &edge : m_edges)
    {
      degrees[edge.first]++;
      degrees[edge.second]++;
    }

  double avgDegree = 0.0;
  uint32_t maxDegree = 0;
  for (const auto &entry : degrees)
    {
      avgDegree += entry.second;
      maxDegree = std::max (maxDegree, entry.second);
    }
  avgDegree /= m_numNodes;

  std::cout << "Average Degree: " << std::fixed << std::setprecision (2) << avgDegree
            << "\n";
  std::cout << "Max Degree: " << maxDegree << "\n";

  // Link quality statistics
  double avgFidelity = 0.0;
  double avgSuccessRate = 0.0;
  for (const auto &entry : m_linkProperties)
    {
      avgFidelity += entry.second.first;
      avgSuccessRate += entry.second.second;
    }
  avgFidelity /= m_linkProperties.size ();
  avgSuccessRate /= m_linkProperties.size ();

  std::cout << "Average Link Fidelity: " << std::setprecision (3) << avgFidelity << "\n";
  std::cout << "Average Link Success Rate: " << std::setprecision (3) << avgSuccessRate
            << "\n";
  std::cout << "===========================\n";
}

void
QuantumTopologyHelper::ExportToFile (const std::string &filename) const
{
  std::ofstream file (filename);
  if (!file.is_open ())
    {
      NS_LOG_ERROR ("Failed to open file: " << filename);
      return;
    }

  // Write nodes with positions
  file << "# Nodes: id, x, y\n";
  for (const auto &entry : m_nodePositions)
    {
      file << entry.first << ", " << entry.second.first << ", " << entry.second.second
           << "\n";
    }

  // Write edges with properties
  file << "\n# Edges: node1, node2, fidelity, success_rate\n";
  for (const auto &entry : m_linkProperties)
    {
      file << entry.first.first << ", " << entry.first.second << ", " << entry.second.first
           << ", " << entry.second.second << "\n";
    }

  file.close ();
  NS_LOG_INFO ("Topology exported to " << filename);
}

} // namespace ns3
