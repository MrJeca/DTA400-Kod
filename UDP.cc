/*
 * SPDX-License-Identifier: GPL-2.0-only
 */



#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"

#include <fstream>

using namespace ns3;



// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 UDP    |    |    ns-3 UDP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 2 ms
//
//
// We want to look at a simple UDP flow using a custom application.
// We create a socket and pass it into the constructor of our simple
// application which we then install in the source node.
// ===========================================================================
//

// Track total application payload bytes received successfully
static uint64_t g_totalBytesReceived = 0;

static void
SinkRxTrace(Ptr<const Packet> p, const Address &addr)
{
    g_totalBytesReceived += p->GetSize();
}

static void
RxDrop(Ptr<const Packet> p)
{
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
}

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.0005));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.));
    sinkApps.Stop(Seconds(20.));
    
    

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    sink->TraceConnectWithoutContext("Rx", MakeCallback(&SinkRxTrace));

    //Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    //app->Setup(ns3UdpSocket, sinkAddress, 1040, 1000, DataRate("1Mbps"));
    //nodes.Get(0)->AddApplication(app);
    
    OnOffHelper onoff("ns3::UdpSocketFactory", sinkAddress); 
    onoff.SetConstantRate(DataRate("1Mbps"), 1040);
    onoff.SetAttribute("MaxBytes", UintegerValue(1000 * 1040)); // Total bytes to send (1000 packets * 1040 bytes)
    ApplicationContainer app = onoff.Install(nodes.Get(0));
    app.Start(Seconds(1.0));
    app.Stop(Seconds(20.0));

    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(20));
    Simulator::Run();
    
// ==========================================
    // PRINT RESULTS AT THE END OF SIMULATION
    // ==========================================
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (auto i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        
        double duration = i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds();
        
        if (duration > 0 && i->second.rxPackets > 0)
        {
            // Total throughput (includes headers and retransmissions)
            double throughputMbps = (8.0 * i->second.rxBytes) / duration / 1024 / 1024;
            
         // Calculate Goodput based on actual received application bytes
    double appDuration = 19.0; // Simulation active time (Stop - Start = 20 - 1)
    double goodputMbps = (8.0 * g_totalBytesReceived) / appDuration / 1024 / 1024;
            
            std::cout << "  Throughput: " << throughputMbps << " Mbps\n";
            std::cout << "  Goodput: " << goodputMbps << " Mbps\n";
        }
        else
        {
            std::cout << "  Throughput: 0.00 Mbps\n";
            std::cout << "  Goodput: 0.00 Mbps\n";
        }

        // Packet Loss Ratio
        double totalPackets = i->second.txPackets + i->second.lostPackets;
        double lossRatio = (totalPackets > 0) ? ((double)i->second.lostPackets / totalPackets) * 100.0 : 0.0;
        std::cout << "  Packet Loss Ratio: " << lossRatio << "%\n";
        
        // Average End-to-End Latency
        if (i->second.rxPackets > 0)
        {
            double avgDelayMs = i->second.delaySum.GetMilliSeconds() / (double)i->second.rxPackets;
            std::cout << "  Average End-to-End Latency: " << avgDelayMs << " ms\n";
        }
        else
        {
            std::cout << "  Average End-to-End Latency: N/A (No packets received)\n";
        }
        std::cout << "--------------------------------------------------\n";
    }
    
    Simulator::Destroy();

    return 0;
}
