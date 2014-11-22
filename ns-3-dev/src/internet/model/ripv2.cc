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
 */

#include <iomanip>
#include "ripv2.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/assert.h"
#include "ns3/unused.h"
#include "ns3/random-variable-stream.h"
#include "ns3/ipv4-route.h"
#include "ns3/node.h"
#include "ns3/names.h"
#include "ns3/ripv2-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/ipv4-packet-info-tag.h"

#define RIPV2_ALL_NODE "224.0.0.9"
#define RIPV2_PORT 520



namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ripv2");
NS_OBJECT_ENSURE_REGISTERED (Ripv2);

Ripv2::Ripv2 ()
  : m_ipv4 (0), m_splitHorizonStrategy (Ripv2::POISON_REVERSE), m_initialized (false)
{
  m_rng = CreateObject<UniformRandomVariable> ();
}

Ripv2::~Ripv2 ()
{
}

TypeId
Ripv2::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ripv2")
    .SetParent<Ipv4RoutingProtocol> ()
    .AddConstructor<Ripv2> ()
    .AddAttribute ("UnsolicitedRoutingUpdate", "The time between two Unsolicited Routing Updates.",
                   TimeValue (Seconds(30)),
                   MakeTimeAccessor (&Ripv2::m_unsolicitedUpdate),
                   MakeTimeChecker ())
    .AddAttribute ("StartupDelay", "Maximum random delay for protocol startup (send route requests).",
                   TimeValue (Seconds(1)),
                   MakeTimeAccessor (&Ripv2::m_startupDelay),
                   MakeTimeChecker ())
    .AddAttribute ("TimeoutDelay", "The delay to invalidate a route.",
                   TimeValue (Seconds(180)),
                   MakeTimeAccessor (&Ripv2::m_timeoutDelay),
                   MakeTimeChecker ())
    .AddAttribute ("GarbageCollectionDelay", "The delay to delete an expired route.",
                   TimeValue (Seconds(120)),
                   MakeTimeAccessor (&Ripv2::m_garbageCollectionDelay),
                   MakeTimeChecker ())
    .AddAttribute ("MinTriggeredCooldown", "Min cooldown delay after a Triggered Update.",
                   TimeValue (Seconds(1)),
                   MakeTimeAccessor (&Ripv2::m_minTriggeredUpdateDelay),
                   MakeTimeChecker ())
    .AddAttribute ("MaxTriggeredCooldown", "Max cooldown delay after a Triggered Update.",
                   TimeValue (Seconds(5)),
                   MakeTimeAccessor (&Ripv2::m_maxTriggeredUpdateDelay),
                   MakeTimeChecker ())
    .AddAttribute ("SplitHorizon", "Split Horizon strategy.",
                   EnumValue (Ripv2::POISON_REVERSE),
                   MakeEnumAccessor (&Ripv2::m_splitHorizonStrategy),
                   MakeEnumChecker (Ripv2::NO_SPLIT_HORIZON, "NoSplitHorizon",
                                    Ripv2::SPLIT_HORIZON, "SplitHorizon",
                                    Ripv2::POISON_REVERSE, "PoisonReverse"))
  ;
  return tid;
}

int64_t Ripv2::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);

  m_rng->SetStream (stream);
  return 1;
}

void Ripv2::DoInitialize ()
{
  NS_LOG_FUNCTION (this);

  bool addedGlobal = false;

  m_initialized = true;

  Time delay = m_unsolicitedUpdate + Seconds (m_rng->GetValue (0, 0.5*m_unsolicitedUpdate.GetSeconds ()) );
  m_nextUnsolicitedUpdate = Simulator::Schedule (delay, &Ripv2::SendUnsolicitedRouteUpdate, this);


  for (uint32_t i = 0 ; i < m_ipv4->GetNInterfaces (); i++)
    {
      bool activeInterface = false;
      if (m_interfaceExclusions.find (i) == m_interfaceExclusions.end ())
        {
          activeInterface = true;
        }

      for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); j++)
        {
          Ipv4InterfaceAddress address = m_ipv4->GetAddress (i, j);
          if (address.GetScope() == Ipv4InterfaceAddress::GLOBAL && activeInterface == true)
            {
              NS_LOG_LOGIC ("Ripv2: adding socket to " << address.GetLocal ());
              TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
              Ptr<Node> theNode = GetObject<Node> ();
              Ptr<Socket> socket = Socket::CreateSocket (theNode, tid);
              InetSocketAddress local = InetSocketAddress (address.GetLocal (), RIPV2_PORT);
              int ret = socket->Bind (local);
              NS_ASSERT_MSG (ret == 0, "Bind unsuccessful");
              socket->BindToNetDevice (m_ipv4->GetNetDevice (i));
              socket->ShutdownRecv ();
	      socket->SetIpRecvTtl(true);
              m_sendSocketList[socket] = i;
            }
        else if(m_ipv4->GetAddress(i,j).GetScope() == Ipv4InterfaceAddress::GLOBAL)
            {
               addedGlobal = true;
            }
        }
    }

  if (!m_recvSocket)
    {
      NS_LOG_LOGIC ("Ripv2: adding receiving socket");
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      Ptr<Node> theNode = GetObject<Node> ();
      m_recvSocket = Socket::CreateSocket (theNode, tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), RIPV2_PORT);
      m_recvSocket->Bind (local);
      m_recvSocket->SetRecvCallback (MakeCallback (&Ripv2::Receive, this));
	  m_recvSocket->SetIpRecvTtl(true);
      m_recvSocket->SetRecvPktInfo (true);
    }
  if (addedGlobal)
    {
      Time delay = Seconds (m_rng->GetValue (m_minTriggeredUpdateDelay.GetSeconds (), m_maxTriggeredUpdateDelay.GetSeconds ()));
      m_nextTriggeredUpdate = Simulator::Schedule (delay, &Ripv2::DoSendRouteUpdate, this, false);
    }

  delay = Seconds (m_rng->GetValue (0.01, m_startupDelay.GetSeconds ()));
  m_nextTriggeredUpdate = Simulator::Schedule (delay, &Ripv2::SendRouteRequest, this);

  Ipv4RoutingProtocol::DoInitialize ();
}

