#ifndef QUANTUM_ROUTING_METRIC_H
#define QUANTUM_ROUTING_METRIC_H

#include "ns3/object.h"
#include "ns3/quantum-routing-protocol.h"

#include <map>
#include <string>
#include <vector>

namespace ns3 {

class QuantumNetworkLayer;

struct QuantumRoutingSegment
{
    std::string leftNode;
    std::string rightNode;
    double initialFidelity;
    BellPairNoiseFamily noiseFamily;
    double setupTimeMs;
    double leftTauMs;
    double rightTauMs;

    QuantumRoutingSegment ()
        : leftNode (""),
          rightNode (""),
          initialFidelity (1.0),
          noiseFamily (BellPairNoiseFamily::WERNER),
          setupTimeMs (0.0),
          leftTauMs (1e12),
          rightTauMs (1e12)
    {
    }
};

struct QuantumRoutingLabel
{
    std::vector<std::string> path;
    std::vector<QuantumRoutingSegment> segments;
    std::map<std::string, double> scalars;
};

class QuantumRoutingMetric : public Object
{
public:
    static TypeId GetTypeId ();

    QuantumRoutingMetric ();
    ~QuantumRoutingMetric () override;

    virtual QuantumRoutingLabel CreateInitialLabel (const std::string &srcNode) const;

    virtual bool ExtendLabel (QuantumNetworkLayer* networkLayer,
                              const QuantumRoutingLabel &current,
                              const std::string &nextNode,
                              const LinkMetrics &linkAttributes,
                              QuantumRoutingLabel &extended) const = 0;

    virtual bool IsBetter (const QuantumRoutingLabel &lhs,
                           const QuantumRoutingLabel &rhs) const = 0;

    virtual bool Dominates (const QuantumRoutingLabel &lhs,
                            const QuantumRoutingLabel &rhs) const = 0;

    virtual double GetScore (const QuantumRoutingLabel &label) const = 0;
};

class BottleneckFidelityRoutingMetric : public QuantumRoutingMetric
{
public:
    static TypeId GetTypeId ();

    BottleneckFidelityRoutingMetric ();
    ~BottleneckFidelityRoutingMetric () override;

    QuantumRoutingLabel CreateInitialLabel (const std::string &srcNode) const override;

    bool ExtendLabel (QuantumNetworkLayer* networkLayer,
                      const QuantumRoutingLabel &current,
                      const std::string &nextNode,
                      const LinkMetrics &linkAttributes,
                      QuantumRoutingLabel &extended) const override;

    bool IsBetter (const QuantumRoutingLabel &lhs,
                   const QuantumRoutingLabel &rhs) const override;

    bool Dominates (const QuantumRoutingLabel &lhs,
                    const QuantumRoutingLabel &rhs) const override;

    double GetScore (const QuantumRoutingLabel &label) const override;

    double ResolveLinkInitialFidelity (const LinkMetrics &attributes) const;
    BellPairNoiseFamily ResolveLinkNoiseFamily (const LinkMetrics &attributes) const;
    double ResolveLinkReadyTimeMs (const LinkMetrics &attributes) const;
    double ResolveNodeCoherenceTimeMs (QuantumNetworkLayer* networkLayer,
                                       const std::string &nodeName) const;

private:
    double GetScalar (const QuantumRoutingLabel &label,
                      const std::string &name,
                      double fallback = 0.0) const;

    bool m_useCurrentLinkDelay;
};

} // namespace ns3

#endif /* QUANTUM_ROUTING_METRIC_H */
