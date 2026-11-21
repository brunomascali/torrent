#pragma once

#include <boost/asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <download/peer/wire/error.hxx>
#include <download/peer/wire/handshake.hxx>
#include <download/peer/wire/message.hxx>
#include <expected>
#include <io/tcp_connection.hxx>

namespace download::peer::wire {

inline constexpr std::uint32_t max_message_length = 1u << 20;

boost::asio::awaitable<std::expected<void, Error>>
write_handshake(io::TcpConnection &conn, const Handshake &handshake);

boost::asio::awaitable<std::expected<Handshake, Error>>
read_handshake(io::TcpConnection &conn);

boost::asio::awaitable<std::expected<void, Error>>
write_message(io::TcpConnection &conn, const PeerMessage &message);

boost::asio::awaitable<std::expected<PeerMessage, Error>>
read_message(io::TcpConnection &conn, std::size_t num_pieces);

}
