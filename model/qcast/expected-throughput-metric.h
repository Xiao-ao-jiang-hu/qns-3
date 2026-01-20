#ifndef EXPECTED_THROUGHPUT_METRIC_H
#define EXPECTED_THROUGHPUT_METRIC_H

#include "ns3/quantum-metric.h"

namespace ns3 {

/**
 * \brief Expected throughput metric for Q-CAST protocol
 * 
 * This metric implements the E_t (expected throughput) metric used in
 * the Q-CAST protocol. It is a non-additive metric that calculates
 * the expected throughput of quantum links.
 */
class ExpectedThroughputMetric : public QuantumMetric
{
public:
  ExpectedThroughputMetric();
  ~ExpectedThroughputMetric();

  static TypeId GetTypeId(void);

  // QuantumMetric interface implementation
  double CalculateChannelCost(Ptr<QuantumChannel> channel) override;
  double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) override;
  std::string GetName() const override;
  bool IsAdditive() const override;
  bool IsMonotonic() const override;

  /**
   * \brief Set the link success rate
   * \param successRate The success rate of quantum links
   */
  void SetLinkSuccessRate(double successRate);

  /**
   * \brief Get the link success rate
   * \return The current link success rate
   */
  double GetLinkSuccessRate() const;

private:
  double m_linkSuccessRate; ///< Success rate of quantum links
  double m_alpha;           ///< Alpha parameter for swap success probability (default 0.1)
  
  /**
   * \brief Calculate swap success probability S(h)
   * \param hopCount Number of hops (path length)
   * \return Swap success probability
   */
  double CalculateSwapSuccessProbability(size_t hopCount) const;
};

} // namespace ns3

#endif // EXPECTED_THROUGHPUT_METRIC_H