Ptr<Ipv4Route> Ripv2::RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION (this << header << oif);

  Ipv4Address destination = header.GetDestination ();
  Ptr<Ipv4Route> rtentry = 0;

  if (destination.IsMulticast ())
    {
      // Note:  Multicast routes for outbound packets are stored in the
      // normal unicast table.  An implication of this is that it is not
      // possible to source multicast datagrams on multiple interfaces.
      // This is a well-known property of sockets implementation on
      // many Unix variants.
      // So, we just log it and fall through to LookupStatic ()
      NS_LOG_LOGIC ("RouteOutput (): Multicast destination");
    }

  rtentry = Lookup (destination, oif);
  if (rtentry)
    {
      sockerr = Socket::ERROR_NOTERROR;
    }
  else
    {
      sockerr = Socket::ERROR_NOROUTETOHOST;
    }
  return rtentry;
}

bool Ripv2::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                        UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                        LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << p << header << header.GetSource () << header.GetDestination () << idev);

  NS_ASSERT (m_ipv4 != 0);
  // Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);
  Ipv4Address dst = header.GetDestination ();

  if (dst.IsMulticast ())
    {
      NS_LOG_LOGIC ("Multicast route not supported by Ripv2");
      return false; // Let other routing protocols try to handle this
    }

  /// \todo  Configurable option to enable \RFC{1222} Strong End System Model
  // Right now, we will be permissive and allow a source to send us
  // a packet to one of our other interface addresses; that is, the
  // destination unicast address does not match one of the iif addresses,
  // but we check our other interfaces.  This could be an option
  // (to remove the outer loop immediately below and just check iif).
  for (uint32_t j = 0; j < m_ipv4->GetNInterfaces (); j++)
    {
      for (uint32_t i = 0; i < m_ipv4->GetNAddresses (j); i++)
        {
          Ipv4InterfaceAddress iaddr = m_ipv4->GetAddress (j, i);
          Ipv4Address addr = iaddr.GetLocal ();
          if (addr.IsEqual (header.GetDestination ()))
            {
              if (j == iif)
                {
                  NS_LOG_LOGIC ("For me (destination " << addr << " match)");
                }
              else
                {
                  NS_LOG_LOGIC ("For me (destination " << addr << " match) on another interface " << header.GetDestination ());
                }
              lcb (p, header, iif);
              return true;
            }
          NS_LOG_LOGIC ("Address " << addr << " not a match");
        }
    }
  // Check if input device supports IP forwarding
  if (m_ipv4->IsForwarding (iif) == false)
    {
      NS_LOG_LOGIC ("Forwarding disabled for this interface");
      ecb (p, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }
  // Next, try to find a route
  NS_LOG_LOGIC ("Unicast destination");
  Ptr<Ipv4Route> rtentry = Lookup (header.GetDestination ());

  if (rtentry != 0)
    {
      NS_LOG_LOGIC ("Found unicast destination- calling unicast callback");
      ucb (rtentry, p, header);  // unicast forwarding callback
      return true;
    }
  else
    {
      NS_LOG_LOGIC ("Did not find unicast destination- returning false");
      return false; // Let other routing protocols try to handle this
    }
}

void Ripv2::NotifyInterfaceUp (uint32_t i)
{
  NS_LOG_FUNCTION (this << i);

  for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); j++)
    {
      Ipv4InterfaceAddress address = m_ipv4->GetAddress (i, j);
      Ipv4Mask networkMask = address.GetMask();
      Ipv4Address networkAddress = address.GetLocal ().CombineMask(networkMask);

      if (networkAddress != Ipv4Address::GetAny () && networkMask != Ipv4Mask::GetZero ())
        {
          if (networkMask == Ipv4Mask::GetOnes ())
            {
              /* host route */
              AddNetworkRouteTo (networkAddress, Ipv4Mask::GetOnes (), 0);
            }
          else
            {
              AddNetworkRouteTo (networkAddress, networkMask, i);
            }
        }
    }

  if (!m_initialized)
    {
      return;
    }


  bool sendSocketFound = false;
  for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
    {
      if (iter->second == i)
        {
          sendSocketFound = true;
          break;
        }
    }

  bool activeInterface = false;
  if (m_interfaceExclusions.find (i) == m_interfaceExclusions.end ())
    {
      activeInterface = true;
    }

  for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); j++)
    {
      Ipv4InterfaceAddress address = m_ipv4->GetAddress (i, j);

      Ipv4Address networkAddress = address.GetLocal ().CombineMask (address.GetMask ());
      Ipv4Mask networkMask = address.GetMask ();
      AddNetworkRouteTo (networkAddress, networkMask, i);

      if (address.GetScope () == Ipv4InterfaceAddress::LINK && sendSocketFound == false && activeInterface == true)
        {
          NS_LOG_LOGIC ("Ripv2: adding sending socket to " << address.GetLocal ());
          TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
          Ptr<Node> theNode = GetObject<Node> ();
          Ptr<Socket> socket = Socket::CreateSocket (theNode, tid);
          InetSocketAddress local = InetSocketAddress (address.GetLocal (), RIPV2_PORT);
          socket->Bind (local);
          socket->BindToNetDevice (m_ipv4->GetNetDevice (i));
          socket->ShutdownRecv ();
          socket->SetIpRecvTtl (true);
          m_sendSocketList[socket] = i;
        }
     else if (address.GetScope () == Ipv4InterfaceAddress::GLOBAL)
       {
          SendTriggeredRouteUpdate ();
       }
    }

  if (!m_recvSocket)
    {
      NS_LOG_LOGIC ("Ripv2: adding receiving socket");
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      Ptr<Node> theNode = GetObject<Node> ();
      m_recvSocket = Socket::CreateSocket (theNode, tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), RIPV2_PORT);
      m_recvSocket->Bind (local);
      m_recvSocket->SetRecvCallback (MakeCallback (&Ripv2::Receive, this));
      m_recvSocket->SetIpRecvTtl(true);
      m_recvSocket->SetRecvPktInfo (true);
    }
}

