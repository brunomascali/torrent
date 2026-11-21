#include <download/peer/peer_connection.hxx>
#include <download/peer/wire/stream.hxx>
#include <logger/logger.hxx>

#include <algorithm>
#include <utility>
#include <variant>

namespace download::peer {

namespace {
constexpr std::size_t max_pieces_in_flight = 64;
}

PeerConnection::PeerConnection(Peer peer, Identity identity,
                               PiecePicker &picker, PieceSink &sink)
    : m_peer(std::move(peer)), m_identity(identity), m_picker(picker),
      m_sink(sink), m_peer_has(picker.layout().num_pieces(), false) {}

boost::asio::awaitable<void> PeerConnection::run() {
  struct CleanupOnExit {
    PeerConnection &self;
    ~CleanupOnExit() {
      self.release_pending_pieces();
      self.m_tcp_connection.reset();
    }
  } cleanup_on_exit{*this};

  auto connection = co_await io::TcpConnection::connect(m_peer.ip, m_peer.port);
  if (!connection) {
    logger::warn("Connect failed: {}", connection.error().str());
    co_return;
  }
  m_tcp_connection.emplace(std::move(*connection));

  if (auto hs = co_await handshake(); !hs) {
    logger::warn("Handshake with {}:{} failed: {}", m_peer.ip, m_peer.port, hs.error());
    co_return;
  }

  logger::debug("Successful handshake with peer {}:{}", m_peer.ip, m_peer.port);
  co_await main_loop();
}

boost::asio::awaitable<void> PeerConnection::main_loop() {
  const std::size_t num_pieces = m_picker.layout().num_pieces();
  while (true) {
    auto msg = co_await wire::read_message(*m_tcp_connection, num_pieces);
    if (!msg) {
      logger::warn("Error receiving message: {}", msg.error().str());
      co_return;
    }

    co_await std::visit(
        [this](const auto &m) -> boost::asio::awaitable<void> {
          co_return co_await handle_msg(m);
        },
        *msg);
  }
}

boost::asio::awaitable<std::expected<void, std::string>> PeerConnection::handshake() {
  wire::Handshake ours;
  ours.info_hash = m_identity.info_hash;
  ours.peer_id = m_identity.peer_id;

  if (auto sent = co_await wire::write_handshake(*m_tcp_connection, ours); !sent) {
    co_return std::unexpected(sent.error().str());
  }

  auto theirs = co_await wire::read_handshake(*m_tcp_connection);
  if (!theirs) {
    co_return std::unexpected(theirs.error().str());
  }
  if (theirs->info_hash != ours.info_hash) {
    co_return std::unexpected("peer answered with a different info_hash");
  }
  co_return std::expected<void, std::string>{};
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::KeepAlive &) {
  co_return;
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Choke &) {
  m_choked = true;
  release_pending_pieces();
  co_return;
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Unchoke &) {
  logger::debug("Received unchoke");
  m_choked = false;
  co_await fill_requests();
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Interested &) {
  co_return;
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::NotInterested &) {
  co_return;
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Have &have) {
  m_peer_has[have.index] = true;
  co_await update_interest();
  co_await fill_requests();
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Bitfield &bitfield) {
  logger::debug("Received bitfield of size {}", bitfield.mask.size());
  m_peer_has = bitfield.mask;
  co_await update_interest();
  co_await fill_requests();
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Request &) {
  co_return;
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Piece &piece) {
  auto it = m_incoming_pieces.find(piece.index);
  if (it == m_incoming_pieces.end()) {
    logger::warn("Block for untracked piece {}", piece.index);
    co_return;
  }

  if (!it->second.add_chunk(piece.begin, piece.block)) {
    co_return;
  }

  if (it->second.all_chunks_received()) {
    logger::trace("Piece {} completed", piece.index);
    const std::uint32_t index = piece.index;
    auto data = std::move(it->second.data);
    m_incoming_pieces.erase(it);

    m_sink.on_piece(index, data);
    co_await fill_requests();
  }
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Cancel &) {
  co_return;
}

boost::asio::awaitable<void> PeerConnection::update_interest() {
  if (!m_interested && m_picker.is_interesting(m_peer_has)) {
    m_interested = true;
    co_await send_message(wire::Interested{});
  }
}

boost::asio::awaitable<void> PeerConnection::fill_requests() {
  if (m_choked) {
    co_return;
  }

  while (m_incoming_pieces.size() < max_pieces_in_flight) {
    auto piece = m_picker.reserve(m_peer_has);
    if (!piece) {
      break;
    }

    const std::uint32_t size = m_picker.layout().piece_size(*piece);
    m_incoming_pieces.emplace(*piece, IncomingPiece(*piece, size));

    for (std::uint32_t offset = 0; offset < size; offset += PIECE_CHUNK_SIZE) {
      const std::uint32_t length = std::min(PIECE_CHUNK_SIZE, size - offset);
      if (!co_await send_message(wire::Request{*piece, offset, length})) {
        co_return;
      }
    }
  }
}

void PeerConnection::release_pending_pieces() {
  for (const auto &[index, _] : m_incoming_pieces) {
    m_picker.release(index);
  }
  m_incoming_pieces.clear();
}

boost::asio::awaitable<bool> PeerConnection::send_message(const wire::PeerMessage &message) {
  logger::trace("Sending message: {}", message.index());
  auto sent = co_await wire::write_message(*m_tcp_connection, message);
  if (!sent) {
    logger::warn("Error sending message: {}", sent.error().str());
  }
  co_return sent.has_value();
}

}
