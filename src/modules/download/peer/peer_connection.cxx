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
#include <sys/socket.h>
#include <utility>
#include <variant>
#include <vector>

namespace download::peer {
PeerConnection::PeerConnection(const Peer &peer,
                               DownloadManager &download_manager)
    : m_download_manager_ref(download_manager), m_peer(peer), m_peer_id(make_peer_id()) {
}

asio::awaitable<void> PeerConnection::prepare_connection() {
  m_tcp_connection = std::make_unique<io::TcpConnection>(
      m_download_manager_ref.io_context(), m_peer.ip, m_peer.port);

  auto connect_result = co_await m_tcp_connection->async_connect();
  if (!connect_result) {
    logger::warn("Connect failed: {}", connect_result.error());
    co_return;
  }

  auto hs_result = co_await handshake(m_download_manager_ref.info_hash());

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
  m_choked = false;

  if (m_bitfield.size() == 0)
    co_return;

  logger::debug("Received unchoke");
  co_await fill_requests();

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
  logger::debug("Received bitfield of size {}", bitfield.mask.size());
  std::size_t num_pieces = m_download_manager_ref.metainfo().num_pieces();

  for (std::size_t i = num_pieces; i < bitfield.mask.size(); ++i) {
    if (bitfield.mask[i]) {
      logger::warn("Peer set a padding bit in its bitfield");
      break;
    }
  }

  logger::debug("num pieces: {}", num_pieces);
  m_bitfield = bitfield.mask;
  m_bitfield.resize(num_pieces);

  co_await send_message(Interested{});
}

asio::awaitable<void> PeerConnection::handle_msg(const Request &) { co_return; }

asio::awaitable<void> PeerConnection::handle_msg(const Piece &piece) {
  auto it = m_incoming_pieces.find(piece.index);
  if (it == m_incoming_pieces.end()) {
    logger::warn("Block for untracked piece {}", piece.index);
    co_return;
  }

  if (!it->second.add_chunk(piece.begin, piece.block)) {
    // Maybe mark the piece as available again?
    co_return;
  }

  logger::trace("Progress on {} {}", piece.index, piece.block.size());

  if (it->second.all_chunks_received()) {
    logger::trace("All blocks from piece {} received", piece.index);
    uint32_t index = piece.index;
    auto data = std::move(it->second.data);
    m_incoming_pieces.erase(it);

    if (m_download_manager_ref.verify_piece_hash(index, data)) {
      logger::debug("Piece {} validated. Can write to disk", index);
      m_download_manager_ref.write_to_disk(index, data);
    } else {
      logger::debug("Piece {} hash != expected", index);
    }

    co_await fill_requests();
  }

  co_return;
}

asio::awaitable<void> PeerConnection::handle_msg(const Cancel &) { co_return; }

asio::awaitable<std::expected<void, std::string>>
PeerConnection::handshake(std::span<const std::byte, 20> info_hash) {
  auto handshake_request = HandshakeMessage::as_bytes(info_hash, m_peer_id);
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

asio::awaitable<void> PeerConnection::fill_requests() {
  while (m_incoming_pieces.size() < 32) {
    auto piece = m_download_manager_ref.reserve_piece(m_bitfield);

    if (piece) {
      m_incoming_pieces.emplace(
        std::make_pair(*piece, IncomingPiece(*piece, 262144))
      );
      logger::debug("Requesting piece {}", *piece);
      for (int offset = 0; offset < m_download_manager_ref.metainfo().piece_length(); offset += 16 * 1024) {
        PeerMessage request = Request(*piece, offset, 16 * 1024);
        co_await m_tcp_connection->async_write(PeerMessage::to_bytes(request));
      }
    }
  }
  co_return;
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

  co_return PeerMessage::from_bytes(*id, *payload, m_download_manager_ref.metainfo().num_pieces());
}

asio::awaitable<bool> PeerConnection::send_message(const PeerMessage &message) {
  logger::debug("Sending message: {}", message.index());
  auto sent = co_await m_tcp_connection->async_write(PeerMessage::to_bytes(message));

  co_return sent.has_value();
}

std::array<std::byte, 20> make_peer_id() {
  constexpr std::string_view prefix = "-XX0001-";
  constexpr std::string_view alphabet =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::array<std::byte, 20> id{};
  auto it = std::ranges::transform(prefix, id.begin(), [](char c) {
              return static_cast<std::byte>(c);
            }).out;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);
  for (; it != id.end(); ++it)
    *it = static_cast<std::byte>(alphabet[dist(gen)]);

  return id;
}

} // namespace download::peer