void Ripv2::NotifyInterfaceDown (uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);

  /* remove all routes that are going through this interface */
  for (RoutesI it = m_routes.begin (); it != m_routes.end (); it++)
    {
      if (it->first->GetInterface () == interface)
        {
          InvalidateRoute (it->first);
        }
    }

  for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
    {
      NS_LOG_INFO ("Checking socket for interface " << interface);
      if (iter->second == interface)
        {
          NS_LOG_INFO ("Removed socket for interface " << interface);
          iter->first->Close ();
          m_sendSocketList.erase (iter);
          break;
        }
    }

  if (m_interfaceExclusions.find (interface) == m_interfaceExclusions.end ())
    {
      SendTriggeredRouteUpdate ();
    }
}

void Ripv2::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);

  if (!m_ipv4->IsUp (interface))
    {
      return;
    }

  if (m_interfaceExclusions.find (interface) != m_interfaceExclusions.end ())
    {
      return;
    }

  Ipv4Address networkAddress = address.GetLocal ().CombineMask (address.GetMask ());
  Ipv4Mask networkMask = address.GetMask ();

  if (address.GetLocal () != Ipv4Address::GetAny () && address.GetMask () != Ipv4Mask::GetZero ())
    {
      AddNetworkRouteTo (networkAddress, networkMask, interface);
    }

  SendTriggeredRouteUpdate ();
}

void Ripv2::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);

  if (!m_ipv4->IsUp (interface))
    {
      return;
    }
  Ipv4Address networkAddress = address.GetLocal ().CombineMask (address.GetMask ());
  Ipv4Mask networkMask = address.GetMask ();

  // Remove all routes that are going through this interface
  // which reference this network
  for (RoutesI it = m_routes.begin (); it != m_routes.end (); it++)
    {
      if (it->first->GetInterface () == interface
          && it->first->IsNetwork ()
          && it->first->GetDestNetwork () == networkAddress
          && it->first->GetDestNetworkMask () == networkMask)
        {
          InvalidateRoute (it->first);
        }
    }

  if (m_interfaceExclusions.find (interface) == m_interfaceExclusions.end ())
    {
      SendTriggeredRouteUpdate ();
    }

}

void Ripv2::NotifyAddRoute (Ipv4Address dst, Ipv4Mask mask, Ipv4Address nextHop, uint32_t interface)
{
  NS_LOG_INFO (this << dst << mask << nextHop << interface);
  // \todo this can be used to add delegate routes
}
 
void Ripv2::NotifyRemoveRoute (Ipv4Address dst, Ipv4Mask mask, Ipv4Address nextHop, uint32_t interface)
{
  NS_LOG_FUNCTION (this << dst << mask << nextHop << interface);
  // \todo this can be used to delete delegate routes
}

void Ripv2::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_FUNCTION (this << ipv4);

  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  uint32_t i = 0;
  m_ipv4 = ipv4;

  for (i = 0; i < m_ipv4->GetNInterfaces (); i++)
    {
      if (m_ipv4->IsUp (i))
        {
          NotifyInterfaceUp (i);
        }
      else
        {
          NotifyInterfaceDown (i);
        }
    }
}
void Ripv2::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
  NS_LOG_FUNCTION (this << stream);

  std::ostream* os = stream->GetStream ();

  *os << "Node: " << m_ipv4->GetObject<Node> ()->GetId ()
      << " Time: " << Simulator::Now ().GetSeconds () << "s "
      << "Ripv2 routing table" << std::endl;

  if (!m_routes.empty ())
    {
      *os << "Destination      Mask                   Next Hop      Flag  Met   If" << std::endl;
      for (RoutesCI it = m_routes.begin (); it != m_routes.end (); it++)
        {
          Ripv2RoutingTableEntry* route = it->first;
          Ripv2RoutingTableEntry::Status_e status = route->GetRouteStatus();

          if (status == Ripv2RoutingTableEntry::RIPV2_VALID)
            {
              std::ostringstream dest, mask, gw, flags;

              dest << route->GetDest ();
              *os << std::setiosflags (std::ios::left) << std::setw (17) << dest.str ();

              mask << route->GetDestNetworkMask ();
              *os << std::setiosflags (std::ios::left) << std::setw (23) << mask.str ();

              gw << route->GetGateway ();
              *os << std::setiosflags (std::ios::left) << std::setw (14) << gw.str ();
              flags << "U";
              if (route->IsHost ())
                {
                  flags << "H";
                }
              else if (route->IsGateway ())
                {
                  flags << "G";
                }
              *os << std::setiosflags (std::ios::left) << std::setw (6) << flags.str ();
              *os << std::setiosflags (std::ios::left) << std::setw (6) << int32_t(route->GetRouteMetric ());

              if (Names::FindName (m_ipv4->GetNetDevice (route->GetInterface ())) != "")
                {
                  *os << Names::FindName (m_ipv4->GetNetDevice (route->GetInterface ()));
                }
              else
                {
                  *os << route->GetInterface ();
                }
              *os << std::endl;
            }
        }
    }
}

void Ripv2::DoDispose ()
{
  NS_LOG_FUNCTION (this);

  for (RoutesI j = m_routes.begin ();  j != m_routes.end (); j = m_routes.erase (j))
    {
      delete j->first;
    }
  m_routes.clear ();

  m_nextTriggeredUpdate.Cancel ();
  m_nextUnsolicitedUpdate.Cancel ();
  m_nextTriggeredUpdate = EventId ();
  m_nextUnsolicitedUpdate = EventId ();

  for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
    {
      iter->first->Close ();
    }
  m_sendSocketList.clear ();

  m_recvSocket->Close ();
  m_recvSocket = 0;

  m_ipv4 = 0;

  Ipv4RoutingProtocol::DoDispose ();
}

