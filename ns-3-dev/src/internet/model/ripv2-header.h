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

#ifndef RIPV2_HEADER_H
#define RIPV2_HEADER_H

#include <list>
#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"


namespace ns3 {

/**
 * \ingroup ripng
 * \brief Rip Routing Table Entry (RTE) - see \RFC{2080}
 */
class Ripv2Rte : public Header
{
public:
  Ripv2Rte (void);

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Return the instance type identifier.
   * \return instance type ID
   */
  virtual TypeId GetInstanceTypeId (void) const;

  virtual void Print (std::ostream& os) const;

  /**
   * \brief Get the serialized size of the packet.
   * \return size
   */
  virtual uint32_t GetSerializedSize (void) const;

  /**
   * \brief Serialize the packet.
   * \param start Buffer iterator
   */
  virtual void Serialize (Buffer::Iterator start) const;

  /**
   * \brief Deserialize the packet.
   * \param start Buffer iterator
   * \return size of the packet
   */
  virtual uint32_t Deserialize (Buffer::Iterator start);


  /**
   * \brief Set the IpAddress
   * \param IpAddress the IpAddress
   */
  void SetIpAddress (Ipv4Address ipaddress);

  /**
   * \brief Get the IpAddress
   * \returns the IpAddress
   */
  Ipv4Address GetIpAddress (void) const;
    /**
   * \brief Set the SubnetMask
   * \param SubnetMask the SubnetMask
   */
  void SetSubnetMask (Ipv4Mask subnetmask);

  /**
   * \brief Get the SubnetMask
   * \returns the SubnetMask
   */
  Ipv4Mask GetSubnetMask (void) const;

    /**
   * \brief Set the NextHop
   * \param NextHop the NextHop
   */
  void SetNextHop (Ipv4Address nexthop);

  /**
   * \brief Get the NextHop
   * \returns the NextHop
   */
  Ipv4Address GetNextHop (void) const;
  
  /**
   * \brief Set the route tag
   * \param routeTag the route tag
   */
  void SetRouteTag (uint16_t routeTag);

  /**
   * \brief Get the route tag
   * \returns the route tag
   */
  uint16_t GetRouteTag (void) const;

  /**
   * \brief Set the route metric
   * \param routeMetric the route metric
   */
  void SetRouteMetric (uint32_t routeMetric);

  /**
   * \brief Get the route metric
   * \returns the route metric
   */
  uint32_t GetRouteMetric (void) const;


private:
  uint16_t m_AFI; // Address Family Identifier
  uint16_t m_tag; //!< route tag
  Ipv4Address m_IpAddress; //!<Ip Address
  Ipv4Mask m_SubnetMask; //!<SubnetMask
  Ipv4Address m_NextHop; //!<NextHop
  uint32_t m_metric; //!< route metric
};


/**
 * \ingroup ripng
 * \brief Ripv2Header - see \RFC{2080}
 */
class Ripv2Header : public Header
{
public:
  Ripv2Header (void);

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Return the instance type identifier.
   * \return instance type ID
   */
  virtual TypeId GetInstanceTypeId (void) const;

  virtual void Print (std::ostream& os) const;

  /**
   * \brief Get the serialized size of the packet.
   * \return size
   */
  virtual uint32_t GetSerializedSize (void) const;

  /**
   * \brief Serialize the packet.
   * \param start Buffer iterator
   */
  virtual void Serialize (Buffer::Iterator start) const;

  /**
   * \brief Deserialize the packet.
   * \param start Buffer iterator
   * \return size of the packet
   */
  virtual uint32_t Deserialize (Buffer::Iterator start);

  /**
   * Commands to be used in Ripv2 headers
   */
  enum Command_e
  {
    REQUEST = 0x1,
    RESPONSE = 0x2,
  };

  /**
   * \brief Set the command
   * \param command the command
   */
  void SetCommand (Command_e command);

  /**
   * \brief Get the command
   * \returns the command
   */
  Command_e GetCommand (void) const;

  /**
   * \brief Add a RTE to the message
   * \param rte the RTE
   */
  void AddRte (Ripv2Rte rte);

  /**
   * \brief Clear all the RTEs from the header
   */
  void ClearRtes ();

  /**
   * \brief Get the number of RTE included in the message
   * \returns the number of RTE in the message
   */
  uint16_t GetRteNumber (void) const;

  /**
   * \brief Get the list of the RTEs included in the message
   * \returns the list of the RTEs in the message
   */
  std::list<Ripv2Rte> GetRteList (void) const;

private:
  uint8_t m_command; //!< command type
  std::list<Ripv2Rte> m_rteList; //!< list of the RTEs in the message
};
  std::ostream & operator << (std::ostream & os, const Ripv2Header & h);
}

#endif /* RIPV2_HEADER_H */

