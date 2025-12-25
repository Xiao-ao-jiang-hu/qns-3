#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/quantum-dijkstra-routing.h"
#include "ns3/quantum-l3-protocol.h"
#include "ns3/quantum-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("QuantumRoutingExample");

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    LogComponentEnable("QuantumRoutingExample", LOG_LEVEL_INFO);
    LogComponentEnable("QuantumL3Protocol", LOG_LEVEL_ALL);
    LogComponentEnable("QuantumDijkstraRouting", LOG_LEVEL_ALL);

    NS_LOG_INFO("Creating Nodes...");
    NodeContainer nodes;
    nodes.Create(4); // 0, 1, 2, 3

    NS_LOG_INFO("Installing Internet Stack...");
    InternetStackHelper internet;
    internet.Install(nodes);

    // Setup IP addresses for the topology
    // 0-1: 10.1.1.0
    // 1-3: 10.1.3.0
    // 0-2: 10.1.2.0
    // 2-3: 10.1.4.0

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    Ipv4AddressHelper address;

    // Link 0-1
    NetDeviceContainer d01 = p2p.Install(nodes.Get(0), nodes.Get(1));
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i01 = address.Assign(d01);

    // Link 1-3
    NetDeviceContainer d13 = p2p.Install(nodes.Get(1), nodes.Get(3));
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i13 = address.Assign(d13);

    // Link 0-2
    NetDeviceContainer d02 = p2p.Install(nodes.Get(0), nodes.Get(2));
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i02 = address.Assign(d02);

    // Link 2-3
    NetDeviceContainer d23 = p2p.Install(nodes.Get(2), nodes.Get(3));
    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i23 = address.Assign(d23);

    NS_LOG_INFO("Setting up Quantum Network Layer...");

    // Create and configure Dijkstra Routing
    Ptr<QuantumDijkstraRouting> routing = CreateObject<QuantumDijkstraRouting>();

    Ipv4Address addr0 = i01.GetAddress(0);
    Ipv4Address addr1 = i01.GetAddress(1);
    Ipv4Address addr2 = i02.GetAddress(1);
    Ipv4Address addr3 = i13.GetAddress(1);

    // Add links to the routing protocol (Global knowledge for this example)
    routing->AddLink(addr0, addr1, 1.0); // 0-1
    routing->AddLink(addr1, addr3, 1.0); // 1-3
    routing->AddLink(addr0, addr2, 0.5); // 0-2
    routing->AddLink(addr2, addr3, 0.5); // 2-3

    // Install QuantumL3Protocol on all nodes
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<QuantumL3Protocol> ql3 = CreateObject<QuantumL3Protocol>();
        ql3->SetRoutingProtocol(
            routing); // In this simple example, they share the same routing object
        nodes.Get(i)->AggregateObject(ql3);
    }

    NS_LOG_INFO("Triggering Entanglement Request from Node 0 to Node 3...");

    Simulator::Schedule(Seconds(1.0),
                        &QuantumL3Protocol::RequestEntanglement,
                        nodes.Get(0)->GetObject<QuantumL3Protocol>(),
                        addr3,
                        101);

    Simulator::Stop(Seconds(5.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
