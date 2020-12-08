/*
 * Copyright (c) 2015-2019 IMDEA Networks Institute
 * Author: Hany Assasa <hany.assasa@gmail.com>
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "common-functions.h"


// github test
/**
 * Simulation Objective:
 * This script is used to evaluate allocation of Static Service Periods in IEEE 802.11ad.
 *
 * Network Topology:
 * The scenario consists of 2 DMG STAs (a + b) and one PCP/AP as following:
 *
 *                         DMG PCP/AP (0,1)
 *
 *
 * a DMG STA (-1,0)                      b DMG STA (1,0)
 *
 * Simulation Description:
 * Once all the stations have assoicated successfully with the PCP/AP. The PCP/AP allocates two SPs
 * to perform TxSS between all the stations. Once a DMG STA has completed TxSS phase with b DMG.
 * The PCP/AP allocates two static service periods for communication as following:
 *
 * SP1: DMG a STA -----> DMG b STA (SP Length = 3.2 ms)
 * SP2: DMG b STA -----> DMG a STA (SP Length = 3.2 ms)
 *
 * Running the Simulation:
 * To run the script with the default parameters:
 * ./waf --run "evaluate_service_period_tcp"
 *
 * To run the script with different duration for the service period e.g. SP1=10ms:
 * ./waf --run "evaluate_service_period_tcp --spDuration=10000"
 *
 * Simulation Output:
 * The simulation generates the following traces:
 * 1. PCAP traces for each station. From the PCAP files, we can see that data transmission takes place
 * during its SP. In addition, we can notice in the announcement of the two Static Allocation Periods
 * inside each DMG Beacon.
 */

NS_LOG_COMPONENT_DEFINE ("EvaluateServicePeriod");

using namespace ns3;
using namespace std;

Ptr<PacketSink> packetSink;
Ptr<PacketSink> packetSink2;

/* Network Nodes */
Ptr<WifiNetDevice> apWifiNetDevice;
Ptr<WifiNetDevice> aWifiNetDevice;
Ptr<WifiNetDevice> bWifiNetDevice;
Ptr<WifiNetDevice> cWifiNetDevice;
Ptr<WifiNetDevice> dWifiNetDevice;


NetDeviceContainer staDevices;

Ptr<DmgApWifiMac> apWifiMac;
Ptr<DmgStaWifiMac> aWifiMac;
Ptr<DmgStaWifiMac> bWifiMac;
Ptr<DmgStaWifiMac> cWifiMac;
Ptr<DmgStaWifiMac> dWifiMac;


string channel_access_scheme = "CBAP";
/*** Access Point Variables ***/
uint8_t assoicatedStations = 0;           /* Total number of assoicated stations with the AP */
uint8_t stationsTrained = 0;              /* Number of BF trained stations */
bool scheduledStaticPeriods = true;      /* Flag to indicate whether we scheduled Static Service Periods or not */



//if(channel_access_scheme == "CBAP")
// {
//std::cout << "-------------------- " << "Channel Access Scheme Selected CBAP" << "-------------------- "  << std::endl;
//  bool scheduledStaticPeriods = true;      /* Flag to indicate whether we scheduled Static Service Periods or not */
// }
//else
// {
//std::cout << "-------------------- " << "Channel Access Scheme Selected SP" << "-------------------- "  << std::endl;
//  bool scheduledStaticPeriods = false;      /* Flag to indicate whether we scheduled Static Service Periods or not */
// }

/*** Service Periods ***/
uint16_t sp1Duration = 3200;    /* The duration of the allocated service period in the forward direction in MicroSeconds */
uint16_t sp2Duration = 3200;    /* The duration of the allocated service period in the reverse direction in MicroSeconds */
uint32_t blocks = 8;

void
CalculateThroughput (Ptr<PacketSink> sink, uint64_t lastTotalRx, double averageThroughput)
{
  Time now = Simulator::Now ();                                         /* Return the simulator's virtual time. */
  double cur = (sink->GetTotalRx() - lastTotalRx) * (double) 8/1e5;     /* Convert Application RX Packets to MBits. */
  std::cout << now.GetSeconds () << '\t' << cur << std::endl;
  lastTotalRx = sink->GetTotalRx ();
  averageThroughput += cur;
  Simulator::Schedule (MilliSeconds (100), &CalculateThroughput, sink, lastTotalRx, averageThroughput);
}