Ptr<Ipv4Route> Ripv2::Lookup (Ipv4Address dst, Ptr<NetDevice> interface)
{
  NS_LOG_FUNCTION (this << dst << interface);

  Ptr<Ipv4Route> rtentry = 0;
  uint16_t longestMask = 0;
 /* when sending on multicast, there have to be interface specified */
  if (dst.IsLocalMulticast())
    {
      NS_ASSERT_MSG (interface, "Try to send on link multicast address, and no interface index is given!");
      rtentry = Create<Ipv4Route> ();
      rtentry->SetSource (m_ipv4->SelectSourceAddress(interface, dst, Ipv4InterfaceAddress::GLOBAL));
      rtentry->SetDestination (dst);
      rtentry->SetGateway (Ipv4Address::GetZero ());
      rtentry->SetOutputDevice (interface);
      return rtentry;
    }
  for (RoutesI it = m_routes.begin (); it != m_routes.end (); it++)
    {
      Ripv2RoutingTableEntry* j = it->first;

      if (j->GetRouteStatus () == Ripv2RoutingTableEntry::RIPV2_VALID)
        {
          Ipv4Mask mask = j->GetDestNetworkMask ();
          uint16_t maskLen = mask.GetPrefixLength ();
          Ipv4Address entry = j->GetDestNetwork ();

          NS_LOG_LOGIC ("Searching for route to " << dst << ", mask length  " << maskLen);

          if (mask.IsMatch (dst, entry))
            {
              NS_LOG_LOGIC ("Found global network route " << j << ", mask length " << maskLen);

              /* if interface is given, check the route will output on this interface */
              if (!interface || interface == m_ipv4->GetNetDevice (j->GetInterface ()))
                {
                  if (maskLen < longestMask)
                    {
                      NS_LOG_LOGIC ("Previous match longer, skipping");
                      continue;
                    }

                  longestMask = maskLen;

                  Ipv4RoutingTableEntry* route = j;
                  uint32_t interfaceIdx = route->GetInterface ();
                  rtentry = Create<Ipv4Route> ();

                  if (route->GetGateway ().IsAny ())
                    {
                      rtentry->SetSource (m_ipv4->SelectSourceAddress (m_ipv4->GetNetDevice(interfaceIdx), route->GetDest (), Ipv4InterfaceAddress::GLOBAL));
                    }
                  else if (route->GetDest ().IsAny ())
                   {
                      rtentry->SetSource (m_ipv4->SelectSourceAddress (m_ipv4->GetNetDevice(interfaceIdx), dst, Ipv4InterfaceAddress::GLOBAL));
                    }
                  else
                    {
                      rtentry->SetSource (m_ipv4->SelectSourceAddress (m_ipv4->GetNetDevice(interfaceIdx), route->GetDest (), Ipv4InterfaceAddress::GLOBAL));
                    }

                  rtentry->SetDestination (route->GetDest ());
                  rtentry->SetGateway (route->GetGateway ());
                  rtentry->SetOutputDevice (m_ipv4->GetNetDevice (interfaceIdx));
                }
            }
        }
    }

  if (rtentry)
    {
      NS_LOG_LOGIC ("Matching route via " << rtentry->GetDestination () << " (through " << rtentry->GetGateway () << ") at the end");
    }
  return rtentry;
}

void Ripv2::AddNetworkRouteTo (Ipv4Address network, Ipv4Mask networkMask, Ipv4Address nextHop, uint32_t interface)
{
  NS_LOG_FUNCTION (this << network << networkMask << nextHop << interface );

  Ripv2RoutingTableEntry* route = new Ripv2RoutingTableEntry (network, networkMask, nextHop, interface);
  route->SetRouteMetric (1);
  route->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
  route->SetRouteChanged (true);

  m_routes.push_back (std::make_pair (route, EventId ()));
}

void Ripv2::AddNetworkRouteTo (Ipv4Address network, Ipv4Mask networkMask, uint32_t interface)
{
  NS_LOG_FUNCTION (this << network << networkMask << interface);

  Ripv2RoutingTableEntry* route = new Ripv2RoutingTableEntry (network, networkMask, interface);
  route->SetRouteMetric (1);
  route->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
  route->SetRouteChanged (true);

  m_routes.push_back (std::make_pair (route, EventId ()));
}

void Ripv2::InvalidateRoute (Ripv2RoutingTableEntry *route)
{
  NS_LOG_FUNCTION (this << *route);

  for (RoutesI it = m_routes.begin (); it != m_routes.end (); it++)
    {
      if (it->first == route)
        {
          route->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_INVALID);
          route->SetRouteMetric (16);
          route->SetRouteChanged (true);
          if (it->second.IsRunning ())
            {
              it->second.Cancel ();
            }
          it->second = Simulator::Schedule (m_garbageCollectionDelay, &Ripv2::DeleteRoute, this, route);
          return;
        }
    }
  NS_ABORT_MSG ("Ripv2::InvalidateRoute - cannot find the route to update");
}

void Ripv2::DeleteRoute (Ripv2RoutingTableEntry *route)
{
  NS_LOG_FUNCTION (this << *route);

  for (RoutesI it = m_routes.begin (); it != m_routes.end (); it++)
    {
      if (it->first == route)
        {
          delete route;
          m_routes.erase (it);
          return;
        }
    }
  NS_ABORT_MSG ("Ripv2::DeleteRoute - cannot find the route to delete");
}

