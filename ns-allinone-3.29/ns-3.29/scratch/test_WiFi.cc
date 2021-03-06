#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/lte-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/wifi-module.h"
#include "ns3/csma-module.h"
#include "ns3/ofswitch13-module.h"
#include "ns3/socket.h"
#include <string>
#include <regex>

using namespace ns3;

class TakumaController;

void modify ()
{
    std::ostringstream oss;
    oss << "Update:" << Simulator::Now ().GetSeconds ();
    // Every update change the node description for node 2
    std::ostringstream node0Oss;
    node0Oss << "-----Node:" << Simulator::Now ().GetSeconds ();
    if (Simulator::Now ().GetSeconds () < 10) // This is important or the simulation
    {
        // will run endlessly
        Simulator::Schedule (Seconds (1), modify);
    }
}

void
SetPosition (Ptr<Node> node, Vector position)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel> ();
    mobility->SetPosition (position);
}

Vector 
GetPosition (Ptr<Node> node) 
{ 
	Ptr<MobilityModel> mobility = node->GetObject<MobilityModel> (); 
	return mobility->GetPosition (); 
} 

void
AdvancePosition (Ptr<Node> node)
{
    Vector pos = GetPosition (node);
    pos.x += 10.0;
    SetPosition (node, pos);
    Simulator::Schedule (Seconds (1.0), AdvancePosition, node);
}

NS_LOG_COMPONENT_DEFINE("Study");
    