void
CalculateThroughput2 (Ptr<PacketSink> sink, uint64_t lastTotalRx, double averageThroughput)
{
  Time now = Simulator::Now ();                                         /* Return the simulator's virtual time. */
  double cur = (sink->GetTotalRx() - lastTotalRx) * (double) 8/1e5;     /* Convert Application RX Packets to MBits. */
  std::cout << now.GetSeconds () << '\t' << cur << std::endl;
  lastTotalRx = sink->GetTotalRx ();
  averageThroughput += cur;
  Simulator::Schedule (MilliSeconds (100), &CalculateThroughput2, sink, lastTotalRx, averageThroughput);
}

void
StationAssoicated (Ptr<DmgStaWifiMac> staWifiMac, Mac48Address address, uint16_t aid)
{
  std::cout << "DMG STA " << staWifiMac->GetAddress () << " associated with DMG AP " << address << std::endl;
  std::cout << "Association ID (AID) = " << aid << std::endl;
  assoicatedStations++;
  /* Check if all stations have assoicated with the DMG PCP/AP */
  if (assoicatedStations == 4)
    {
      std::cout << "All stations got associated with " << address << std::endl;

      /* For simplicity we assume that each station is aware of the capabilities of the peer station */
      /* Otherwise, we have to request the capabilities of the peer station. */
      aWifiMac->StorePeerDmgCapabilities (bWifiMac);
      bWifiMac->StorePeerDmgCapabilities (aWifiMac);

      dWifiMac->StorePeerDmgCapabilities (cWifiMac);
      cWifiMac->StorePeerDmgCapabilities (dWifiMac);


      /* Schedule Beamforming Training SP */
      apWifiMac->AllocateBeamformingServicePeriod (aWifiMac->GetAssociationID (), bWifiMac->GetAssociationID (), 0, true);
      apWifiMac->AllocateBeamformingServicePeriod (dWifiMac->GetAssociationID (), cWifiMac->GetAssociationID (), 0, true);
    }
}

void
SLSCompleted (Ptr<DmgWifiMac> staWifiMac, Mac48Address address, ChannelAccessPeriod accessPeriod,
              BeamformingDirection beamformingDirection, bool isInitiatorTxss, bool isResponderTxss,
              SECTOR_ID sectorId, ANTENNA_ID antennaId)
{
  if (accessPeriod == CHANNEL_ACCESS_DTI)
    {
      std::cout << "DMG STA " << staWifiMac->GetAddress () << " completed SLS phase with DMG STA " << address << std::endl;
      std::cout << "The best antenna configuration is SectorID=" << uint32_t (sectorId)
                << ", AntennaID=" << uint32_t (antennaId) << std::endl;

     stationsTrained++;


      if ((stationsTrained == 4) & !scheduledStaticPeriods)  //stationsTrained times need SLSCompleted times. If 4 STA nodes, stationsTrained = 4. 
        {
          uint32_t startPeriod = 0;
          std::cout << "Schedule Static Periods" << std::endl;
          scheduledStaticPeriods = true;
          /* Schedule Static Periods */
          Time guardTime = MicroSeconds (5);
          startPeriod = apWifiMac->AllocateSingleContiguousBlock (1, SERVICE_PERIOD_ALLOCATION, true,
                                                                      aWifiMac->GetAssociationID (),
                                                                      bWifiMac->GetAssociationID (),
                                                                      startPeriod + guardTime.GetMicroSeconds (), sp1Duration);

          startPeriod = apWifiMac->AllocateSingleContiguousBlock (2, SERVICE_PERIOD_ALLOCATION, true,
                                                                      dWifiMac->GetAssociationID (),
                                                                      cWifiMac->GetAssociationID (),
                                                                      startPeriod + guardTime.GetMicroSeconds ()*2, sp1Duration);




        }
    }
}

