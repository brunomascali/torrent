#include "boost/asio/awaitable.hpp"
#include "boost/asio/io_context.hpp"
#include "download/download_manager.hxx"
#include "download/peer/message.hxx"
#include "logger/logger.hxx"
#include <algorithm>
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <download/peer/handshake.hxx>
#include <download/peer/peer_connection.hxx>
#include <expected>
#include <iterator>
#include <stdexcept>
#include <variant>
#include <vector>

namespace download::peer {
PeerConnection::PeerConnection(const Peer &peer,
                               DownloadManager &download_manager)
    : m_download_manager_ref(download_manager), m_peer(peer) {
  m_info_hash = m_download_manager_ref.metainfo().info_hash().value();
}

asio::awaitable<void> PeerConnection::prepare_connection() {
  m_tcp_connection = std::make_unique<io::TcpConnection>(
      m_download_manager_ref.io_context(), m_peer.ip, m_peer.port);

  auto connect_result = co_await m_tcp_connection->async_connect();
  if (!connect_result) {
    logger::warn("Connect failed: {}", connect_result.error());
    co_return;
  }

  auto hs_result = co_await handshake(m_info_hash);

  if (!hs_result) {
    logger::warn("Handshake failed: {}", hs_result.error());
    co_return;
  }

  logger::debug("Successful handshake with peer {}:{}", m_peer.ip, m_peer.port);
  co_await main_loop();
}

asio::awaitable<void> PeerConnection::main_loop() {
  while (true) {
    auto msg = co_await receive_message();

    if (!msg) {
      logger::warn("Error receiving message: {}", msg.error());
      co_return;
    }

    co_await std::visit(
        [this](const auto &m) -> asio::awaitable<void> {
          co_return co_await handle_msg(m);
        },
        *msg);
  }
}

asio::awaitable<void> PeerConnection::handle_msg(const KeepAlive &) {
  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Choke &) { co_return; }

asio::awaitable<void> PeerConnection::handle_msg(const Unchoke &) {
  m_choked = true;

  logger::debug("Received unchoke");

  auto piece = m_download_manager_ref.reserve_piece(m_bitfield);

  if (piece) {
    logger::debug("Requesting piece {}", *piece);
    for (int offset = 0; offset < m_download_manager_ref.metainfo().piece_length().value(); offset += 16 * 1024) {
      PeerMessage request = Request(*piece, offset, 16 * 1024);
      co_await m_tcp_connection->async_write(request.to_bytes());
    }
  }

  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Interested &) {
  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const NotInterested &) {
  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Have &) { co_return; }

asio::awaitable<void> PeerConnection::handle_msg(const Bitfield &bitfield) {
  logger::trace("Received bitfield of size {}", bitfield.mask.size());

  int64_t pieces_encoded_in_bitfield =
      static_cast<int64_t>(bitfield.mask.size()) * 8;
  int64_t num_pieces = m_download_manager_ref.metainfo().num_pieces().value();

  if (std::abs(pieces_encoded_in_bitfield - num_pieces) >= 8) {
    logger::error("Mismatch between pieces encoded in bitfield ({}) and pieces "
                  "in metainfo ({})",
                  pieces_encoded_in_bitfield, num_pieces);
    throw std::logic_error("invalid bitfield");
  }

  m_bitfield = std::move(bitfield.mask);

  auto send = co_await send_message(Interested{});
  if (!send) {
    logger::warn("Error sending Interested message");
  }

  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Request &) { co_return; }

asio::awaitable<void> PeerConnection::handle_msg(const Piece &piece) {
  logger::debug("Piece {} {} received", piece.index, piece.begin);

  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Cancel &) { co_return; }

asio::awaitable<std::expected<void, std::string>>
PeerConnection::handshake(const std::string &info_hash) {
  auto handshake_request = HandshakeMessage::as_bytes(info_hash);
  auto handshake_response =
      co_await m_tcp_connection->async_write(handshake_request);

  if (!handshake_response.has_value()) {
    co_return std::unexpected("Failed writing to peer socket");
  }

  auto response = co_await m_tcp_connection->async_read_exact(68);

  if (!response) {
    co_return std::unexpected("Failed to read from tcp connection");
  }

  if (response->size() != 68 ||
      !std::equal(handshake_request.begin() + 28,
                  handshake_request.begin() + 48, response->begin() + 28,
                  response->begin() + 48)) {
    co_return std::unexpected("request != response (handshake)");
  }

  co_return std::expected<void, std::string>{};
}

std::vector<std::byte>
PeerConnection::create_handshake_message(const std::string &info_hash) {
  if (info_hash.size() != 20) {
    throw std::invalid_argument("info_hash must be 20 bytes, got " +
                                std::to_string(info_hash.size()));
  }

  constexpr std::string_view pstr = "BitTorrent protocol";

  std::vector<std::byte> handshake_msg;
  handshake_msg.reserve(49 + pstr.size());

  // pstrlen
  handshake_msg.push_back(std::byte{static_cast<unsigned char>(pstr.size())});

  // pstr
  std::transform(
      pstr.begin(), pstr.end(), std::back_inserter(handshake_msg),
      [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

  // reserved (8 bytes, all zero)
  handshake_msg.insert(handshake_msg.end(), 8, std::byte{0});

  // info_hash (20 bytes)
  std::transform(
      info_hash.begin(), info_hash.end(), std::back_inserter(handshake_msg),
      [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

  // peer_id (20 bytes)
  std::string peer_id = "12312412312315123512";
  std::transform(
      peer_id.begin(), peer_id.end(), std::back_inserter(handshake_msg),
      [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

  return handshake_msg;
}

asio::awaitable<std::expected<PeerMessage, std::string>>
PeerConnection::receive_message() {
  auto length = co_await m_tcp_connection->async_read_u32();
  if (!length)
    co_return std::unexpected(length.error());

  if (*length == 0)
    co_return KeepAlive{};

  auto id = co_await m_tcp_connection->async_read_byte();
  if (!id)
    co_return std::unexpected(id.error());

  auto payload = co_await m_tcp_connection->async_read_exact(*length - 1);
  if (!payload)
    co_return std::unexpected(payload.error());

  co_return PeerMessage::from_bytes(*id, *payload);
}

asio::awaitable<bool> PeerConnection::send_message(const PeerMessage &message) {
  logger::debug("Sending message: {}", message.index());
  auto sent = co_await m_tcp_connection->async_write(message.to_bytes());

  co_return sent.has_value();
}

} // namespace download::peer
