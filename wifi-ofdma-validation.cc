/*
 * Copyright (c) 2015 Sébastien Deronne
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * Author: Sébastien Deronne <sebastien.deronne@gmail.com>
 */

#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/he-phy.h"
#include "ns3/mobility-helper.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/udp-server.h"
#include "ns3/uinteger.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SimplesHtHiddenStations");

int main(int argc, char* argv[])
{
    uint32_t payloadSize = 1472; // bytes
    double simulationTime = 10;  // seconds
    uint32_t nMpdus = 1;
    uint32_t maxAmpduSize = 0;
    int mcs = 11;
    int channelWidth = 80;
    bool enableRts = true;
    double minExpectedThroughput = 0;
    double maxExpectedThroughput = 0;

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(5);

    CommandLine cmd(__FILE__);
    cmd.AddValue("nMpdus", "Number of aggregated MPDUs", nMpdus);
    cmd.AddValue("payloadSize", "Payload size in bytes", payloadSize);
    cmd.AddValue("enableRts", "Enable RTS/CTS", enableRts);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("minExpectedThroughput",
                 "if set, simulation fails if the lowest throughput is below this value",
                 minExpectedThroughput);
    cmd.AddValue("maxExpectedThroughput",
                 "if set, simulation fails if the highest throughput is above this value",
                 maxExpectedThroughput);
    cmd.Parse(argc, argv);

    if (!enableRts)
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("999999"));
    }
    else
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
        Config::SetDefault("ns3::WifiDefaultProtectionManager::EnableMuRts", BooleanValue(true));
    }

    // Set the maximum size for A-MPDU with regards to the payload size
    maxAmpduSize = nMpdus * (payloadSize + 200);

    // Set the maximum wireless range to 5 meters in order to reproduce a hidden nodes scenario,
    // i.e. the distance between hidden stations is larger than 5 meters
    Config::SetDefault("ns3::RangePropagationLossModel::MaxRange", DoubleValue(5));
    int numStats = 4;
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(numStats);
    NodeContainer wifiApNode;
    wifiApNode.Create(1);

    NetDeviceContainer apDevice;
    NetDeviceContainer staDevices;
    WifiMacHelper mac;
    WifiHelper wifi;
    std::string channelStr("{0, " + std::to_string(channelWidth) + ", ");
    StringValue ctrlRate;
    auto nonHtRefRateMbps = HePhy::GetNonHtReferenceRate(mcs) / 1e6;

    std::ostringstream ossDataMode;
    ossDataMode << "HeMcs" << mcs;

    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlMode;
    ossControlMode << "OfdmRate" << nonHtRefRateMbps << "Mbps";
    ctrlRate = StringValue(ossControlMode.str());
    channelStr += "BAND_5GHZ, 0}";

    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                    "DataMode",
                                    StringValue(ossDataMode.str()),
                                    "ControlMode",
                                    ctrlRate);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    phy.SetChannel(channel.Create());

    Ssid ssid = Ssid("projekt-bws");

    mac.SetType("ns3::StaWifiMac",
                "Ssid",
                SsidValue(ssid));
    phy.Set("ChannelSettings", StringValue(channelStr));
    staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "EnableBeaconJitter",
                BooleanValue(false),
                "Ssid",
                SsidValue(ssid));
    apDevice = wifi.Install(phy, mac, wifiApNode);

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(maxAmpduSize));

    // Setting mobility model
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    // AP is between the two stations, each station being located at 5 meters from the AP.
    // The distance between the two stations is thus equal to 10 meters.
    // Since the wireless range is limited to 5 meters, the two stations are hidden from each other.
    positionAlloc->Add(Vector(5.0, 5.0, 0.0));
    positionAlloc->Add(Vector(5.0, 10.0, 0.0));
    positionAlloc->Add(Vector(10.0, 5.0, 0.0));
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));
    positionAlloc->Add(Vector(0.0, 5.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    mobility.Install(wifiApNode);
    mobility.Install(wifiStaNodes);

    // Internet stack
    InternetStackHelper stack;
    stack.Install(wifiApNode);
    stack.Install(wifiStaNodes);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer StaInterface;
    StaInterface = address.Assign(staDevices);
    Ipv4InterfaceContainer ApInterface;
    ApInterface = address.Assign(apDevice);

    // Setting applications
    uint16_t port = 9;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(wifiApNode);
    serverApp.Start(Seconds(0.0));
    serverApp.Stop(Seconds(simulationTime + 1));

    UdpClientHelper client(ApInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
    client.SetAttribute("Interval", TimeValue(Time("0.0001"))); // packets/s
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));

    // Saturated UDP traffic from stations to AP
    ApplicationContainer clientApp1 = client.Install(wifiStaNodes);
    clientApp1.Start(Seconds(1.0));
    clientApp1.Stop(Seconds(simulationTime + 1));

    phy.EnablePcap("SimpleHtHiddenStations_Ap", apDevice.Get(0));
    phy.EnablePcap("SimpleHtHiddenStations_Sta1", staDevices.Get(0));
    phy.EnablePcap("SimpleHtHiddenStations_Sta2", staDevices.Get(1));
    AsciiTraceHelper ascii;
    phy.EnableAsciiAll(ascii.CreateFileStream("SimpleHtHiddenStations.tr"));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simulationTime + 1));

    Simulator::Run();

    uint64_t totalPacketsThrough = DynamicCast<UdpServer>(serverApp.Get(0))->GetReceived();

    float packetsDropped;
    float totalPackagesDropped = 0;    

    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (int i = 1; i <= numStats; i++) {
        totalPackagesDropped = totalPackagesDropped + stats[i].packetsDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
        packetsDropped = stats[i].packetsDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
        std::cout << "Station " << i << " droped packages:\t\t\t" << packetsDropped/1000<< "%\t\t" <<  packetsDropped << std::endl;
    }
    std::cout << "Total droped packages:\t\t\t" <<  totalPackagesDropped/1000/numStats << "%\t\t" << totalPackagesDropped << std::endl; 

    Simulator::Destroy();

    double throughput = totalPacketsThrough * payloadSize * 8 / (simulationTime * 1000000.0);
    std::cout << "Throughput: " << throughput << " Mbit/s" << '\n';
    if (throughput < minExpectedThroughput ||
        (maxExpectedThroughput > 0 && throughput > maxExpectedThroughput))
    {
        NS_LOG_ERROR("Obtained throughput " << throughput << " is not in the expected boundaries!");
        exit(1);
    }
    return 0;
}
