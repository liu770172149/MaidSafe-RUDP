/*******************************************************************************
 *  Copyright 2012 MaidSafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of MaidSafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of MaidSafe.net. *
 ******************************************************************************/
// Original author: Christopher M. Kohlhoff (chris at kohlhoff dot com)

#ifndef MAIDSAFE_RUDP_CORE_MULTIPLEXER_H_
#define MAIDSAFE_RUDP_CORE_MULTIPLEXER_H_

#include <array>  // NOLINT
#include <vector>

#include "boost/asio/io_service.hpp"
#include "boost/asio/ip/udp.hpp"
#include "maidsafe/transport/transport.h"
#include "maidsafe/transport/rudp_dispatch_op.h"
#include "maidsafe/transport/rudp_dispatcher.h"
#include "maidsafe/transport/rudp_packet.h"
#include "maidsafe/transport/rudp_parameters.h"

namespace maidsafe {

namespace transport {

class RudpMultiplexer {
 public:
  explicit RudpMultiplexer(boost::asio::io_service &asio_service);  // NOLINT (Fraser)
  ~RudpMultiplexer();

  // Open the multiplexer as a client for the specified protocol.
  TransportCondition Open(const boost::asio::ip::udp &protocol);

  // Open the multiplexer as a server on the specified endpoint.
  TransportCondition Open(const boost::asio::ip::udp::endpoint &endpoint);

  // Whether the multiplexer is open.
  bool IsOpen() const;

  // Close the multiplexer.
  void Close();

  // Asynchronously receive a single packet and dispatch it.
  template <typename DispatchHandler>
  void AsyncDispatch(DispatchHandler handler) {
    RudpDispatchOp<DispatchHandler> op(handler, &socket_,
                                       boost::asio::buffer(receive_buffer_),
                                       &sender_endpoint_, &dispatcher_);
    socket_.async_receive_from(boost::asio::buffer(receive_buffer_),
                               sender_endpoint_, 0, op);
  }

  // Called by the acceptor or socket objects to send a packet. Returns true if
  // the data was sent successfully, false otherwise.
  template <typename Packet>
  TransportCondition SendTo(const Packet &packet,
                            const boost::asio::ip::udp::endpoint &endpoint) {
    std::array<unsigned char, RudpParameters::kUDPPayload> data;
    auto buffer = boost::asio::buffer(&data[0], RudpParameters::max_size);
    if (size_t length = packet.Encode(buffer)) {
      boost::system::error_code ec;
      socket_.send_to(boost::asio::buffer(buffer, length), endpoint, 0, ec);
      return ec ? kSendFailure : kSuccess;
    }
    return kSendFailure;
  }

 private:
  friend class RudpAcceptor;
  friend class RudpSocket;

  // Disallow copying and assignment.
  RudpMultiplexer(const RudpMultiplexer&);
  RudpMultiplexer &operator=(const RudpMultiplexer&);

  // The UDP socket used for all RUDP protocol communication.
  boost::asio::ip::udp::socket socket_;

  // Data members used to receive information about incoming packets.
  std::vector<unsigned char> receive_buffer_;
  boost::asio::ip::udp::endpoint sender_endpoint_;

  // Dispatcher keeps track of the active sockets and the acceptor.
  RudpDispatcher dispatcher_;
};

}  // namespace transport

}  // namespace maidsafe

#endif  // MAIDSAFE_RUDP_CORE_MULTIPLEXER_H_