void Ripv2::Receive (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);

  Ptr<Packet> packet = socket->Recv ();
  NS_LOG_INFO ("Received " << *packet);

  Ipv4PacketInfoTag interfaceInfo;
  if (!packet->RemovePacketTag (interfaceInfo))
    {
      NS_ABORT_MSG ("No incoming interface on Ripv2 message, aborting.");
    }
  uint32_t incomingIf = interfaceInfo.GetRecvIf ();
  Ptr<Node> node = this->GetObject<Node> ();
  Ptr<NetDevice> dev = node->GetDevice (incomingIf);
  uint32_t ipInterfaceIndex = m_ipv4->GetInterfaceForDevice (dev);

  SocketIpTtlTag timetoliveTag;
  if (!packet->RemovePacketTag (timetoliveTag))
    {
      NS_ABORT_MSG ("No incoming Hop Count on Ripv2 message, aborting.");
    }
  uint8_t ttl = timetoliveTag.GetTtl ();

  SocketAddressTag tag;
  if (!packet->RemovePacketTag (tag))
    {
      NS_ABORT_MSG ("No incoming sender address on Ripv2 message, aborting.");
    }
  Ipv4Address senderAddress = InetSocketAddress::ConvertFrom (tag.GetAddress ()).GetIpv4 ();
  uint16_t senderPort = InetSocketAddress::ConvertFrom (tag.GetAddress ()).GetPort ();

  int32_t interfaceForAddress = m_ipv4->GetInterfaceForAddress (senderAddress);
  if (interfaceForAddress != -1)
    {
      NS_LOG_LOGIC ("Ignoring a packet sent by myself.");
      return;
    }

  Ripv2Header hdr;
  packet->RemoveHeader (hdr);

  if (hdr.GetCommand () == Ripv2Header::RESPONSE)
    {
      HandleResponses (hdr, senderAddress, ipInterfaceIndex, ttl);
    }
  else if (hdr.GetCommand () == Ripv2Header::REQUEST)
    {
      HandleRequests (hdr, senderAddress, senderPort, ipInterfaceIndex, ttl);
    }
  else
    {
      NS_LOG_LOGIC ("Ignoring message with unknown command: " << int (hdr.GetCommand ()));
    }
  return;
}

void Ripv2::HandleRequests (Ripv2Header requestHdr, Ipv4Address senderAddress, uint16_t senderPort, uint32_t incomingInterface, uint8_t ttl)
{
  NS_LOG_FUNCTION (this << senderAddress << int (senderPort) << incomingInterface << int (ttl) << requestHdr);

  std::list<Ripv2Rte> rtes = requestHdr.GetRteList ();

  if (rtes.empty ())
    {
      return;
    }

  // check if it's a request for the full table from a neighbor
  // one entry in the request,
  // address family identifier of zero 
  // metric of infinity 
  if (rtes.size () == 1 && ttl == 255)
    {
      if (rtes.begin ()->GetIpAddress () == Ipv4Address::GetAny () &&
          rtes.begin ()->GetSubnetMask () == Ipv4Mask::GetZero () &&
          rtes.begin ()->GetRouteMetric () == 16) 
        {
          // Output whole thing. Use Split Horizon
          if (m_interfaceExclusions.find (incomingInterface) == m_interfaceExclusions.end ())
            {
              // we use one of the sending sockets, as they're bound to the right interface
              // and the local address might be used on different interfaces.
              Ptr<Socket> sendingSoket;
              for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
                {
                  if (iter->second == incomingInterface)
                    {
                      sendingSoket = iter->first;
                    }
                }
              NS_ASSERT_MSG (sendingSoket, "HandleRequest - Impossible to find a socket to send the reply");

              //uint16_t mtu = m_ipv4->GetMtu (incomingInterface);
              uint16_t maxRte = 25;

              Ptr<Packet> p = Create<Packet> ();
              SocketIpTtlTag tag;
              p->RemovePacketTag (tag);
              tag.SetTtl (255);
              p->AddPacketTag (tag);

              Ripv2Header hdr;
              hdr.SetCommand (Ripv2Header::RESPONSE);

              for (RoutesI rtIter = m_routes.begin (); rtIter != m_routes.end (); rtIter++)
                {
                  bool splitHorizoning = (rtIter->first->GetInterface () == incomingInterface);

                  Ipv4InterfaceAddress rtDestAddr = Ipv4InterfaceAddress(rtIter->first->GetDestNetwork (), rtIter->first->GetDestNetworkMask ());

                  bool isGlobal = (rtDestAddr.GetScope () == Ipv4InterfaceAddress::GLOBAL);
                  bool isDefaultRoute = ((rtIter->first->GetDestNetwork () == Ipv4Address::GetAny ()) &&
                      (rtIter->first->GetDestNetworkMask () == Ipv4Mask::GetZero ()) &&
                      (rtIter->first->GetInterface () != incomingInterface));

                  if ((isGlobal || isDefaultRoute) &&
                      (rtIter->first->GetRouteStatus () == Ripv2RoutingTableEntry::RIPV2_VALID) )
                    {
                      Ripv2Rte rte;
                      rte.SetIpAddress (rtIter->first->GetDestNetwork ());
                      rte.SetSubnetMask (rtIter->first->GetDestNetworkMask ());
                      if (m_splitHorizonStrategy == POISON_REVERSE && splitHorizoning)
                        {
                          rte.SetRouteMetric (16);
                        }
                      else
                        {
                          rte.SetRouteMetric (rtIter->first->GetRouteMetric ());
                        }
                      rte.SetRouteTag (rtIter->first->GetRouteTag ());
                      if ((m_splitHorizonStrategy != SPLIT_HORIZON) ||
                          (m_splitHorizonStrategy == SPLIT_HORIZON && !splitHorizoning))
                        {
                          hdr.AddRte (rte);
                        }
                    }
                  if (hdr.GetRteNumber () == maxRte)
                    {
                      p->AddHeader (hdr);
                      NS_LOG_DEBUG ("SendTo: " << *p);
                      sendingSoket->SendTo (p, 0, InetSocketAddress (senderAddress, RIPV2_PORT));
                      p->RemoveHeader (hdr);
                      hdr.ClearRtes ();
                    }
                }
              if (hdr.GetRteNumber () > 0)
                {
                  p->AddHeader (hdr);
                  NS_LOG_DEBUG ("SendTo: " << *p);
                  sendingSoket->SendTo (p, 0, InetSocketAddress (senderAddress, RIPV2_PORT));
                }
            }
        }
    }
  else
    {
      // note: we got the request as a single packet, so no check is necessary for MTU limit

      // we use one of the sending sockets, as they're bound to the right interface
      // and the local address might be used on different interfaces.
      Ptr<Socket> sendingSoket;
     if (ttl == 255)
        {
          for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
            {
              if (iter->second == incomingInterface)
                {
                  sendingSoket = iter->first;
                }
            }
        }
      else
        {
          sendingSoket = m_recvSocket;
        }

      Ptr<Packet> p = Create<Packet> ();
      SocketIpTtlTag tag;
      p->RemovePacketTag (tag);
      tag.SetTtl (255);
      p->AddPacketTag (tag);

      Ripv2Header hdr;
      hdr.SetCommand (Ripv2Header::RESPONSE);

      for (std::list<Ripv2Rte>::iterator iter = rtes.begin ();
          iter != rtes.end (); iter++)
        {
          bool found = false;
          for (RoutesI rtIter = m_routes.begin (); rtIter != m_routes.end (); rtIter++)
            {
                if (rtIter->first->GetRouteStatus () == Ripv2RoutingTableEntry::RIPV2_VALID)
                {
                  Ipv4Address requestedAddress = iter->GetIpAddress();
				  Ipv4Mask requesteMask=iter->GetSubnetMask();
                  Ipv4Address rtAddress = rtIter->first->GetDestNetwork ();
                  Ipv4Mask rtMask=rtIter->first->GetDestNetworkMask();

                  if ((requestedAddress == rtAddress)&&(requesteMask==rtMask))
                    {
                      iter->SetRouteMetric (rtIter->first->GetRouteMetric ());
                      iter->SetRouteTag (rtIter->first->GetRouteTag ());
                      hdr.AddRte (*iter);
                      found = true;
                      break;
                    }
                }
            }
          if (!found)
            {
              iter->SetRouteMetric (16);
              iter->SetRouteTag (0);
              hdr.AddRte (*iter);
            }
        }
      p->AddHeader (hdr);
      NS_LOG_DEBUG ("SendTo: " << *p);
      sendingSoket->SendTo (p, 0, InetSocketAddress (senderAddress, senderPort));
    }

}