int
main (int argc, char *argv[])
{
  uint32_t packetSize = 1448;                   /* Transport Layer Payload size in bytes. */
  string dataRate = "300Mbps";                  /* Application Layer Data Rate. */
  string dataRate2 = "150Mbps";                  /* Application Layer Data Rate. */

  string tcpVariant = "ns3::TcpNewReno";        /* TCP Variant Type. */
  uint32_t bufferSize = 131072;                 /* TCP Send/Receive Buffer Size. */
  uint32_t msduAggregationSize = 7935;          /* The maximum aggregation size for A-MSDU in Bytes. */
  uint32_t queueSize = 10000;                   /* Wifi Mac Queue Size. */
  string phyMode = "DMG_MCS12";                 /* Type of the Physical Layer. */
  bool verbose = false;                         /* Print Logging Information. */
  double simulationTime = 10;                   /* Simulation time in seconds. */
  bool pcapTracing = false;                     /* PCAP Tracing is enabled or not. */

  uint64_t maxPackets = 0;                      /* The maximum number of packets to transmit. */

  /* Command line argument parser setup. */
  CommandLine cmd;
  cmd.AddValue ("packetSize", "Payload size in bytes", packetSize);
  cmd.AddValue ("dataRate", "Data rate for OnOff Application", dataRate);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpTahoe, TcpReno, TcpNewReno, TcpWestwood, TcpWestwoodPlus ", tcpVariant);
  cmd.AddValue ("bufferSize", "TCP Buffer Size (Send/Receive)", bufferSize);
  cmd.AddValue ("msduAggregation", "The maximum aggregation size for A-MSDU in Bytes", msduAggregationSize);
  cmd.AddValue ("queueSize", "The size of the Wifi Mac Queue", queueSize);
  cmd.AddValue ("blocks", "Number of SP Blocks per allocation", blocks);
  cmd.AddValue ("sp1Duration", "The duration of service period in MicroSeconds in the forward direction", sp1Duration);
  cmd.AddValue ("sp2Duration", "The duration of service period in MicroSeconds in the reverse direction", sp2Duration);
  cmd.AddValue ("phyMode", "802.11ad PHY Mode", phyMode);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("pcap", "Enable PCAP Tracing", pcapTracing);

  cmd.AddValue ("scheduledStaticPeriods", "scheduledStaticPeriods", scheduledStaticPeriods);
  cmd.AddValue ("channel_access_scheme", "channel_access_scheme", channel_access_scheme);

  cmd.Parse (argc, argv);

if(channel_access_scheme == "CBAP")
 {
std::cout << "-------------------- " << "Channel Access Scheme Selected CBAP" << "-------------------- "  << std::endl;
 }
else
 {
std::cout << "-------------------- " << "Channel Access Scheme Selected SP" << "-------------------- "  << std::endl;
 }

  /* Global params: no fragmentation, no RTS/CTS, fixed rate for all packets */
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));

  /*** Configure TCP Options ***/
  /* Select TCP variant */
  TypeId tid = TypeId::LookupByName (tcpVariant);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (tid));
  /* Configure TCP Segment Size */
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (packetSize));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (bufferSize));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (bufferSize));

  /**** WifiHelper is a meta-helper: it helps creates helpers ****/
  DmgWifiHelper wifi;

  /* Basic setup */
  wifi.SetStandard (WIFI_PHY_STANDARD_80211ad);

  /* Turn on logging */
  if (verbose)
    {
      wifi.EnableLogComponents ();
      LogComponentEnable ("EvaluateServicePeriod", LOG_LEVEL_ALL);
    }

  /**** Set up Channel ****/
  DmgWifiChannelHelper wifiChannel ;
  /* Simple propagation delay model */
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  /* Friis model with standard-specific wavelength */
  wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel", "Frequency", DoubleValue (60.48e9));

  /**** Setup physical layer ****/
  DmgWifiPhyHelper wifiPhy = DmgWifiPhyHelper::Default ();
  /* Nodes will be added to the channel we set up earlier */
  wifiPhy.SetChannel (wifiChannel.Create ());
  /* All nodes transmit at 10 dBm == 10 mW, no adaptation */
  wifiPhy.Set ("TxPowerStart", DoubleValue (10.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (10.0));
  wifiPhy.Set ("TxPowerLevels", UintegerValue (1));
  /* Set operating channel */
  wifiPhy.Set ("ChannelNumber", UintegerValue (2));
  /* Sensitivity model includes implementation loss and noise figure */
  wifiPhy.Set ("CcaMode1Threshold", DoubleValue (-79));
  wifiPhy.Set ("EnergyDetectionThreshold", DoubleValue (-79 + 3));
  /* Set default algorithm for all nodes to be constant rate */
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "ControlMode", StringValue (phyMode),
                                                                "DataMode", StringValue (phyMode));

  /* Make four nodes and set them up with the phy and the mac */
  NodeContainer wifiNodes;
  wifiNodes.Create (5);
  Ptr<Node> apNode = wifiNodes.Get (0);
  Ptr<Node> aNode = wifiNodes.Get (1);
  Ptr<Node> bNode = wifiNodes.Get (2);

  Ptr<Node> cNode = wifiNodes.Get (3);
  Ptr<Node> dNode = wifiNodes.Get (4);


  /* Add a DMG upper mac */
  DmgWifiMacHelper wifiMac = DmgWifiMacHelper::Default ();

  /* Install DMG PCP/AP Node */
  Ssid ssid = Ssid ("ServicePeriod");
  wifiMac.SetType ("ns3::DmgApWifiMac",
                   "Ssid", SsidValue(ssid),
                   "BE_MaxAmpduSize", UintegerValue (0),
                   "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "SSSlotsPerABFT", UintegerValue (8), "SSFramesPerSlot", UintegerValue (8),
                   "BeaconInterval", TimeValue (MicroSeconds (102400)),
                   "ATIPresent", BooleanValue (false));

  /* Set Analytical Codebook for the DMG Devices */
  wifi.SetCodebook ("ns3::CodebookAnalytical",
                    "CodebookType", EnumValue (SIMPLE_CODEBOOK),
                    "Antennas", UintegerValue (1),
                    "Sectors", UintegerValue (8));

  NetDeviceContainer apDevice;
  apDevice = wifi.Install (wifiPhy, wifiMac, apNode);

  /* Install DMG STA Nodes */
  wifiMac.SetType ("ns3::DmgStaWifiMac",
                   "Ssid", SsidValue (ssid), "ActiveProbing", BooleanValue (false),
                   "BE_MaxAmpduSize", UintegerValue (0),
                   "BE_MaxAmsduSize", UintegerValue (msduAggregationSize));

  staDevices = wifi.Install (wifiPhy, wifiMac, NodeContainer (aNode, bNode, cNode, dNode));

  /* Setting mobility model */
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, +1.0, 0.0));   /* PCP/AP */
  positionAlloc->Add (Vector (-1.0, 0.0, 0.0));   /* a STA */
  positionAlloc->Add (Vector (+1.0, 0.0, 0.0));   /* b STA */

  positionAlloc->Add (Vector (-2.0, 0.0, 0.0));   /* c STA */
  positionAlloc->Add (Vector (+2.0, 0.0, 0.0));   /* d STA */


  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiNodes);

  /* Internet stack*/
  InternetStackHelper stack;
  stack.Install (wifiNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ApInterface;
  ApInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces;
  staInterfaces = address.Assign (staDevices);

  /* Populate routing table */
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  /* We do not want any ARP packets */
  PopulateArpCache ();

  /*** Install Applications ***/

 
  /** b Node Variables **/
  uint64_t bNodeLastTotalRx = 0;
  double bNodeAverageThroughput = 0;

//------------------------------------------------------------------------------------------------------------------------------------------------------------
  /** c Node Variables **/
  uint64_t cNodeLastTotalRx = 0;
  double cNodeAverageThroughput = 0;
//------------------------------------------------------------------------------------------------------------------------------------------------------------


  /* Install Simple UDP Server on the bNode */
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), 9999));
  ApplicationContainer sinks = sinkHelper.Install ( NodeContainer(bNode,cNode) );
  packetSink = StaticCast<PacketSink> (sinks.Get (0));
  packetSink2 = StaticCast<PacketSink> (sinks.Get (1));


  /** STA Node Variables **/