int
main (int argc, char *argv[])
{
    bool verbose = false;
    bool trace = false;
    uint16_t numNodes = 7;
    uint16_t sourceNode = 0;
    uint16_t sinkNode = numNodes - 1;
    Time simTime = Seconds (30.0);
    Time simStart1 = Seconds (1.0);
    Time simStop1 = Seconds (15.0);
    Time simStart2 = Seconds (15.0);
    Time simStop2 = Seconds (30.0);
    Time interPacketInterval = MilliSeconds (100);
    double distance = 90.0;    // m
    uint32_t packetSize = 128; //byte

    // Configure command line parameters
    CommandLine cmd;
    cmd.AddValue ("verbose", "Enable verbose output", verbose);
    cmd.AddValue ("trace", "Enable datapath stats and pcap traces", trace);
    cmd.AddValue ("numNodes", "Number of nodes", numNodes);
    cmd.AddValue ("simTime", "Total duration of the simulation", simTime);
    cmd.AddValue ("interPacketInterval", "Inter packet interval", interPacketInterval);
    cmd.AddValue ("distance", "Distance between nodes", distance);
    cmd.AddValue ("packetSize", "Size of packet", packetSize);
    cmd.Parse (argc, argv);

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults ();

    // parse again so you can override default values from the command line
    cmd.Parse(argc, argv);

    if (verbose)
    {
        OFSwitch13Helper::EnableDatapathLogs ();
        LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13LearningController", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_ALL);
        LogComponentEnable ("OFSwitch13InternalHelper", LOG_LEVEL_ALL);
    }

    LogComponentEnable ("UdpClient", LOG_LEVEL_INFO);
    LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);

    // Enable checksum computations (required by OFSwitch13 module)
    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

    // Create UE nodes
    NodeContainer ueNodes;
    ueNodes.Create (numNodes);

    // Create AP nodes
    NodeContainer apNodes;
    apNodes.Create (1);

    // Create the switch node
    NodeContainer switches;
    switches.Create(numNodes);


    // Use the CsmaHelper to connect host nodes to the switch node
    CsmaHelper csma;
    csma.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
    csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));

    /*
     *          OPENFLOW
     */
    NetDeviceContainer ueDev;
    NetDeviceContainer switchPorts[numNodes];
    for (uint32_t i = 0; i < ueNodes.GetN (); i++)
    {
        NodeContainer pair (ueNodes.Get (i), switches.Get(i));
        NetDeviceContainer link = csma.Install (pair);
        ueDev.Add (link.Get (0));
        switchPorts[i].Add (link.Get (1));
    }

    for (int i = 1; i < 4; i++)
    {
        NodeContainer pair1 (switches.Get (sourceNode), switches.Get(i));
        NetDeviceContainer link1 = csma.Install (pair1);
        switchPorts[sourceNode].Add (link1.Get (0));
        switchPorts[i].Add (link1.Get (1));
    }
    for (int i = 1; i < 6; i++)
    {
        if (i == 3) continue;
        NodeContainer pair2 (switches.Get (3), switches.Get(i));
        NetDeviceContainer link2 = csma.Install (pair2);
        switchPorts[3].Add (link2.Get (0));
        switchPorts[i].Add (link2.Get (1));
    }
    for (int i = 3; i < 6; i++)
    {
        NodeContainer pair3 (switches.Get (sinkNode), switches.Get(i));
        NetDeviceContainer link3 = csma.Install (pair3);
        switchPorts[sinkNode].Add (link3.Get (0));
        switchPorts[i].Add (link3.Get (1));
    }
    NodeContainer pair4 (switches.Get (1), switches.Get(4));
    NetDeviceContainer link4 = csma.Install (pair4);
    switchPorts[1].Add (link4.Get (0));
    switchPorts[4].Add (link4.Get (1));
    NodeContainer pair5 (switches.Get (2), switches.Get(5));
    NetDeviceContainer link5 = csma.Install (pair5);
    switchPorts[2].Add (link5.Get (0));
    switchPorts[5].Add (link5.Get (1));

    // Create two controller nodes
    NodeContainer controllers;
    controllers.Create (1);


    /*
     *          WIFI
     */
    NetDeviceContainer wifiDev;
    NetDeviceContainer apDev;

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
	YansWifiPhyHelper phy = YansWifiPhyHelper::Default ();
    phy.SetChannel (channel.Create ());

    WifiHelper wifi;
    wifi.SetStandard (WIFI_PHY_STANDARD_80211n_2_4GHZ);
    //wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                            "DataMode",StringValue ("HtMcs0"),
                            "ControlMode",StringValue ("HtMcs7"));

    WifiMacHelper mac;
    Ssid ssid = Ssid ("Wifi");
    mac.SetType ("ns3::StaWifiMac",
                 "Ssid", SsidValue (ssid),
                 "ActiveProbing", BooleanValue (false));
    wifiDev = wifi.Install(phy, mac, ueNodes.Get(sourceNode));

    mac.SetType ("ns3::ApWifiMac",
                 "Ssid", SsidValue (ssid));
    apDev = wifi.Install(phy, mac, apNodes);
    
    NodeContainer p2pNodes;     // = 2
    p2pNodes.Add (apNodes);
    p2pNodes.Create (1);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
    
    NetDeviceContainer p2pDev;
    p2pDev = p2p.Install (p2pNodes);

    NodeContainer csmaNodes;    // = 2
    csmaNodes.Add (p2pNodes.Get (1));
    csmaNodes.Add (ueNodes.Get (sinkNode));


    NetDeviceContainer csmaDev;
    csmaDev = csma.Install (csmaNodes);


    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    for (int i = 0; i < numNodes; i++)
    {
        
        if (i % 3 == 0){
            positionAlloc->Add(Vector(distance * (i / 3), 200, 0));
        }
        else if (i % 3 == 1){
            positionAlloc->Add(Vector(distance * ((i - 1) / 3) + distance / 2, 170, 0));
        }
        else{
            positionAlloc->Add(Vector(distance * ((i - 2) / 3) + distance / 2, 230, 0));
        }
        
    }
    for (int i = 0; i < numNodes; i++)
    {
        if (i % 3 == 0){
            positionAlloc->Add(Vector(distance * (i / 3), 210, 0));
        }
        else if (i % 3 == 1){
            positionAlloc->Add(Vector(distance * ((i - 1) / 3) + distance / 2, 180, 0));
        }
        else{
            positionAlloc->Add(Vector(distance * ((i - 2) / 3) + distance / 2, 240, 0));
        }
    }
    positionAlloc->Add(Vector(distance * 2, 50, 0));
    for (uint16_t i = 0; i < 2; i++)
    {
        positionAlloc->Add(Vector(distance * 2 * i + 50, 140, 0));
    }

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install (switches);
    mobility.Install (ueNodes);
    mobility.Install (controllers);
    mobility.Install (p2pNodes);        //AP
    
    // Configure the OpenFlow network domain
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper> ();
    Ptr<TakumaController> ctrl = CreateObject<TakumaController> ();

    of13Helper->InstallController (controllers.Get (0), ctrl);
    for (int i = 0; i < numNodes; i++)
    {
        of13Helper->InstallSwitch (switches.Get(i), switchPorts[i]);
    }
    of13Helper->CreateOpenFlowChannels ();

    // Install the TCP/IP stack into ueNodes nodes
    InternetStackHelper internet;
    //internet.InstallAll ();
    internet.Install(ueNodes);
    internet.Install(p2pNodes);

    // Set IPv4 host addresses
    Ipv4AddressHelper address;
    Ipv4InterfaceContainer ueIpIface_csma;
    address.SetBase ("10.1.1.0", "255.255.255.0");
    ueIpIface_csma = address.Assign (ueDev);


    address.SetBase ("10.10.10.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterface;
    p2pInterface = address.Assign (p2pDev);
    address.SetBase ("10.10.11.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterface;
    csmaInterface = address.Assign (csmaDev);
    address.SetBase ("10.10.12.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiIpIface;
    wifiIpIface = address.Assign (wifiDev);
    Ipv4InterfaceContainer apIpIface;
    apIpIface = address.Assign (apDev);

    // WiFi
    UdpServerHelper Server1 (30);
    ApplicationContainer serverApps1 = Server1.Install(csmaNodes.Get(sinkNode-5));
    serverApps1.Start (simStart1);
    serverApps1.Stop (simStop1);

    UdpClientHelper Client1(csmaInterface.GetAddress(sinkNode-5),30);
    Client1.SetAttribute ("MaxPackets", UintegerValue (10000));
    Client1.SetAttribute ("Interval", TimeValue (interPacketInterval));
    Client1.SetAttribute ("PacketSize", UintegerValue (packetSize));

    ApplicationContainer clientApps1 = Client1.Install (ueNodes.Get (sourceNode));
    clientApps1.Start (simStart1);
    clientApps1.Stop (simStop1);


    // csma
    UdpServerHelper Server2 (9);
    ApplicationContainer serverApps2 = Server2.Install(ueNodes.Get(sinkNode));
    serverApps2.Start (simStart2);
    serverApps2.Stop (simStop2);

    UdpClientHelper Client2(ueIpIface_csma.GetAddress(sinkNode),9);
    Client2.SetAttribute ("MaxPackets", UintegerValue (10000));
    Client2.SetAttribute ("Interval", TimeValue (interPacketInterval));
    Client2.SetAttribute ("PacketSize", UintegerValue (packetSize));

    ApplicationContainer clientApps2 = Client2.Install(ueNodes.Get(sourceNode));
    clientApps2.Start (simStart2);
    clientApps2.Stop (simStop2);

    // Enable datapath stats and pcap traces at ueNodes, switch(es), and controller(s)
    if (trace)
    {
        of13Helper->EnableOpenFlowPcap ("openflow");
        of13Helper->EnableDatapathStats ("switch-stats");
        csma.EnablePcap ("switch", switchPorts[0], true);
        csma.EnablePcap ("switch", switchPorts[1], true);
        csma.EnablePcap ("switch", switchPorts[2], true);
        csma.EnablePcap ("host", ueDev);
    }


    // Run the simulation
    AnimationInterface anim("test_WiFi.xml");
    anim.EnablePacketMetadata();

    double x_size = 4.0;
    double y_size = 4.0;

    for (uint32_t i = 0; i < numNodes; i++)
    {
        anim.UpdateNodeDescription (ueNodes.Get (i), "UE");
	    anim.UpdateNodeSize (ueNodes.Get (i)->GetId(), x_size, y_size);
        anim.UpdateNodeColor (ueNodes.Get (i), 255, 0, 0);
    }
    for (uint32_t i = 0; i < numNodes; i++)
    {
        anim.UpdateNodeDescription (switches.Get (i), "OFS");
	    anim.UpdateNodeSize (switches.Get (i)->GetId(), x_size, y_size);
        anim.UpdateNodeColor (switches.Get (i), 0, 255, 0);
    }
    anim.UpdateNodeDescription (controllers.Get (0), "OFC");
    anim.UpdateNodeSize (controllers.Get (0)->GetId(),  x_size, y_size);
    anim.UpdateNodeColor (controllers.Get (0), 0, 0, 255);
    for (uint32_t i = 0; i < 2; i++)
    {
        anim.UpdateNodeDescription (p2pNodes.Get (i), "AP");
	    anim.UpdateNodeSize (p2pNodes.Get (i)->GetId(),  x_size, y_size);
        anim.UpdateNodeColor (p2pNodes.Get (i), 0, 255, 0);
    }

    Simulator::Schedule (Seconds (1), modify);
    Simulator::Schedule (Seconds (1), AdvancePosition, ueNodes.Get (sourceNode));
    Simulator::Schedule (Seconds (1), AdvancePosition, ueNodes.Get (sinkNode));
    Simulator::Schedule (Seconds (1), AdvancePosition, switches.Get (sourceNode));
    Simulator::Schedule (Seconds (1), AdvancePosition, switches.Get (sinkNode));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Stop (simTime);
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";     //フローiの送信パケット数合計
        std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";	    //送信バイト数合計
        std::cout << "  Tx Offered: " << i->second.txBytes * 8.0 / 10 / 1000 / 1000  << " Mbps\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";	    //受信パケット数合計
        std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";	    //受信バイト数合計
        std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 10 / 1000 / 1000  << " Mbps\n";	//スループット
    }

    Simulator::Destroy ();
    return 0;
}

class TakumaController : public OFSwitch13Controller{
	public:
		ofl_err HandlePacketIn(struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid);
		
		void OptimalPath(Ipv4Address sw_srcIp, Mac48Address dst48);
		uint32_t BroadCastPath(struct ofl_msg_packet_in *msg, Ipv4Address sw_srcIp);
		void CreateFlowMod(Ptr<const RemoteSwitch> swtch, Mac48Address dst48);
		void PacketOut(struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid, uint32_t outPort);

	protected:
		void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch);
	
	private:
		// 最適経路DBデータ構造作成
		// [次ホップ,出力ポート]の構造体
		typedef struct{
			uint32_t outPort;
			Ipv4Address nextHop;
		} opt_output_nhop_table;
		// [スイッチPtr,{ 宛先 : テーブル(出力ポート,ネクストホップ) }]の構造体
		typedef struct{
			Ptr<const RemoteSwitch> swtchPtr;
			std::map<Mac48Address, opt_output_nhop_table> flowMap;
		} opt_swptr_flowmap_struct;
		// { スイッチIP : 構造体(スイッチPtr,フローエントリ群) } のmap
		typedef std::map<Ipv4Address, opt_swptr_flowmap_struct> OPT_DB_MAP;
		// 最適経路DB(mapとして保存されている)
		OPT_DB_MAP opt_db_map;

};

