#ifndef SLICED_DIJKSTRA_ROUTING_PROTOCOL_H
#define SLICED_DIJKSTRA_ROUTING_PROTOCOL_H

#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-routing-metric.h"
#include "ns3/quantum-routing-protocol.h"

#include <map>
#include <string>
#include <vector>

namespace ns3 {

class SlicedDijkstraRoutingProtocol : public QuantumRoutingProtocol
{
public:
    static TypeId GetTypeId (void);

    SlicedDijkstraRoutingProtocol ();
    ~SlicedDijkstraRoutingProtocol () override;

    void DoDispose (void) override;

    void Initialize (void) override;
    void SetNetworkLayer (QuantumNetworkLayer* netLayer) override;
    std::vector<std::string> CalculateRoute (const std::string &src,
                                             const std::string &dst) override;
    void NotifyTopologyChange (void) override;
    std::string RouteToString (const std::vector<std::string> &route) override;

    void UpdateTopology (
        const std::map<std::string, std::map<std::string, LinkMetrics>> &topology) override;
    void AddNeighbor (const std::string &node, const std::string &neighbor,
                      const LinkMetrics &metrics) override;
    void RemoveNeighbor (const std::string &node, const std::string &neighbor) override;
    void UpdateLinkMetrics (const std::string &node, const std::string &neighbor,
                            const LinkMetrics &metrics) override;
    bool HasRoute (const std::string &src, const std::string &dst) override;
    double GetRouteMetric (const std::string &src, const std::string &dst) override;

    std::vector<QuantumRoutingLabel> GetNodeLabels (const std::string &node) const;

protected:
    /**
     * \brief Multi-label best-first search with optional Tmax bucket slicing.
     *
     * Each node may retain up to K labels. When bucket slicing is enabled,
     * labels are diversified along the metric-provided `t_max_ms` axis so that
     * fast and slow prefixes can coexist at the same frontier node.
     */
    QuantumRoutingLabel RunMultiLabelSearch (
        const std::string &src,
        const std::string &dst,
        std::map<std::string, std::vector<QuantumRoutingLabel>> &labelsByNode);

    bool AdmitLabel (const std::string &node,
                     const QuantumRoutingLabel &candidate,
                     std::map<std::string, std::vector<QuantumRoutingLabel>> &labelsByNode) const;

    int64_t GetBucketId (const QuantumRoutingLabel &state) const;

    std::map<std::string, std::map<std::string, LinkMetrics>> m_topology;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> m_routes;
    std::map<std::string, std::map<std::string, double>> m_routeMetrics;
    std::map<std::string, std::vector<QuantumRoutingLabel>> m_lastLabelsByNode;

    uint32_t m_k;
    double m_bucketWidthMs;
    bool m_useBuckets;
};

} // namespace ns3

#endif /* SLICED_DIJKSTRA_ROUTING_PROTOCOL_H */
