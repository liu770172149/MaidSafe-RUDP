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

#ifndef MAIDSAFE_RUDP_CORE_ACCEPTOR_H_
#define MAIDSAFE_RUDP_CORE_ACCEPTOR_H_

#include <memory>
#include <deque>
#include "boost/asio/buffer.hpp"
#include "boost/asio/deadline_timer.hpp"
#include "boost/asio/io_service.hpp"
#include "boost/asio/ip/udp.hpp"
#include "maidsafe/transport/transport.h"

#include "maidsafe/transport/rudp_accept_op.h"

namespace maidsafe {

namespace transport {

class RudpDispatcher;
class RudpMultiplexer;
class RudpSocket;

class RudpAcceptor {
 public:
  explicit RudpAcceptor(RudpMultiplexer &multiplexer);  // NOLINT (Fraser)
  ~RudpAcceptor();

  // Returns whether the acceptor is open.
  bool IsOpen() const;

  // Close the acceptor and cancel pending asynchronous operations.
  void Close();

  // Initiate an asynchronous operation to accept a new server-side connection.
  template <typename AcceptHandler>
  void AsyncAccept(RudpSocket &socket, AcceptHandler handler) {  // NOLINT (Fraser)
    RudpAcceptOp<AcceptHandler> op(handler, socket);
    waiting_accept_.async_wait(op);
    StartAccept(socket);
  }

 private:
  // Disallow copying and assignment.
  RudpAcceptor(const RudpAcceptor&);
  RudpAcceptor &operator=(const RudpAcceptor&);

  void StartAccept(RudpSocket &socket);  // NOLINT (Fraser)

  // Called by the RudpDispatcher when a new packet arrives for the acceptor.
  friend class RudpDispatcher;
  void HandleReceiveFrom(const boost::asio::const_buffer &data,
                         const boost::asio::ip::udp::endpoint &endpoint);

  // The multiplexer used to send and receive UDP packets.
  RudpMultiplexer &multiplexer_;

  // This class allows only one outstanding asynchronous accept operation at a
  // time. The following data members store the pending accept, and the socket
  // object that is waiting to be accepted.
  boost::asio::deadline_timer waiting_accept_;
  RudpSocket *waiting_accept_socket_;

  // A connection request that is yet to be processed by the acceptor.
  struct PendingRequest {
    PendingRequest()
        : remote_id(0),
          remote_endpoint() {}
    boost::uint32_t remote_id;
    boost::asio::ip::udp::endpoint remote_endpoint;
  };

  // A queue of the connections that are pending accept processing.
  typedef std::deque<PendingRequest> PendingRequestQueue;
  PendingRequestQueue pending_requests_;
};

}  // namespace transport

}  // namespace maidsafe

#endif  // MAIDSAFE_RUDP_CORE_ACCEPTOR_H_