void Ripv2::HandleResponses (Ripv2Header hdr, Ipv4Address senderAddress, uint32_t incomingInterface, uint8_t ttl)
{
  NS_LOG_FUNCTION (this << senderAddress << incomingInterface << int (ttl) << hdr);

  if (m_interfaceExclusions.find (incomingInterface) != m_interfaceExclusions.end ())
    {
      NS_LOG_LOGIC ("Ignoring an update message from an excluded interface: " << incomingInterface);
      return;
    }
//direct
  if (ttl != 255)
    {
      NS_LOG_LOGIC ("Ignoring an update message with suspicious hop count: " << int (ttl));
      return;
    }

  std::list<Ripv2Rte> rtes = hdr.GetRteList ();

  // validate the RTEs before processing
  for (std::list<Ripv2Rte>::iterator iter = rtes.begin ();
      iter != rtes.end (); iter++)
    {
      if (iter->GetRouteMetric () == 0 || iter->GetRouteMetric () > 16)
        {
          NS_LOG_LOGIC ("Ignoring an update message with malformed metric: " << int32_t (iter->GetRouteMetric ()));
          return;
        }
      if (iter->GetSubnetMask ().GetPrefixLength() > 32)
        {
          NS_LOG_LOGIC ("Ignoring an update message with malformed prefix length: " << int (iter->GetSubnetMask ().GetPrefixLength ()));
          return;
        }
      if (iter->GetIpAddress ().IsEqual (Ipv4Address::GetLoopback()) ||
          //iter->GetIpAddress ().IsLinkLocal () ||
          iter->GetIpAddress().IsMulticast ())
        {
          NS_LOG_LOGIC ("Ignoring an update message with wrong prefixes: " << iter->GetIpAddress ());
          return;
        }	
    }

  bool changed = false;

  for (std::list<Ripv2Rte>::iterator iter = rtes.begin ();
      iter != rtes.end (); iter++)
    {
      Ipv4Mask rteMask = iter->GetSubnetMask();
      Ipv4Address rteAddr = iter->GetIpAddress().CombineMask(rteMask);

      NS_LOG_LOGIC ("Processing RTE " << *iter);

      uint32_t interfaceMetric = 1;
      if (m_interfaceMetrics.find (incomingInterface) != m_interfaceMetrics.end ())
        {
          interfaceMetric = m_interfaceMetrics[incomingInterface];
        }
      uint32_t rteMetric = std::min (iter->GetRouteMetric () + interfaceMetric, uint32_t(16));
      RoutesI it;
      bool found = false;
      for (it = m_routes.begin (); it != m_routes.end (); it++)
        {
          if (it->first->GetDestNetwork () == rteAddr &&
              it->first->GetDestNetworkMask () == rteMask)
            {
              found = true;
              if (rteMetric < it->first->GetRouteMetric ())
                {
                  if (senderAddress != it->first->GetGateway ())
                    {
                      Ripv2RoutingTableEntry* route = new Ripv2RoutingTableEntry (rteAddr, rteMask, senderAddress, incomingInterface);
                      delete it->first;
                      it->first = route;
                    }
                  it->first->SetRouteMetric (rteMetric);
                  it->first->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
                  it->first->SetRouteTag (iter->GetRouteTag ());
                  it->first->SetRouteChanged (true);
                  it->second.Cancel ();
                  it->second = Simulator::Schedule (m_timeoutDelay, &Ripv2::InvalidateRoute, this, it->first);
                  changed = true;
                }
              else if (rteMetric == it->first->GetRouteMetric ())
                {
                  if (senderAddress == it->first->GetGateway ())
                    {
                      it->second.Cancel ();
                      it->second = Simulator::Schedule (m_timeoutDelay, &Ripv2::InvalidateRoute, this, it->first);
                    }
                  else
                    {
                      if (Simulator::GetDelayLeft (it->second) < m_timeoutDelay/2)
                        {
                          Ripv2RoutingTableEntry* route = new Ripv2RoutingTableEntry (rteAddr, rteMask, senderAddress, incomingInterface);
                          route->SetRouteMetric (rteMetric);
                          route->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
                          route->SetRouteTag (iter->GetRouteTag ());
                          route->SetRouteChanged (true);
                          delete it->first;
                          it->first = route;
                          it->second.Cancel ();
                          it->second = Simulator::Schedule (m_timeoutDelay, &Ripv2::InvalidateRoute, this, route);
                          changed = true;
                        }
                    }
                }
              else if (rteMetric > it->first->GetRouteMetric () && senderAddress == it->first->GetGateway ())
                {
                  it->second.Cancel ();
                  if (rteMetric < 16)
                    {
                      it->first->SetRouteMetric (rteMetric);
                      it->first->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
                      it->first->SetRouteTag (iter->GetRouteTag ());
                      it->first->SetRouteChanged (true);
                      it->second.Cancel ();
                      it->second = Simulator::Schedule (m_timeoutDelay, &Ripv2::InvalidateRoute, this, it->first);
                    }
                  else
                    {
                      InvalidateRoute (it->first);
                    }
                  changed = true;
                }
            }
        }
      if (!found && rteMetric != 16)
        {
          NS_LOG_LOGIC ("Received a RTE with new route, adding.");

          Ripv2RoutingTableEntry* route = new Ripv2RoutingTableEntry (rteAddr, rteMask, senderAddress, incomingInterface);
          route->SetRouteMetric (rteMetric);
          route->SetRouteStatus (Ripv2RoutingTableEntry::RIPV2_VALID);
          route->SetRouteChanged (true);
          m_routes.push_front (std::make_pair (route, EventId ()));
          EventId invalidateEvent = Simulator::Schedule (m_timeoutDelay, &Ripv2::InvalidateRoute, this, route);
          (m_routes.begin ())->second = invalidateEvent;
          changed = true;
        }
    }

  if (changed)
    {
      SendTriggeredRouteUpdate ();
    }
}

