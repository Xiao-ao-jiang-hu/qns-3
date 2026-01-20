#include "expected-throughput-metric.h"
#include "ns3/log.h"
#include "ns3/quantum-channel.h"

NS_LOG_COMPONENT_DEFINE("ExpectedThroughputMetric");

namespace ns3 {

ExpectedThroughputMetric::ExpectedThroughputMetric()
  : m_linkSuccessRate(0.95)
{
  NS_LOG_LOGIC("Creating ExpectedThroughputMetric");
}

ExpectedThroughputMetric::~ExpectedThroughputMetric()
{
  NS_LOG_LOGIC("Destroying ExpectedThroughputMetric");
}

TypeId
ExpectedThroughputMetric::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ExpectedThroughputMetric")
    .SetParent<QuantumMetric>()
    .AddConstructor<ExpectedThroughputMetric>();
  return tid;
}

double
ExpectedThroughputMetric::CalculateChannelCost(Ptr<QuantumChannel> channel)
{
  // Simplified implementation: assume we can get success probability and generation rate
  // Actual implementation would need to get these parameters from QuantumChannel
  double successProbability = m_linkSuccessRate;
  double generationRate = 10.0; // Default entanglement generation rate (pairs/second)
  
  double expectedThroughput = successProbability * generationRate;
  
  // Cost is inversely proportional to expected throughput
  return (expectedThroughput > 0) ? (1.0 / expectedThroughput) : 1000.0;
}

double
ExpectedThroughputMetric::CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path)
{
  if (path.empty())
  {
    return 0.0;
  }
  
  // Find the worst channel in the path (highest cost)
  double maxCost = 0.0;
  for (const auto& channel : path)
  {
    double channelCost = CalculateChannelCost(channel);
    if (channelCost > maxCost)
    {
      maxCost = channelCost;
    }
  }
  
  // Path cost is determined by the worst link
  return maxCost;
}

std::string
ExpectedThroughputMetric::GetName() const
{
  return "ExpectedThroughputMetric";
}

bool
ExpectedThroughputMetric::IsAdditive() const
{
  return false; // Expected throughput is non-additive
}

bool
ExpectedThroughputMetric::IsMonotonic() const
{
  return true; // Higher throughput is better (lower cost is better)
}

void
ExpectedThroughputMetric::SetLinkSuccessRate(double successRate)
{
  m_linkSuccessRate = successRate;
}

double
ExpectedThroughputMetric::GetLinkSuccessRate() const
{
  return m_linkSuccessRate;
}

} // namespace ns3