ofl_err TakumaController::HandlePacketIn(struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid){
	enum ofp_packet_in_reason reason = msg->reason;
	auto sw_srcIp = swtch->GetIpv4();

	// PacketInの中身確認
	char *msgStr = ofl_structs_match_to_string((struct ofl_match_header*)msg->match, 0);
	//NS_LOG_INFO(msgStr);
	free(msgStr);

	if(reason == OFPR_NO_MATCH){
		// 宛先macアドレス保存
		Mac48Address dst48;
		struct ofl_match_tlv *ethDst = oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match*)msg->match);
		dst48.CopyFrom(ethDst->value);
		//NS_LOG_INFO(dst48);

		// Ethタイプ保存
		// struct ofl_match_tlv *ethType = oxm_match_lookup(OXM_OF_ETH_TYPE, (struct ofl_match*)msg->match);
		// NS_LOG_INFO(ethType->value);

		// ブロードキャスト処理
		if(dst48 == "ff:ff:ff:ff:ff:ff"){
			// 出力ポート検索
			uint32_t outPort = BroadCastPath(msg, sw_srcIp);
			// パケット出力
			PacketOut(msg, swtch, xid, outPort);
			NS_LOG_INFO("********************");
		}else{
			// 以降の処理がどのスイッチのものか出力
			NS_LOG_INFO(sw_srcIp);

			// 最適経路制御呼び出し
			OptimalPath(sw_srcIp, dst48);
			// パケット出力
			PacketOut(msg, swtch, xid, opt_db_map[sw_srcIp].flowMap[dst48].outPort);

			// Flow-mod作成
			CreateFlowMod(opt_db_map[sw_srcIp].swtchPtr, dst48);

			NS_LOG_INFO("-------------------");
		}
	}
	return 0;
}