void Ripv2::DoSendRouteUpdate (bool periodic)
{
  NS_LOG_FUNCTION (this << (periodic ? " periodic" : " triggered"));

  for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
    {
      uint32_t interface = iter->second;

      if (m_interfaceExclusions.find (interface) == m_interfaceExclusions.end ())
        {
          //uint16_t mtu = m_ipv4->GetMtu (interface);
          uint16_t maxRte = 25;

          Ptr<Packet> p = Create<Packet> ();
          SocketIpTtlTag tag;
          tag.SetTtl (255);
          p->AddPacketTag (tag);

          Ripv2Header hdr;
          hdr.SetCommand (Ripv2Header::RESPONSE);

          for (RoutesI rtIter = m_routes.begin (); rtIter != m_routes.end (); rtIter++)
            {
              bool splitHorizoning = (rtIter->first->GetInterface () == interface);
              Ipv4InterfaceAddress rtDestAddr = Ipv4InterfaceAddress(rtIter->first->GetDestNetwork (), rtIter->first->GetDestNetworkMask ());

              NS_LOG_DEBUG ("Processing RT " << rtDestAddr << " " << int(rtIter->first->IsRouteChanged ()));

              bool isGlobal = (rtDestAddr.GetScope () == Ipv4InterfaceAddress::GLOBAL);
              bool isDefaultRoute = ((rtIter->first->GetDestNetwork () == Ipv4Address::GetAny ()) &&
                  (rtIter->first->GetDestNetworkMask () == Ipv4Mask::GetZero ()) &&
                  (rtIter->first->GetInterface () != interface));

              if ((isGlobal || isDefaultRoute) &&(periodic || rtIter->first->IsRouteChanged ()))
                {
                  Ripv2Rte rte;
                  rte.SetIpAddress (rtIter->first->GetDestNetwork ());
                  rte.SetSubnetMask (rtIter->first->GetDestNetworkMask ());
                  if (m_splitHorizonStrategy == POISON_REVERSE && splitHorizoning)
                    {
                      rte.SetRouteMetric (16);
                    }
                  else
                    {
                      rte.SetRouteMetric (rtIter->first->GetRouteMetric ());
                    }
                  rte.SetRouteTag (rtIter->first->GetRouteTag ());
                  if (m_splitHorizonStrategy == SPLIT_HORIZON && !splitHorizoning)
                    {
                      hdr.AddRte (rte);
                    }
                  else if (m_splitHorizonStrategy != SPLIT_HORIZON)
                    {
                      hdr.AddRte (rte);
                    }
                }
              if (hdr.GetRteNumber () == maxRte)
                {
                  p->AddHeader (hdr);
                  NS_LOG_DEBUG ("SendTo: " << *p);
                  iter->first->SendTo (p, 0, InetSocketAddress (RIPV2_ALL_NODE, RIPV2_PORT));
                  p->RemoveHeader (hdr);
                  hdr.ClearRtes ();
                }
            }
          if (hdr.GetRteNumber () > 0)
            {
              p->AddHeader (hdr);
              NS_LOG_DEBUG ("SendTo: " << *p);
              iter->first->SendTo (p, 0, InetSocketAddress (RIPV2_ALL_NODE, RIPV2_PORT));
            }
        }
    }
  for (RoutesI rtIter = m_routes.begin (); rtIter != m_routes.end (); rtIter++)
    {
      rtIter->first->SetRouteChanged (false);
    }
}

void Ripv2::SendTriggeredRouteUpdate ()
{
  NS_LOG_FUNCTION (this);

  if (m_nextTriggeredUpdate.IsRunning())
    {
      NS_LOG_LOGIC ("Skipping Triggered Update due to cooldown");
      return;
    }

  // DoSendRouteUpdate (false);

  // note: The RFC states:
  //     After a triggered
  //     update is sent, a timer should be set for a random interval between 1
  //     and 5 seconds.  If other changes that would trigger updates occur
  //     before the timer expires, a single update is triggered when the timer
  //     expires.  The timer is then reset to another random value between 1
  //     and 5 seconds.  Triggered updates may be suppressed if a regular
  //     update is due by the time the triggered update would be sent.
  // Here we rely on this:
  // When an update occurs (either Triggered or Periodic) the "IsChanged ()"
  // route field will be cleared.
  // Hence, the following Triggered Update will be fired, but will not send
  // any route update.

  Time delay = Seconds (m_rng->GetValue (m_minTriggeredUpdateDelay.GetSeconds (), m_maxTriggeredUpdateDelay.GetSeconds ()));
  m_nextTriggeredUpdate = Simulator::Schedule (delay, &Ripv2::DoSendRouteUpdate, this, false);
}

