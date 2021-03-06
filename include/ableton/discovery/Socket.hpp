/* Copyright 2016, Ableton AG, Berlin. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  If you would like to incorporate Link into a proprietary software application,
 *  please contact <link-devs@ableton.com>.
 */

#pragma once

#include <ableton/platforms/asio/AsioService.hpp>
#include <ableton/platforms/asio/AsioWrapper.hpp>
#include <ableton/util/SafeAsyncHandler.hpp>
#include <array>
#include <cassert>

namespace ableton
{
namespace discovery
{

template <std::size_t MaxPacketSize>
struct Socket
{
  Socket(platforms::asio::AsioService& io)
    : mpImpl(std::make_shared<Impl>(io))
  {
  }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& rhs)
    : mpImpl(std::move(rhs.mpImpl))
  {
  }

  std::size_t send(
    const uint8_t* const pData, const size_t numBytes, const asio::ip::udp::endpoint& to)
  {
    assert(numBytes < MaxPacketSize);
    return mpImpl->mSocket.send_to(asio::buffer(pData, numBytes), to);
  }

  template <typename Handler>
  void receive(Handler handler)
  {
    mpImpl->mHandler = std::move(handler);
    mpImpl->mSocket.async_receive_from(
      asio::buffer(mpImpl->mReceiveBuffer, MaxPacketSize), mpImpl->mSenderEndpoint,
      util::makeAsyncSafe(mpImpl));
  }

  asio::ip::udp::endpoint endpoint() const
  {
    return mpImpl->mSocket.local_endpoint();
  }

  struct Impl
  {
    Impl(platforms::asio::AsioService& io)
      : mSocket(io.mService, asio::ip::udp::v4())
    {
    }

    ~Impl()
    {
      // Ignore error codes in shutdown and close as the socket may
      // have already been forcibly closed
      asio::error_code ec;
      mSocket.shutdown(asio::ip::udp::socket::shutdown_both, ec);
      mSocket.close(ec);
    }

    void operator()(const asio::error_code& error, const std::size_t numBytes)
    {
      if (!error && numBytes > 0 && numBytes <= MaxPacketSize)
      {
        const auto bufBegin = begin(mReceiveBuffer);
        mHandler(mSenderEndpoint, bufBegin, bufBegin + static_cast<ptrdiff_t>(numBytes));
      }
    }

    asio::ip::udp::socket mSocket;
    asio::ip::udp::endpoint mSenderEndpoint;
    using Buffer = std::array<uint8_t, MaxPacketSize>;
    Buffer mReceiveBuffer;
    using ByteIt = typename Buffer::const_iterator;
    std::function<void(const asio::ip::udp::endpoint&, ByteIt, ByteIt)> mHandler;
  };

  std::shared_ptr<Impl> mpImpl;
};

// Configure an asio socket for receiving multicast messages
template <std::size_t MaxPacketSize>
void configureMulticastSocket(Socket<MaxPacketSize>& socket,
  const asio::ip::address_v4& addr,
  const asio::ip::udp::endpoint& multicastEndpoint)
{
  socket.mpImpl->mSocket.set_option(asio::ip::udp::socket::reuse_address(true));
  // ???
  socket.mpImpl->mSocket.set_option(asio::socket_base::broadcast(!addr.is_loopback()));
  // ???
  socket.mpImpl->mSocket.set_option(
    asio::ip::multicast::enable_loopback(addr.is_loopback()));
  socket.mpImpl->mSocket.set_option(asio::ip::multicast::outbound_interface(addr));
  // Is from_string("0.0.0.0") best approach?
  socket.mpImpl->mSocket.bind(
    {asio::ip::address::from_string("0.0.0.0"), multicastEndpoint.port()});
  socket.mpImpl->mSocket.set_option(
    asio::ip::multicast::join_group(multicastEndpoint.address().to_v4(), addr));
}

// Configure an asio socket for receiving unicast messages
template <std::size_t MaxPacketSize>
void configureUnicastSocket(
  Socket<MaxPacketSize>& socket, const asio::ip::address_v4& addr)
{
  // ??? really necessary?
  socket.mpImpl->mSocket.set_option(
    asio::ip::multicast::enable_loopback(addr.is_loopback()));
  socket.mpImpl->mSocket.set_option(asio::ip::multicast::outbound_interface(addr));
  socket.mpImpl->mSocket.bind(asio::ip::udp::endpoint{addr, 0});
}

} // namespace discovery
} // namespace ableton
