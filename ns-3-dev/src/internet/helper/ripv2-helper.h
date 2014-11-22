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

#ifndef RIPV2_HELPER_H
#define RIPV2_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/node.h"

namespace ns3 {

/**
 * \brief Helper class that adds RIPng routing to nodes.
 *
 * This class is expected to be used in conjunction with
 * ns3::InternetStackHelper::SetRoutingHelper
 *
 */
class Ripv2Helper : public Ipv4RoutingHelper
{
public:
  /*
   * Construct an Ripv2Helper to make life easier while adding RIPng
   * routing to nodes.
   */
  Ripv2Helper ();

  /**
   * \brief Construct an Ripv2Helper from another previously
   * initialized instance (Copy Constructor).
   */
  Ripv2Helper (const Ripv2Helper &);

  virtual ~Ripv2Helper ();

  /**
   * \returns pointer to clone of this Ipv4NixVectorHelper
   *
   * This method is mainly for internal use by the other helpers;
   * clients are expected to free the dynamic memory allocated by this method
   */
  Ripv2Helper* Copy (void) const;

  /**
   * \param node the node on which the routing protocol will run
   * \returns a newly-created routing protocol
   *
   * This method will be called by ns3::InternetStackHelper::Install
   */
  virtual Ptr<Ipv4RoutingProtocol> Create (Ptr<Node> node) const;

  /**
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set.
   *
   * This method controls the attributes of ns3::Ripng
   */
  void Set (std::string name, const AttributeValue &value);

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model. Return the number of streams (possibly zero) that
   * have been assigned. The Install() method should have previously been
   * called by the user.
   *
   * \param c NetDeviceContainer of the set of net devices for which the
   *          SixLowPanNetDevice should be modified to use a fixed stream
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this helper
   */
  int64_t AssignStreams (NodeContainer c, int64_t stream);

  /**
   * \brief Install a default route in the node.
   *
   * The traffic will be routed to the nextHop, located on the specified
   * interface, unless a more specific route is found.
   *
   * \param node the node
   * \param nextHop the next hop
   * \param interface the network interface
   */
  void SetDefaultRouter (Ptr<Node> node, Ipv4Address nextHop, uint32_t interface);

  /**
   * \brief Exclude an interface from RIPng protocol.
   *
   * You have to call this function \a before installing RIPng in the nodes.
   *
   * Note: the exclusion means that RIPng will not be propagated on that interface.
   * The network prefix on that interface will be still considered in RIPng.
   *
   * \param node the node
   * \param interface the network interface to be excluded
   */
  void ExcludeInterface (Ptr<Node> node, uint32_t interface);

  /**
   * \brief Set a metric for an interface.
   *
   * You have to call this function \a before installing RIPng in the nodes.
   *
   * Note: RIPng will apply the metric on route message reception.
   * As a consequence, interface metric should be set on the receiver.
   *
   * \param node the node
   * \param interface the network interface
   * \param metric the interface metric
   */
  void SetInterfaceMetric (Ptr<Node> node, uint32_t interface, uint32_t metric);

private:
  /**
   * \brief Assignment operator declared private and not implemented to disallow
   * assignment and prevent the compiler from happily inserting its own.
   */
  Ripv2Helper &operator = (const Ripv2Helper &o);

  ObjectFactory m_factory; //|< Object Factory

  std::map< Ptr<Node>, std::set<uint32_t> > m_interfaceExclusions; //!< Interface Exclusion set
  std::map< Ptr<Node>, std::map<uint32_t, uint32_t> > m_interfaceMetrics; //!< Interface Metric set
};

} // namespace ns3


#endif /* RIPV2_HELPER_H */

