#include "ns3/quantum-basis.h"
#include "ns3/quantum-metric.h"
#include "ns3/quantum-channel.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumMetric");

// 内部默认度量实现类
class FidelityMetric : public QuantumMetric
{
public:
  FidelityMetric() {}
  
  double CalculateChannelCost(Ptr<QuantumChannel> channel) override
  {
    // 假设通道有保真度属性，这里使用简单转换
    // 成本 = 1 - 保真度（保真度越高，成本越低）
    // 实际实现需要从通道获取保真度
    return 1.0 - 0.95; // 默认保真度0.95
  }
  
  double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) override
  {
    if (path.empty()) return 0.0;
    
    // 对于保真度，路径成本 = 1 - 乘积(通道保真度)
    double productFidelity = 1.0;
    for (const auto& channel : path)
    {
      double channelFidelity = 1.0 - CalculateChannelCost(channel);
      productFidelity *= channelFidelity;
    }
    return 1.0 - productFidelity;
  }
  
  std::string GetName() const override
  {
    return "FidelityMetric";
  }
  
  bool IsAdditive() const override
  {
    return false; // 保真度是乘性的，不是加性的
  }
  
  bool IsMonotonic() const override
  {
    return true; // 保真度是单调的（更高更好）
  }
  
  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("ns3::FidelityMetric")
      .SetParent<QuantumMetric>()
      .AddConstructor<FidelityMetric>();
    return tid;
  }
};

class DelayMetric : public QuantumMetric
{
public:
  DelayMetric() {}
  
  double CalculateChannelCost(Ptr<QuantumChannel> channel) override
  {
    // 假设通道有延迟属性，这里返回固定值
    // 实际实现需要从通道获取延迟
    return 0.01; // 10ms延迟
  }
  
  double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) override
  {
    double totalDelay = 0.0;
    for (const auto& channel : path)
    {
      totalDelay += CalculateChannelCost(channel);
    }
    return totalDelay;
  }
  
  std::string GetName() const override
  {
    return "DelayMetric";
  }
  
  bool IsAdditive() const override
  {
    return true; // 延迟是加性的
  }
  
  bool IsMonotonic() const override
  {
    return true; // 延迟是单调的（更低更好）
  }
  
  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("ns3::DelayMetric")
      .SetParent<QuantumMetric>()
      .AddConstructor<DelayMetric>();
    return tid;
  }
};

class ErrorRateMetric : public QuantumMetric
{
public:
  ErrorRateMetric() {}
  
  double CalculateChannelCost(Ptr<QuantumChannel> channel) override
  {
    // 假设通道有错误率属性
    return 0.001; // 0.1%错误率
  }
  
  double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) override
  {
    if (path.empty()) return 0.0;
    
    // 对于错误率，路径成本 = 1 - 乘积(1 - 错误率)
    double productSuccess = 1.0;
    for (const auto& channel : path)
    {
      double errorRate = CalculateChannelCost(channel);
      productSuccess *= (1.0 - errorRate);
    }
    return 1.0 - productSuccess;
  }
  
  std::string GetName() const override
  {
    return "ErrorRateMetric";
  }
  
  bool IsAdditive() const override
  {
    return false; // 错误率是乘性的
  }
  
  bool IsMonotonic() const override
  {
    return true; // 错误率是单调的（更低更好）
  }
  
  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("ns3::ErrorRateMetric")
      .SetParent<QuantumMetric>()
      .AddConstructor<ErrorRateMetric>();
    return tid;
  }
};

class CompositeMetric : public QuantumMetric
{
private:
  std::vector<Ptr<QuantumMetric>> m_metrics;
  std::vector<double> m_weights;
  
public:
  CompositeMetric()
    : m_metrics(), m_weights()
  {}

  CompositeMetric(const std::vector<Ptr<QuantumMetric>>& metrics,
                 const std::vector<double>& weights)
    : m_metrics(metrics), m_weights(weights)
  {
    NS_ASSERT(metrics.size() == weights.size());
  }
  
  double CalculateChannelCost(Ptr<QuantumChannel> channel) override
  {
    double totalCost = 0.0;
    for (size_t i = 0; i < m_metrics.size(); ++i)
    {
      totalCost += m_weights[i] * m_metrics[i]->CalculateChannelCost(channel);
    }
    return totalCost;
  }
  
  double CalculatePathCost(const std::vector<Ptr<QuantumChannel>>& path) override
  {
    double totalCost = 0.0;
    for (size_t i = 0; i < m_metrics.size(); ++i)
    {
      totalCost += m_weights[i] * m_metrics[i]->CalculatePathCost(path);
    }
    return totalCost;
  }
  
  std::string GetName() const override
  {
    std::string name = "CompositeMetric[";
    for (size_t i = 0; i < m_metrics.size(); ++i)
    {
      if (i > 0) name += "+";
      name += m_metrics[i]->GetName();
    }
    name += "]";
    return name;
  }
  
  bool IsAdditive() const override
  {
    // 只有当所有子度量都是加性时，复合度量才是加性的
    for (const auto& metric : m_metrics)
    {
      if (!metric->IsAdditive()) return false;
    }
    return true;
  }
  
  bool IsMonotonic() const override
  {
    // 只有当所有子度量都是单调时，复合度量才是单调的
    for (const auto& metric : m_metrics)
    {
      if (!metric->IsMonotonic()) return false;
    }
    return true;
  }
  
  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("ns3::CompositeMetric")
      .SetParent<QuantumMetric>()
      .AddConstructor<CompositeMetric>();
    return tid;
  }
};

// QuantumMetric静态方法实现
Ptr<QuantumMetric> QuantumMetric::CreateFidelityMetric()
{
  return CreateObject<FidelityMetric>();
}

Ptr<QuantumMetric> QuantumMetric::CreateDelayMetric()
{
  return CreateObject<DelayMetric>();
}

Ptr<QuantumMetric> QuantumMetric::CreateErrorRateMetric()
{
  return CreateObject<ErrorRateMetric>();
}

Ptr<QuantumMetric> QuantumMetric::CreateCompositeMetric(
    const std::vector<Ptr<QuantumMetric>>& metrics,
    const std::vector<double>& weights)
{
  return CreateObject<CompositeMetric>(metrics, weights);
}

Ptr<QuantumMetric> QuantumMetric::GetDefaultMetric()
{
  // 默认使用保真度度量
  return CreateFidelityMetric();
}

// QuantumMetric基类TypeId
NS_OBJECT_ENSURE_REGISTERED (QuantumMetric);

TypeId
QuantumMetric::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::QuantumMetric")
    .SetParent<Object>()
    .SetGroupName("Quantum");
  return tid;
}

} // namespace ns3