void Ripv2::SendUnsolicitedRouteUpdate ()
{
  NS_LOG_FUNCTION (this);

  if (m_nextTriggeredUpdate.IsRunning())
    {
      m_nextTriggeredUpdate.Cancel ();
    }

  DoSendRouteUpdate (true);

  Time delay = m_unsolicitedUpdate + Seconds (m_rng->GetValue (0, 0.5*m_unsolicitedUpdate.GetSeconds ()) );
  m_nextUnsolicitedUpdate = Simulator::Schedule (delay, &Ripv2::SendUnsolicitedRouteUpdate, this);
}

std::set<uint32_t> Ripv2::GetInterfaceExclusions () const
{
  return m_interfaceExclusions;
}

void Ripv2::SetInterfaceExclusions (std::set<uint32_t> exceptions)
{
  NS_LOG_FUNCTION (this);

  m_interfaceExclusions = exceptions;
}

uint32_t Ripv2::GetInterfaceMetric (uint32_t interface) const
{
  NS_LOG_FUNCTION (this << interface);

  std::map<uint32_t, uint32_t>::const_iterator iter = m_interfaceMetrics.find (interface);
  if (iter != m_interfaceMetrics.end ())
    {
      return iter->second;
    }
  return 1;
}

void Ripv2::SetInterfaceMetric (uint32_t interface, uint32_t metric)
{
  NS_LOG_FUNCTION (this << interface << int (metric));

  if (metric < 16)
    {
      m_interfaceMetrics[interface] = metric;
    }
}

void Ripv2::SendRouteRequest ()
{
  NS_LOG_FUNCTION (this);

  Ptr<Packet> p = Create<Packet> ();
  SocketIpTtlTag tag;
  p->RemovePacketTag (tag);
  tag.SetTtl (255);
  p->AddPacketTag (tag);

  Ripv2Header hdr;
  hdr.SetCommand (Ripv2Header::REQUEST);

  Ripv2Rte rte;
  rte.SetIpAddress (Ipv4Address::GetAny ());
  rte.SetSubnetMask (Ipv4Mask::GetZero ());
  rte.SetRouteMetric (16);

  hdr.AddRte (rte);
  p->AddHeader (hdr);

  for (SocketListI iter = m_sendSocketList.begin (); iter != m_sendSocketList.end (); iter++ )
    {
      uint32_t interface = iter->second;

      if (m_interfaceExclusions.find (interface) == m_interfaceExclusions.end ())
        {
          NS_LOG_DEBUG ("SendTo: " << *p);
          iter->first->SendTo (p, 0, InetSocketAddress (RIPV2_ALL_NODE, RIPV2_PORT));
        }
    }
}

void Ripv2::AddDefaultRouteTo (Ipv4Address nextHop, uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);

  AddNetworkRouteTo (Ipv4Address::GetAny (), Ipv4Mask::GetZero (), nextHop, interface);
}


/*
 * Ripv2RoutingTableEntry
 */

Ripv2RoutingTableEntry::Ripv2RoutingTableEntry ()
  : m_tag (0), m_metric (16), m_status (RIPV2_INVALID), m_changed (false)
{
}

Ripv2RoutingTableEntry::Ripv2RoutingTableEntry (Ipv4Address network, Ipv4Mask networkMask, Ipv4Address nextHop, uint32_t interface)
  : Ipv4RoutingTableEntry ( Ipv4RoutingTableEntry::CreateNetworkRouteTo (network, networkMask, nextHop, interface) ),
    m_tag (0), m_metric (16), m_status (RIPV2_INVALID), m_changed (false)
{
}

Ripv2RoutingTableEntry::Ripv2RoutingTableEntry (Ipv4Address network, Ipv4Mask networkMask, uint32_t interface)
  : Ipv4RoutingTableEntry ( Ipv4RoutingTableEntry::CreateNetworkRouteTo (network, networkMask, interface) ),
    m_tag (0), m_metric (16), m_status (RIPV2_INVALID), m_changed (false)
{
}

Ripv2RoutingTableEntry::~Ripv2RoutingTableEntry ()
{
}


void Ripv2RoutingTableEntry::SetRouteTag (uint16_t routeTag)
{
  if (m_tag != routeTag)
    {
      m_tag = routeTag;
      m_changed = true;
    }
}

uint16_t Ripv2RoutingTableEntry::GetRouteTag () const
{
  return m_tag;
}

void Ripv2RoutingTableEntry::SetRouteMetric (uint32_t routeMetric)
{
  if (m_metric != routeMetric)
    {
      m_metric = routeMetric;
      m_changed = true;
    }
}

uint32_t Ripv2RoutingTableEntry::GetRouteMetric () const
{
  return m_metric;
}

void Ripv2RoutingTableEntry::SetRouteStatus (Status_e status)
{
  if (m_status != status)
    {
      m_status = status;
      m_changed = true;
    }
}

Ripv2RoutingTableEntry::Status_e Ripv2RoutingTableEntry::GetRouteStatus (void) const
{
  return m_status;
}

void Ripv2RoutingTableEntry::SetRouteChanged (bool changed)
{
  m_changed = changed;
}

bool Ripv2RoutingTableEntry::IsRouteChanged (void) const
{
  return m_changed;
}


std::ostream & operator << (std::ostream& os, const Ripv2RoutingTableEntry& rte)
{
  os << static_cast<const Ipv4RoutingTableEntry &>(rte);
  os << ", metric: " << uint32_t (rte.GetRouteMetric ()) << ", tag: " << uint16_t (rte.GetRouteTag ());
  return os;
}

}

