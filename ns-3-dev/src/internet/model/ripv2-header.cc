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

#include "ripv2-header.h"

namespace ns3 {

/*
 * Ripv2Rte
 */
NS_OBJECT_ENSURE_REGISTERED (Ripv2Rte)
  ;

Ripv2Rte::Ripv2Rte ()
  : m_AFI(2), m_tag(0), m_IpAddress("0.0.0.0"), m_SubnetMask("0.0.0.0"), m_NextHop("0.0.0.0"), m_metric(16)
{
}

TypeId Ripv2Rte::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ripv2Rte").SetParent<Header> ().AddConstructor<Ripv2Rte> ();
  return tid;
}

TypeId Ripv2Rte::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void Ripv2Rte::Print (std::ostream & os) const
{
  os << "AFI "<< int(m_AFI) << " Tag " << int(m_tag) << " Adress " << m_IpAddress << " Mask " << m_SubnetMask << " Metric " << int(m_metric);
}

uint32_t Ripv2Rte::GetSerializedSize () const
{
  return 20;
}

void Ripv2Rte::Serialize (Buffer::Iterator i) const
{
  i.WriteHtonU16(m_AFI);
  i.WriteHtonU16(m_tag);
  i.WriteHtonU32(m_IpAddress.Get());
  i.WriteHtonU32(m_SubnetMask.Get());
  i.WriteHtonU32(m_NextHop.Get());
  i.WriteHtonU32(m_metric);      
}

uint32_t Ripv2Rte::Deserialize (Buffer::Iterator i)
{
  uint32_t tmp;
  m_AFI = i.ReadNtohU16();
  m_tag = i.ReadNtohU16();
  tmp = i.ReadNtohU32();
  m_IpAddress.Set(tmp);
  tmp = i.ReadNtohU32();
  m_SubnetMask.Set(tmp);
  tmp = i.ReadNtohU32();
  m_NextHop.Set(tmp);
  m_metric = i.ReadNtohU32();

  return GetSerializedSize ();
}

void Ripv2Rte:: SetIpAddress (Ipv4Address ipaddress)
{
  m_IpAddress = ipaddress;
}

Ipv4Address Ripv2Rte::GetIpAddress () const
{
  return m_IpAddress;
}

void Ripv2Rte::SetSubnetMask (Ipv4Mask subnetmask)
{
  m_SubnetMask = subnetmask;
}

Ipv4Mask Ripv2Rte::GetSubnetMask () const
{
  return m_SubnetMask;
}

void Ripv2Rte:: SetNextHop (Ipv4Address nexthop)
{
  m_NextHop = nexthop;
}

Ipv4Address Ripv2Rte::GetNextHop () const
{
  return m_NextHop;
}

void Ripv2Rte::SetRouteTag (uint16_t routeTag)
{
  m_tag = routeTag;
}

uint16_t Ripv2Rte::GetRouteTag () const
{
  return m_tag;
}

void Ripv2Rte::SetRouteMetric (uint32_t routeMetric)
{
  m_metric = routeMetric;
}

uint32_t Ripv2Rte::GetRouteMetric () const
{
  return m_metric;
}


std::ostream & operator << (std::ostream & os, const Ripv2Rte & h)
{
  h.Print (os);
  return os;
}

/*
 * Ripv2Header
 */
NS_OBJECT_ENSURE_REGISTERED (Ripv2Header)
  ;

Ripv2Header::Ripv2Header ()
  : m_command (0)
{
}

TypeId Ripv2Header::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ripv2Header").SetParent<Header> ().AddConstructor<Ripv2Header> ();
  return tid;
}

TypeId Ripv2Header::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void Ripv2Header::Print (std::ostream & os) const
{
  os << "command " << int(m_command);
  for (std::list<Ripv2Rte>::const_iterator iter = m_rteList.begin ();
      iter != m_rteList.end (); iter ++)
    {
      os << " | ";
      iter->Print (os);
    }
}

uint32_t Ripv2Header::GetSerializedSize () const
{
  Ripv2Rte rte;
  return 4 + m_rteList.size () * rte.GetSerializedSize ();
}

void Ripv2Header::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;

  i.WriteU8 (uint8_t (m_command));
  i.WriteU8 (2);// Ripv2
  i.WriteU16 (0);

  for (std::list<Ripv2Rte>::const_iterator iter = m_rteList.begin ();
      iter != m_rteList.end (); iter ++)
    {
      iter->Serialize (i);
      i.Next(iter->GetSerializedSize ());
    }
}

uint32_t Ripv2Header::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;

  uint8_t temp;
  temp = i.ReadU8 ();
  if ((temp == REQUEST) || (temp == RESPONSE))
    {
      m_command = temp;
    }
  else
    {
      return 0;
    }

  temp = i.ReadU8 ();
  NS_ASSERT_MSG (temp == 2, "Ripv2 received a message with mismatch version, aborting.");

  uint16_t temp16 = i.ReadU16 ();
  NS_ASSERT_MSG (temp16 == 0, "Ripv2 received a message with invalid filled flags, aborting.");

  uint8_t rteNumber = (i.GetSize () - 4)/20;
  for (uint8_t n = 0; n < rteNumber; n++)
    {
      Ripv2Rte rte;
      i.Next (rte.Deserialize (i));
      m_rteList.push_back (rte);
    }

  return GetSerializedSize ();
}

void Ripv2Header::SetCommand (Ripv2Header::Command_e command)
{
  m_command = command;
}

Ripv2Header::Command_e Ripv2Header::GetCommand () const
{
  return Ripv2Header::Command_e (m_command);
}

void Ripv2Header::AddRte (Ripv2Rte rte)
{
  m_rteList.push_back (rte);
}

void Ripv2Header::ClearRtes ()
{
  m_rteList.clear ();
}

uint16_t Ripv2Header::GetRteNumber (void) const
{
  return m_rteList.size ();
}

std::list<Ripv2Rte> Ripv2Header::GetRteList (void) const
{
  return m_rteList;
}


std::ostream & operator << (std::ostream & os, const Ripv2Header & h)
{
  h.Print (os);
  return os;
}


}