void TakumaController::HandshakeSuccessful(Ptr<const RemoteSwitch> swtch){
	DpctlExecute (swtch, "flow-mod cmd=add,table=0,prio=0 "
		       	"apply:output=ctrl:128");
	DpctlExecute(swtch, "port-desc");

	// COに接続したスイッチの最適経路DB作成(初期化)
	opt_swptr_flowmap_struct opt_emp_entry;
	opt_emp_entry.swtchPtr = swtch;
	std::pair<Ipv4Address, opt_swptr_flowmap_struct> opt_entry (swtch->GetIpv4(), opt_emp_entry);
	opt_db_map.insert(opt_entry);

}

void TakumaController::OptimalPath(Ipv4Address sw_srcIp, Mac48Address dst48){
	// 最適経路DBを探索
	auto findRoute = opt_db_map[sw_srcIp].flowMap.find(dst48);
	// 最適経路未登録の場合
	if(findRoute == opt_db_map[sw_srcIp].flowMap.end()){
		// 宛先への最適経路を探索 -> 最適経路テーブル作成
		opt_output_nhop_table path_table;
		// 宛先アドレス別に場合分け
		if(dst48 == "00:00:00:00:00:01"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}
		}else if(dst48 == "00:00:00:00:00:03"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}
		}else if(dst48 == "00:00:00:00:00:05"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}
		}else if(dst48 == "00:00:00:00:00:07"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}
		}else if(dst48 == "00:00:00:00:00:09"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート5へ出力");
				path_table.outPort = 5;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}
		}else if(dst48 == "00:00:00:00:00:0b"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート6へ出力");
				path_table.outPort = 6;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート2へ出力");
				path_table.outPort = 2;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}
		}else if(dst48 == "00:00:00:00:00:0d"){
			if (sw_srcIp == "10.100.0.2"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.3"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.4"){
				NS_LOG_INFO("ポート4へ出力");
				path_table.outPort = 4;
			}else if (sw_srcIp == "10.100.0.5"){
				NS_LOG_INFO("ポート7へ出力");
				path_table.outPort = 7;
			}else if (sw_srcIp == "10.100.0.6"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.7"){
				NS_LOG_INFO("ポート3へ出力");
				path_table.outPort = 3;
			}else if (sw_srcIp == "10.100.0.8"){
				NS_LOG_INFO("ポート1へ出力");
				path_table.outPort = 1;
			}
		}
		// 最適経路DBへ保存
		std::pair<Mac48Address, opt_output_nhop_table> entry (dst48, path_table);
		opt_db_map[sw_srcIp].flowMap.insert(entry);
	}
}

