/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Universita' di Firenze, Italy
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
 * Author: Tommaso Pecorella <tommaso.pecorella@unifi.it>
 * Edited by Tan Luu <luuhoangtan@gmail.com>
 * and Thoan Pham <phamngocthoan2171991@gmail.com>
 */

// Network topology
//
//    SRC
//     |<=== source network
//     A-----B
//      \   / \   all networks have cost 1, except
//       \ /  |   for the direct link from C to D, which
//        C  /    has cost 10
//        | /
//        |/
//        D
//        |<=== target network
//       DST
//
//
// A, B, C and D are RIPv2 routers.
// A and D are configured with static addresses.
// SRC and DST will exchange packets.
//
// After about 3 seconds, the topology is built, and Echo Reply will be received.
// After 40 seconds, the link between B and D will break, causing a route failure.
// After 44 seconds from the failure, the routers will recovery from the failure.
// Split Horizoning should affect the recovery time, but it is not. See the manual
// for an explanation of this effect.
//
// If "showPings" is enabled, the user will see:
// 1) if the ping has been acknowledged
// 2) if a Destination Unreachable has been received by the sender
// 3) nothing, when the Echo Request has been received by the destination but
//    the Echo Reply is unable to reach the sender.
// Examining the .pcap files with Wireshark can confirm this effect.


#include <fstream>
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-routing-table-entry.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Ripv2SimpleRouting");

void TearDownLink (Ptr<Node> nodeA, Ptr<Node> nodeB, uint32_t interfaceA, uint32_t interfaceB)
{
  nodeA->GetObject<Ipv6> ()->SetDown (interfaceA);
  nodeB->GetObject<Ipv6> ()->SetDown (interfaceB);
}

