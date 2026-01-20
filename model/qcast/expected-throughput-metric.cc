#include "expected-throughput-metric.h"
#include "ns3/log.h"
#include "ns3/quantum-channel.h"
#include <cmath>

NS_LOG_COMPONENT_DEFINE("ExpectedThroughputMetric");

namespace ns3 {

ExpectedThroughputMetric::ExpectedThroughputMetric()
  : m_linkSuccessRate(0.95),
    m_alpha(0.1)  // Default alpha for Q-CAST swap success probability
{
  NS_LOG_LOGIC("Creating ExpectedThroughputMetric with alpha=" << m_alpha);
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
  // For a single link (1 hop), expected throughput = link success probability
  // In Q-CAST: E_t(link) = p_i (link success probability)
  // We need to get actual fidelity from QuantumChannel, but for now use default
  double linkSuccessProbability = m_linkSuccessRate;
  
  // For single hop, swap success probability S(1) = 1.0
  double swapSuccess = 1.0;
  
  // Expected throughput for single link: E_t = p_i × S(1) = p_i
  double expectedThroughput = linkSuccessProbability * swapSuccess;
  
  // Cost is inversely proportional to expected throughput
  // Higher expected throughput = lower cost
  double cost = (expectedThroughput > 0) ? (1.0 / expectedThroughput) : 1000.0;
  
  NS_LOG_LOGIC("Channel cost calculation: p=" << linkSuccessProbability 
               << ", E_t=" << expectedThroughput << ", cost=" << cost);
  return cost;
}

double
ExpectedThroughputMetric::CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path)
{
  if (path.empty())
  {
    return 0.0;
  }
  
  size_t hopCount = path.size();
  
  // Calculate product of link success probabilities
  // E_t(P) = (∏ p_i) × S(h) where h = hop count
  double productLinkSuccess = 1.0;
  
  for (const auto& channel : path)
  {
    // Get link success probability for this channel
    // In a real implementation, we would get actual fidelity from channel
    double linkSuccess = m_linkSuccessRate;
    productLinkSuccess *= linkSuccess;
  }
  
  // Calculate swap success probability S(h)
  double swapSuccess = CalculateSwapSuccessProbability(hopCount);
  
  // Calculate expected throughput for the entire path
  double expectedThroughput = productLinkSuccess * swapSuccess;
  
  // Path cost is inversely proportional to expected throughput
  double cost = (expectedThroughput > 0) ? (1.0 / expectedThroughput) : 1000.0;
  
  NS_LOG_LOGIC("Path cost calculation: hops=" << hopCount 
               << ", ∏p_i=" << productLinkSuccess << ", S(h)=" << swapSuccess
               << ", E_t=" << expectedThroughput << ", cost=" << cost);
  return cost;
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

double
ExpectedThroughputMetric::CalculateSwapSuccessProbability(size_t hopCount) const
{
  if (hopCount <= 1)
  {
    return 1.0; // No swap needed for 0 or 1 hop
  }
  
  // S(h) ≈ exp(-α·log₂ h) according to Q-CAST specification
  // where α = 0.1 by default
  double log2h = std::log2(static_cast<double>(hopCount));
  double swapSuccess = std::exp(-m_alpha * log2h);
  
  NS_LOG_LOGIC("Swap success probability for " << hopCount << " hops: " << swapSuccess);
  return swapSuccess;
}

} // namespace ns3