uint32_t TakumaController::BroadCastPath(struct ofl_msg_packet_in *msg, Ipv4Address sw_srcIp){
	// 宛先IP保存
	char *msgStr = ofl_structs_match_to_string((struct ofl_match_header*)msg->match, 0);

	// 正規表現での抽出の準備
	std::regex re("arp_tpa=\".*\", arp_sha");
	std::cmatch m;

	// 宛先IP格納先
	std::string ue_dstIp;

	// 正規表現での抽出処理
	if(std::regex_search(msgStr, m, re)){
		ue_dstIp = m.str();
	}
	size_t c = ue_dstIp.find("\", arp_sha");
	ue_dstIp.erase(c);
	ue_dstIp.erase(0, 9);
	//NS_LOG_INFO(ue_dstIp);
	free(msgStr);

	// 宛先別出力ポート探索
	uint32_t outPort;
    if(ue_dstIp == "10.1.1.1"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 2;
		}
	}else if(ue_dstIp == "10.1.1.2"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 3;
		}
	}else if(ue_dstIp == "10.1.1.3"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 4;
		}
	}else if(ue_dstIp == "10.1.1.4"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 2;
		}
	}else if(ue_dstIp == "10.1.1.5"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 5;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 3;
		}
	}else if(ue_dstIp == "10.1.1.6"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 6;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 2;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 1;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 4;
		}
	}else if(ue_dstIp == "10.1.1.7"){
		if(sw_srcIp == "10.100.0.2"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.3"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.4"){
			outPort = 4;
		}else if(sw_srcIp == "10.100.0.5"){
			outPort = 7;
		}else if(sw_srcIp == "10.100.0.6"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.7"){
			outPort = 3;
		}else if(sw_srcIp == "10.100.0.8"){
			outPort = 1;
		}
	}
	return outPort;
}

