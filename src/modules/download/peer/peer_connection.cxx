#include <download/peer/peer_connection.hxx>
#include <download/peer/wire/stream.hxx>
#include <logger/logger.hxx>

#include <algorithm>
#include <utility>
#include <variant>

namespace download::peer {

namespace {

constexpr std::size_t max_outstanding_requests = 128;
constexpr std::uint32_t max_request_length = 16 * 1024;

Peer peer_from(const io::TcpConnection &connection) {
  const auto endpoint = connection.remote_endpoint();
  return Peer{.peer_id = std::nullopt,
              .ip = endpoint.address().to_string(),
              .port = endpoint.port()};
}

}

PeerConnection::PeerConnection(Peer peer, Identity identity, PiecePicker &picker,
                               Swarm &swarm)
    : m_peer(std::move(peer)), m_identity(identity), m_picker(picker), m_swarm(swarm),
      m_incoming(false), m_peer_has(picker.layout().num_pieces(), false) {}

PeerConnection::PeerConnection(io::TcpConnection incoming, Identity identity,
                               PiecePicker &picker, Swarm &swarm)
    : m_peer(peer_from(incoming)), m_identity(identity), m_picker(picker), m_swarm(swarm),
      m_incoming(true), m_peer_has(picker.layout().num_pieces(), false),
      m_tcp_connection(std::move(incoming)) {}

boost::asio::awaitable<void> PeerConnection::run() {
  struct CleanupOnExit {
    PeerConnection &self;
    ~CleanupOnExit() { self.cleanup(); }
  } cleanup_on_exit{*this};

  if (!m_incoming) {
    auto connection = co_await io::TcpConnection::connect(m_peer.ip, m_peer.port);
    if (!connection) {
      logger::warn("Connect failed: {}", connection.error().str());
      co_return;
    }
    m_tcp_connection.emplace(std::move(*connection));
  }

  if (auto hs = co_await handshake(); !hs) {
    logger::warn("Handshake with {}:{} failed: {}", m_peer.ip, m_peer.port, hs.error());
    co_return;
  }

  logger::debug("Successful {} handshake with peer {}:{}",
                m_incoming ? "incoming" : "outgoing", m_peer.ip, m_peer.port);

  if (!co_await send_bitfield()) {
    co_return;
  }
  co_await main_loop();
}

void PeerConnection::cleanup() {
  release_pending_pieces();
  if (m_holds_upload_slot) {
    m_swarm.release_upload_slot();
    m_holds_upload_slot = false;
  }
  if (m_admitted) {
    m_swarm.leave(*m_admitted);
    m_admitted.reset();
  }
  m_tcp_connection.reset();
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

    if (!co_await send_new_haves()) {
      co_return;
    }
  }
}

boost::asio::awaitable<std::expected<void, std::string>> PeerConnection::handshake() {
  wire::Handshake ours;
  ours.info_hash = m_identity.info_hash;
  ours.peer_id = m_identity.peer_id;

  if (!m_incoming) {
    if (auto sent = co_await wire::write_handshake(*m_tcp_connection, ours); !sent) {
      co_return std::unexpected(sent.error().str());
    }
  }

  auto theirs = co_await wire::read_handshake(*m_tcp_connection);
  if (!theirs) {
    co_return std::unexpected(theirs.error().str());
  }
  if (theirs->info_hash != ours.info_hash) {
    co_return std::unexpected("peer is on a different torrent");
  }
  if (!m_swarm.admit(theirs->peer_id)) {
    co_return std::unexpected("refused: ourselves, already connected, or too many peers");
  }
  m_admitted = theirs->peer_id;

  if (m_incoming) {
    if (auto sent = co_await wire::write_handshake(*m_tcp_connection, ours); !sent) {
      co_return std::unexpected(sent.error().str());
    }
  }
  co_return std::expected<void, std::string>{};
}

boost::asio::awaitable<bool> PeerConnection::send_bitfield() {
  auto mask = m_picker.have_snapshot(m_have_cursor);
  if (std::ranges::none_of(mask, [](bool b) { return b; })) {
    co_return true;
  }
  co_return co_await send_message(wire::Bitfield{std::move(mask)});
}

boost::asio::awaitable<bool> PeerConnection::send_new_haves() {
  for (const auto index : m_picker.completed_since(m_have_cursor)) {
    if (m_peer_has[index]) {
      continue;
    }
    if (!co_await send_message(wire::Have{index})) {
      co_return false;
    }
  }
  co_return true;
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
  m_peer_interested = true;
  if (m_choking_peer && m_swarm.acquire_upload_slot()) {
    m_holds_upload_slot = true;
    m_choking_peer = false;
    co_await send_message(wire::Unchoke{});
  }
}

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::NotInterested &) {
  m_peer_interested = false;
  if (m_holds_upload_slot) {
    m_swarm.release_upload_slot();
    m_holds_upload_slot = false;
    m_choking_peer = true;
    co_await send_message(wire::Choke{});
  }
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

boost::asio::awaitable<void> PeerConnection::handle_msg(const wire::Request &request) {
  if (m_choking_peer) {
    co_return;
  }
  if (request.length == 0 || request.length > max_request_length) {
    logger::debug("Ignoring {}-byte request from {}:{}", request.length, m_peer.ip,
                  m_peer.port);
    co_return;
  }

  auto block = m_swarm.read_block(request.index, request.begin, request.length);
  if (!block) {
    co_return;
  }
  co_await send_message(wire::Piece{request.index, request.begin, std::move(*block)});
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
  m_swarm.on_block_received(piece.block.size());
  if (m_outstanding_requests > 0) {
    --m_outstanding_requests;
  }

  if (it->second.all_chunks_received()) {
    logger::trace("Piece {} completed", piece.index);
    const std::uint32_t index = piece.index;
    auto data = std::move(it->second.data);
    m_incoming_pieces.erase(it);
    m_swarm.on_piece(index, data);
  }

  co_await fill_requests();
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

  while (m_outstanding_requests < max_outstanding_requests) {
    IncomingPiece *target = nullptr;
    for (auto &[index, piece] : m_incoming_pieces) {
      if (!piece.fully_requested()) {
        target = &piece;
        break;
      }
    }

    if (!target) {
      auto index = m_picker.reserve(m_peer_has);
      if (!index) {
        break;
      }
      const std::uint32_t size = m_picker.layout().piece_size(*index);
      target = &m_incoming_pieces.emplace(*index, IncomingPiece(*index, size)).first->second;
    }

    const std::uint32_t offset = target->next_request_offset;
    const std::uint32_t length = std::min(PIECE_CHUNK_SIZE, target->piece_length - offset);
    target->next_request_offset += length;
    ++m_outstanding_requests;

    if (!co_await send_message(wire::Request{target->index, offset, length})) {
      co_return;
    }
  }
}

void PeerConnection::release_pending_pieces() {
  for (const auto &[index, _] : m_incoming_pieces) {
    m_picker.release(index);
  }
  m_incoming_pieces.clear();
  m_outstanding_requests = 0;
}

boost::asio::awaitable<bool> PeerConnection::send_message(const wire::PeerMessage &message) {
  auto sent = co_await wire::write_message(*m_tcp_connection, message);
  if (!sent) {
    logger::warn("Error sending message: {}", sent.error().str());
  }
  co_return sent.has_value();
}

}