//  uint64_t staNodeLastTotalRx = 0;
//  double staNodeAverageThroughput = 0;

  /* Install Simple UDP Transmiter on the DMG PCP/AP */
  Ptr<OnOffApplication> onoff;
  ApplicationContainer srcApp;
  OnOffHelper src ("ns3::UdpSocketFactory", InetSocketAddress (staInterfaces.GetAddress (1), 9999));
  src.SetAttribute ("MaxPackets", UintegerValue (maxPackets));
  src.SetAttribute ("PacketSize", UintegerValue (packetSize));
  src.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1e6]"));
  src.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  src.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate)));
  srcApp = src.Install (aNode);
  onoff = StaticCast<OnOffApplication> (srcApp.Get (0));


  src.SetAttribute ("Remote", AddressValue (InetSocketAddress (staInterfaces.GetAddress (2), 9999)));
  src.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate2)));
  srcApp.Add (src.Install (dNode));

  /* Schedule Applications */
  srcApp.Start (Seconds (1.0));
  srcApp.Stop (Seconds (simulationTime));
  sinks.Start (Seconds (1.0));



  /* Schedule Throughput Calulcations */
  Simulator::Schedule (Seconds (1.1), &CalculateThroughput, packetSink, bNodeLastTotalRx, bNodeAverageThroughput);

  Simulator::Schedule (Seconds (1.1), &CalculateThroughput2, packetSink2, cNodeLastTotalRx, cNodeAverageThroughput);

  /* Set Maximum number of packets in WifiMacQueue */