void TakumaController::CreateFlowMod(Ptr<const RemoteSwitch> swtch, Mac48Address dst48){
	std::ostringstream cmd;
	cmd << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
		<< ",prio=100" << " eth_dst=" << dst48
		<< " apply:output=" << opt_db_map[swtch->GetIpv4()].flowMap[dst48].outPort;
	DpctlExecute(swtch, cmd.str());
}

void TakumaController::PacketOut(struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch, uint32_t xid, uint32_t outPort){
	// PacketIn時の入力ポート保存
	uint32_t inPort;
        size_t portLen = OXM_LENGTH (OXM_OF_IN_PORT); // (Always 4 bytes)
        struct ofl_match_tlv *input = oxm_match_lookup (OXM_OF_IN_PORT, (struct ofl_match*)msg->match);
        memcpy (&inPort, input->value, portLen);	

	// PacketOutの準備
	struct ofl_msg_packet_out reply;
	reply.header.type = OFPT_PACKET_OUT;
	reply.buffer_id = msg->buffer_id;
	reply.in_port = inPort;
	reply.data_length = 0;
        reply.data = 0;

	if (msg->buffer_id == NO_BUFFER){
		// No packet buffer. Send data back to switch
		reply.data_length = msg->data_length;
		reply.data = msg->data;
	}
	// PacketOutの実行
	struct ofl_action_output *a = (struct ofl_action_output*)xmalloc (sizeof (struct ofl_action_output));
	a->header.type = OFPAT_OUTPUT;
	a->port = outPort;
	a->max_len = 0;
	reply.actions_num = 1;
	reply.actions = (struct ofl_action_header**)&a;

	SendToSwitch (swtch, (struct ofl_msg_header*)&reply, xid);
	free (a);
}