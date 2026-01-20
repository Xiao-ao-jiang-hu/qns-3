#include "ns3/quantum-net-stack-helper.h"

#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h" // class QuantumNetworkSimulator
#include "ns3/quantum-node.h" // class QuantumNode
#include "ns3/quantum-phy-entity.h" // class QuantumPhyEntity
#include "ns3/distribute-epr-helper.h" // class DistributeEPRSrcHelper, DistributeEPRDstHelper
#include "ns3/quantum-network-layer.h"
#include "ns3/quantum-resource-manager.h"
#include "ns3/quantum-forwarding-engine.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/quantum-metric.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumNetStackHelper");

QuantumNetStackHelper::QuantumNetStackHelper ()
{
}
QuantumNetStackHelper::~QuantumNetStackHelper ()
{
}

void
QuantumNetStackHelper::Install (NodeContainer c) const
{
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      for (NodeContainer::Iterator j = c.Begin (); j != c.End (); ++j)
        {
          if (i != j)
            {
              Install (DynamicCast<QuantumNode> (*i), DynamicCast<QuantumNode> (*j));
            }
        }
    }
}

void
QuantumNetStackHelper::Install (Ptr<QuantumNode> alice, Ptr<QuantumNode> bob) const
{
  // 安装网络层（如果尚未安装）
  InstallNetworkLayer (alice);
  InstallNetworkLayer (bob);
  
  Ptr<QuantumChannel> qconn = CreateObject<QuantumChannel> (alice, bob);

  NS_LOG_LOGIC ("Installing quantum network stack for " << alice->GetOwner () << " and "
                                                        << bob->GetOwner ());
  Ptr<QuantumPhyEntity> qphyent = alice->GetQuantumPhyEntity ();

  DistributeEPRSrcHelper srcHelper (qphyent, qconn);

  ApplicationContainer srcApps = srcHelper.Install (alice);
  srcApps.Start (Seconds (0.));
  srcApps.Stop (Seconds (ETERNITY));

  DistributeEPRDstHelper dstHelper (qphyent, qconn);

  ApplicationContainer dstApps = dstHelper.Install (bob);
  dstApps.Start (Seconds (0.));
  dstApps.Stop (Seconds (ETERNITY));

  qphyent->AddConn2Apps (qconn, APP_DIST_EPR, {srcApps.Get (0), dstApps.Get (0)});
  
  // 将通道添加到网络层的邻居列表
  Ptr<QuantumNetworkLayer> aliceLayer = alice->GetQuantumNetworkLayer ();
  Ptr<QuantumNetworkLayer> bobLayer = bob->GetQuantumNetworkLayer ();
  if (aliceLayer)
  {
    aliceLayer->AddNeighbor (qconn);
  }
  if (bobLayer)
  {
    bobLayer->AddNeighbor (qconn);
  }
}

void
QuantumNetStackHelper::InstallNetworkLayer (Ptr<QuantumNode> node) const
{
  // 如果节点已经有网络层，则跳过
  if (node->GetQuantumNetworkLayer ())
  {
    NS_LOG_LOGIC ("Node " << node->GetOwner () << " already has a quantum network layer");
    return;
  }
  
  NS_LOG_LOGIC ("Installing quantum network layer on node " << node->GetOwner ());
  
  // 创建网络层
  Ptr<QuantumNetworkLayer> networkLayer = CreateObject<QuantumNetworkLayer> ();
  
  // 设置节点地址（使用owner名称）
  networkLayer->SetAddress (node->GetOwner ());
  networkLayer->SetQuantumNode (node);
  
  // 创建并设置资源管理器
  Ptr<QuantumResourceManager> resourceManager = QuantumResourceManager::GetDefaultResourceManager ();
  networkLayer->SetResourceManager (resourceManager);
  
  // 创建并设置转发引擎
  Ptr<QuantumForwardingEngine> forwardingEngine = QuantumForwardingEngine::GetDefaultForwardingEngine ();
  forwardingEngine->SetResourceManager (resourceManager);
  networkLayer->SetForwardingEngine (forwardingEngine);
  
  // 创建并设置路由协议
  Ptr<QuantumRoutingProtocol> routingProtocol = QuantumRoutingProtocol::GetDefaultRoutingProtocol ();
  routingProtocol->SetNetworkLayer (networkLayer);
  routingProtocol->SetResourceManager (resourceManager);
  routingProtocol->SetMetric (QuantumMetric::GetDefaultMetric ());
  networkLayer->SetRoutingProtocol (routingProtocol);
  
  // 将网络层附加到节点
  node->SetQuantumNetworkLayer (networkLayer);
  
  NS_LOG_LOGIC ("Quantum network layer installed successfully on node " << node->GetOwner ());
}

} // namespace ns3
