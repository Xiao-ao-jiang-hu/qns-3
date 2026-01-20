#ifndef QUANTUM_METRIC_H
#define QUANTUM_METRIC_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"

#include <string>
#include <vector>
#include <memory>

namespace ns3 {

// 前向声明
class QuantumChannel;

/**
 * \brief Abstract base class for quantum network metrics.
 * 
 * This class defines the interface for computing costs of quantum
 * channels and paths. Different metrics can be implemented by
 * subclassing this class.
 */
class QuantumMetric : public Object
{
public:
  /**
   * \brief Calculate the cost of a quantum channel.
   * 
   * \param channel The quantum channel to evaluate
   * \return The cost of the channel (lower is better)
   */
  virtual double CalculateChannelCost(Ptr<QuantumChannel> channel) = 0;
  
  /**
   * \brief Calculate the total cost of a path.
   * 
   * \param path The sequence of channels forming the path
   * \return The total cost of the path
   */
  virtual double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) = 0;
  
  /**
   * \brief Get the name of this metric.
   * 
   * \return The metric name
   */
  virtual std::string GetName() const = 0;
  
  /**
   * \brief Check if this metric is additive.
   * 
   * Additive metrics can be summed along a path (e.g., delay).
   * Non-additive metrics require special handling (e.g., fidelity).
   * 
   * \return true if the metric is additive, false otherwise
   */
  virtual bool IsAdditive() const = 0;
  
  /**
   * \brief Check if this metric is monotonic.
   * 
   * Monotonic metrics always improve (decrease) when adding
   * a better channel to a path (e.g., fidelity).
   * 
   * \return true if the metric is monotonic, false otherwise
   */
  virtual bool IsMonotonic() const = 0;
  
  /**
   * \brief Get the TypeId.
   * 
   * \return The TypeId
   */
  static TypeId GetTypeId(void);

  /**
   * \brief Create a fidelity-based metric.
   * 
   * \return A metric that computes cost based on fidelity
   */
  static Ptr<QuantumMetric> CreateFidelityMetric();
  
  /**
   * \brief Create a delay-based metric.
   * 
   * \return A metric that computes cost based on delay
   */
  static Ptr<QuantumMetric> CreateDelayMetric();
  
  /**
   * \brief Create an error-rate-based metric.
   * 
   * \return A metric that computes cost based on error rate
   */
  static Ptr<QuantumMetric> CreateErrorRateMetric();
  
  /**
   * \brief Create a composite metric from multiple metrics.
   * 
   * \param metrics The metrics to combine
   * \param weights The weights for each metric
   * \return A composite metric
   */
  static Ptr<QuantumMetric> CreateCompositeMetric(
      const std::vector<Ptr<QuantumMetric>>& metrics,
      const std::vector<double>& weights);
  
  /**
   * \brief Get a default metric for testing.
   * 
   * \return A default metric (usually fidelity-based)
   */
  static Ptr<QuantumMetric> GetDefaultMetric();
};

} // namespace ns3

#endif /* QUANTUM_METRIC_H */