//  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/DcaTxop/Queue/MaxPackets", UintegerValue (queueSize));
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/BE_EdcaTxopN/Queue/MaxPackets", UintegerValue (queueSize));
//  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::DmgWifiMac/SPQueue/MaxPackets", UintegerValue (queueSize));

  /* Enable Traces */
  if (pcapTracing)
    {
      wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
      wifiPhy.SetSnapshotLength (120);
      wifiPhy.EnablePcap ("Traces/AccessPoint_CBAP", apDevice, false);
      wifiPhy.EnablePcap ("Traces/aNode_CBAP", staDevices.Get (0), false);
      wifiPhy.EnablePcap ("Traces/bNode_CBAP", staDevices.Get (1), false);
      wifiPhy.EnablePcap ("Traces/cNode_CBAP", staDevices.Get (2), false);
      wifiPhy.EnablePcap ("Traces/dNode_CBAP", staDevices.Get (3), false);

    }

  /* Install FlowMonitor on all nodes */
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  /* Stations */
  apWifiNetDevice = StaticCast<WifiNetDevice> (apDevice.Get (0));
  aWifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (0));
  bWifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (1));

  cWifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (2));
  dWifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (3));


  apWifiMac = StaticCast<DmgApWifiMac> (apWifiNetDevice->GetMac ());
  aWifiMac = StaticCast<DmgStaWifiMac> (aWifiNetDevice->GetMac ());
  bWifiMac = StaticCast<DmgStaWifiMac> (bWifiNetDevice->GetMac ());

  cWifiMac = StaticCast<DmgStaWifiMac> (cWifiNetDevice->GetMac ());
  dWifiMac = StaticCast<DmgStaWifiMac> (dWifiNetDevice->GetMac ());


  /** Connect Traces **/
  aWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssoicated, aWifiMac));
  bWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssoicated, bWifiMac));

  cWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssoicated, cWifiMac));
  dWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssoicated, dWifiMac));


  aWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, aWifiMac));
  bWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, bWifiMac));

  cWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, cWifiMac));
  dWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, dWifiMac));


  Simulator::Stop (Seconds (simulationTime + 0.101));
  Simulator::Run ();
  Simulator::Destroy ();

  /* Print per flow statistics */
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;;
      std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
      std::cout << "  Tx Bytes:   " << i->second.txBytes << std::endl;
      std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / ((simulationTime - 1) * 1e6)  << " Mbps" << std::endl;;
      std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;;
      std::cout << "  Rx Bytes:   " << i->second.rxBytes << std::endl;
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / ((simulationTime - 1) * 1e6)  << " Mbps" << std::endl;;
    }

//  /* Print Application Layer Results Summary */
//  std::cout << "\nApplication Layer Statistics:" << std::endl;;
//  std::cout << "  Tx Packets: " << onoff->GetTotalTxPackets () << std::endl;
//  std::cout << "  Tx Bytes:   " << onoff->GetTotalTxBytes () << std::endl;
//  std::cout << "  Rx Packets: " << packetSink->GetTotalReceivedPackets () << std::endl;
//  std::cout << "  Rx Bytes:   " << packetSink->GetTotalRx () << std::endl;
//  std::cout << "  Throughput: " << packetSink->GetTotalRx () * 8.0 / ((simulationTime - 1) * 1e6) << " Mbps" << std::endl;
  /* Print Application Layer Results Summary */
  Ptr<PacketSink> sink;
  std::cout << "\nApplication Layer Statistics:" << std::endl;;
  for (uint8_t i = 0; i < srcApp.GetN (); i++)
    {
      onoff = StaticCast<OnOffApplication> (srcApp.Get (i));
      sink = StaticCast<PacketSink> (sinks.Get (i));
      std::cout << "Stats (" << i + 1 << ")" << std::endl;
      std::cout << "  Tx Packets: " << onoff->GetTotalTxPackets () << std::endl;
      std::cout << "  Tx Bytes:   " << onoff->GetTotalTxBytes () << std::endl;
      std::cout << "  Rx Packets: " << sink->GetTotalReceivedPackets () << std::endl;
      std::cout << "  Rx Bytes:   " << sink->GetTotalRx () << std::endl;
      std::cout << "  Throughput: " << sink->GetTotalRx () * 8.0 / ((simulationTime - 3) * 1e6) << " Mbps" << std::endl;
    }
  return 0;
}