int main (int argc, char **argv)
{
  bool verbose = false;
  bool printRoutingTables = true;
  bool showPings = false;
  std::string SplitHorizon ("PoisonReverse"); // string object

  CommandLine cmd;
  cmd.AddValue ("verbose", "turn on log components", verbose);
  cmd.AddValue ("printRoutingTables", "Print routing tables at 30, 60 and 90 seconds", printRoutingTables);
  cmd.AddValue ("showPings", "Show Ping reception", showPings);
  cmd.AddValue ("splitHorizonStrategy", "Split Horizon strategy to use (NoSplitHorizon, SplitHorizon, PoisonReverse)", SplitHorizon);
  cmd.Parse (argc, argv);

  if (verbose)
    {
      LogComponentEnable ("Ripv2SimpleRouting", LOG_LEVEL_INFO);
      LogComponentEnable ("Ripv2", LOG_LEVEL_ALL);
      LogComponentEnable ("Icmpv4L4Protocol", LOG_LEVEL_INFO);
      LogComponentEnable ("Ipv4Interface", LOG_LEVEL_ALL);
      LogComponentEnable ("Icmpv4L4Protocol", LOG_LEVEL_ALL);
      LogComponentEnable ("ArpCache", LOG_LEVEL_ALL); // ARP cache
      LogComponentEnable ("V4Ping", LOG_LEVEL_ALL);
    }

  if (showPings)
    {
      LogComponentEnable ("V4Ping", LOG_LEVEL_INFO);
    }

  if (SplitHorizon == "NoSplitHorizon")
    {
      Config::SetDefault ("ns3::Ripv2::SplitHorizon", EnumValue (Ripv2::NO_SPLIT_HORIZON));
    }
  else if (SplitHorizon == "SplitHorizon")
    {
      Config::SetDefault ("ns3::Ripv2::SplitHorizon", EnumValue (Ripv2::SPLIT_HORIZON));
    }
  else
    {
      Config::SetDefault ("ns3::Ripv2::SplitHorizon", EnumValue (Ripv2::POISON_REVERSE));
    }

  NS_LOG_INFO ("Create nodes.");
  Ptr<Node> src = CreateObject<Node> ();
  Names::Add ("SrcNode", src);
  Ptr<Node> dst = CreateObject<Node> ();
  Names::Add ("DstNode", dst);
  Ptr<Node> a = CreateObject<Node> ();
  Names::Add ("RouterA", a);
  Ptr<Node> b = CreateObject<Node> ();
  Names::Add ("RouterB", b);
  Ptr<Node> c = CreateObject<Node> ();
  Names::Add ("RouterC", c);
  Ptr<Node> d = CreateObject<Node> ();
  Names::Add ("RouterD", d);
  NodeContainer net1 (src, a);
  NodeContainer net2 (a, b);
  NodeContainer net3 (a, c);
  NodeContainer net4 (b, c);
  NodeContainer net5 (c, d);
  NodeContainer net6 (b, d);
  NodeContainer net7 (d, dst);
  NodeContainer routers (a, b, c, d);
  NodeContainer nodes (src, dst);


  NS_LOG_INFO ("Create channels.");
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (5000000));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
  NetDeviceContainer ndc1 = csma.Install (net1);
  NetDeviceContainer ndc2 = csma.Install (net2);
  NetDeviceContainer ndc3 = csma.Install (net3);
  NetDeviceContainer ndc4 = csma.Install (net4);
  NetDeviceContainer ndc5 = csma.Install (net5);
  NetDeviceContainer ndc6 = csma.Install (net6);
  NetDeviceContainer ndc7 = csma.Install (net7);

  NS_LOG_INFO ("Create IPv4 and routing");
  Ripv2Helper ripv2Routing;

  // Rule of thumb:
  // Interfaces are added sequentially, starting from 0
  // However, interface 0 is always the loopback...
  ripv2Routing.ExcludeInterface (a, 1);
  ripv2Routing.ExcludeInterface (d, 3);

  // Set metric to 10 for c-d link (default 1)
  ripv2Routing.SetInterfaceMetric (c, 3, 10);
  ripv2Routing.SetInterfaceMetric (d, 1, 10);

  Ipv4ListRoutingHelper listRH;
  listRH.Add (ripv2Routing, 0);

  InternetStackHelper internetv4;
  internetv4.SetIpv6StackInstall (false);
  internetv4.SetRoutingHelper (listRH);
  internetv4.Install (routers);

  InternetStackHelper internetv4Nodes;
  internetv4Nodes.SetIpv6StackInstall (false);
  internetv4Nodes.Install (nodes);

  // Assign addresses.
  // The source and destination networks have global addresses
  // The "core" network just needs link-local addresses for routing.
  // We assign global addresses to the routers as well to receive
  // ICMPv6 errors.
  NS_LOG_INFO ("Assign IPv4 Addresses.");
  Ipv4AddressHelper ipv4;

  ipv4.SetBase ("10.1.0.0", "255.255.255.0" );
  Ipv4InterfaceContainer iic1 = ipv4.Assign (ndc1);
  iic1.SetForwarding (1, true);
  iic1.SetDefaultRouteInAllNodes (1);

  ipv4.SetBase ("10.0.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iic2 = ipv4.Assign (ndc2);
  iic2.SetForwarding (0, true);
  iic2.SetForwarding (1, true);

  ipv4.SetBase ("10.0.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iic3 = ipv4.Assign (ndc3);
  iic3.SetForwarding (0, true);
  iic3.SetForwarding (1, true);

  ipv4.SetBase ("10.0.3.0", "255.255.255.0");
  Ipv4InterfaceContainer iic4 = ipv4.Assign (ndc4);
  iic4.SetForwarding (0, true);
  iic4.SetForwarding (1, true);

  ipv4.SetBase ("10.0.4.0", "255.255.255.0");
  Ipv4InterfaceContainer iic5 = ipv4.Assign (ndc5);
  iic5.SetForwarding (0, true);
  iic5.SetForwarding (1, true);

  ipv4.SetBase ("10.0.5.0", "255.255.255.0");
  Ipv4InterfaceContainer iic6 = ipv4.Assign (ndc6);
  iic6.SetForwarding (0, true);
  iic6.SetForwarding (1, true);

  ipv4.SetBase ("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer iic7 = ipv4.Assign (ndc7);
  iic7.SetForwarding (0, true);
  iic7.SetDefaultRouteInAllNodes (0);

  if (printRoutingTables)
    {
      Ripv2Helper routingHelper;

      Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> (&std::cout);

      routingHelper.PrintRoutingTableAt (Seconds (30.0), a, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (30.0), b, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (30.0), c, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (30.0), d, routingStream);

      routingHelper.PrintRoutingTableAt (Seconds (60.0), a, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (60.0), b, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (60.0), c, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (60.0), d, routingStream);

      routingHelper.PrintRoutingTableAt (Seconds (90.0), a, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (90.0), b, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (90.0), c, routingStream);
      routingHelper.PrintRoutingTableAt (Seconds (90.0), d, routingStream);
    }

  NS_LOG_INFO ("Create Applications.");
  uint32_t packetSize = 1024;
  Time interPacketInterval = Seconds (1.0);

  V4PingHelper ping(iic7.GetAddress(1));
  ping.SetAttribute ("Interval", TimeValue (interPacketInterval));
  ping.SetAttribute ("Size", UintegerValue (packetSize));
  ApplicationContainer apps = ping.Install (src);
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (110.0));

  AsciiTraceHelper ascii;
  csma.EnableAsciiAll (ascii.CreateFileStream ("ripv2-simple-routing.tr"));
  csma.EnablePcapAll ("ripv2-simple-routing", true);

  //Simulator::Schedule (Seconds (40), &TearDownLink, b, d, 3, 2);

  /* Now, do the actual simulation. */
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop (Seconds (120));
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}

