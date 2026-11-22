#include <download/download_session.hxx>

#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <crypto/sha1.hxx>
#include <download/listener.hxx>
#include <download/peer_id.hxx>
#include <logger/logger.hxx>
#include <thread>

namespace download {

namespace {

constexpr std::uint16_t first_listen_port = 6881;
constexpr std::uint16_t last_listen_port = 6889;
constexpr std::size_t max_connections = 200;
constexpr int max_upload_slots = 8;
constexpr std::uint32_t max_block_length = 16 * 1024;

boost::asio::awaitable<void> serve(std::shared_ptr<peer::PeerConnection> connection) {
  co_await connection->run();
}

}

std::expected<std::unique_ptr<DownloadSession>, std::string>
DownloadSession::create(std::unique_ptr<const metainfo::Metainfo> metainfo) {
  if (!metainfo) {
    return std::unexpected("no metainfo");
  }
  if (metainfo->announce_urls().empty()) {
    return std::unexpected("torrent has no tracker");
  }
  auto layout = PieceLayout::make(metainfo->length(), metainfo->piece_length(),
                                  metainfo->num_pieces());
  if (!layout) {
    return std::unexpected("invalid piece layout: " + layout.error());
  }
  return std::unique_ptr<DownloadSession>(
      new DownloadSession(std::move(metainfo), *layout));
}

DownloadSession::DownloadSession(std::unique_ptr<const metainfo::Metainfo> metainfo,
                                 PieceLayout layout)
    : m_metainfo(std::move(metainfo)), m_picker(layout),
      m_tracker(m_metainfo->announce_urls().front()),
      m_identity{.info_hash = m_metainfo->info_hash(), .peer_id = make_peer_id()} {
  logger::debug("Total pieces: {}", layout.num_pieces());
}

DownloadSession::~DownloadSession() = default;

bool DownloadSession::run() {
  logger::info("Starting download...");

  if (!open_storage()) {
    return false;
  }

  auto listener = Listener::open(m_io_context.get_executor(), first_listen_port,
                                 last_listen_port);
  if (listener) {
    m_listen_port = listener->port();
    logger::info("Listening for peers on port {}{}", m_listen_port,
                 listener->accepts_ipv6() ? " (IPv4 and IPv6)" : " (IPv4)");
  } else {
    logger::warn("Not accepting incoming peers: {}", listener.error());
  }

  auto peers = announce(tracker::Event::started);
  if (peers.empty()) {
    logger::error("No peers to download from");
    return false;
  }

  peer::Swarm &swarm = *this;
  if (listener) {
    Listener::start(std::move(*listener), [this](io::TcpConnection connection) {
      peer::Swarm &self = *this;
      spawn(std::make_shared<peer::PeerConnection>(std::move(connection), m_identity,
                                                   m_picker, self));
    });
  }
  for (auto &peer : peers) {
    spawn(std::make_shared<peer::PeerConnection>(std::move(peer), m_identity, m_picker,
                                                 swarm));
  }

  std::vector<std::thread> pool;
  const unsigned n = std::max(1u, std::thread::hardware_concurrency());
  for (unsigned i = 0; i < n; ++i) {
    pool.emplace_back([this] { m_io_context.run(); });
  }
  for (auto &t : pool) {
    t.join();
  }

  if (m_picker.is_complete()) {
    logger::info("Download complete");
    announce(tracker::Event::completed);
  } else {
    logger::warn("All peers gone with {}/{} pieces", m_picker.completed_count(),
                 m_picker.layout().num_pieces());
  }
  return true;
}

bool DownloadSession::on_piece(std::uint32_t index, std::span<const std::byte> data) {
  if (!hash_matches(index, data)) {
    logger::warn("Piece {} failed hash check", index);
    m_picker.release(index);
    return false;
  }

  auto written = m_storage->write(m_picker.layout().piece_offset(index), data);
  if (!written) {
    logger::warn("Error writing piece {}: {}", index, written.error().str());
    m_picker.release(index);
    return false;
  }

  m_picker.complete(index);
  m_downloaded += data.size();
  logger::info("Progress: {}/{}", m_picker.completed_count(),
               m_picker.layout().num_pieces());

  if (m_picker.is_complete()) {
    m_io_context.stop();
  }
  return true;
}

void DownloadSession::spawn(std::shared_ptr<peer::PeerConnection> connection) {
  boost::asio::co_spawn(m_io_context, serve(std::move(connection)), [](std::exception_ptr e) {
    if (!e) {
      return;
    }
    try {
      std::rethrow_exception(e);
    } catch (const std::exception &ex) {
      logger::warn("Peer coroutine terminated: {}", ex.what());
    }
  });
}

std::optional<std::vector<std::byte>>
DownloadSession::read_block(std::uint32_t index, std::uint32_t begin, std::uint32_t length) {
  if (length == 0 || length > max_block_length || !m_picker.has(index)) {
    return std::nullopt;
  }
  const std::uint32_t size = m_picker.layout().piece_size(index);
  if (begin > size || length > size - begin) {
    return std::nullopt;
  }

  std::vector<std::byte> block(length);
  auto read = m_storage->read(m_picker.layout().piece_offset(index) + begin, block);
  if (!read) {
    logger::warn("Error reading piece {} for upload: {}", index, read.error().str());
    return std::nullopt;
  }
  m_uploaded += length;
  return block;
}

bool DownloadSession::admit(const peer::PeerId &peer_id) {
  if (peer_id == m_identity.peer_id) {
    return false;
  }
  std::lock_guard lock(m_peers_mutex);
  if (m_connected.size() >= max_connections) {
    return false;
  }
  return m_connected.insert(peer_id).second;
}

void DownloadSession::leave(const peer::PeerId &peer_id) {
  std::lock_guard lock(m_peers_mutex);
  m_connected.erase(peer_id);
}

bool DownloadSession::acquire_upload_slot() {
  int used = m_upload_slots_used.load();
  while (used < max_upload_slots) {
    if (m_upload_slots_used.compare_exchange_weak(used, used + 1)) {
      return true;
    }
  }
  return false;
}

void DownloadSession::release_upload_slot() { --m_upload_slots_used; }

bool DownloadSession::hash_matches(std::uint32_t index,
                                   std::span<const std::byte> data) const {
  auto expected = m_metainfo->piece_hash(index);
  auto actual = crypto::sha1(data);
  return std::ranges::equal(expected, actual);
}

bool DownloadSession::open_storage() {
  std::vector<StorageFile> files;
  for (const auto &file : m_metainfo->files()) {
    files.push_back(StorageFile{file.path, file.length});
  }

  auto storage = Storage::open(".", std::move(files));
  if (!storage) {
    logger::error("Error opening storage: {}", storage.error().str());
    return false;
  }
  if (storage->total_length() != m_picker.layout().total_length()) {
    logger::error("Files hold {} bytes but the torrent is {} bytes",
                  storage->total_length(), m_picker.layout().total_length());
    return false;
  }

  m_storage.emplace(std::move(*storage));
  return true;
}

std::vector<peer::Peer> DownloadSession::announce(tracker::Event event) {
  tracker::AnnounceRequest req{
      .info_hash = m_identity.info_hash,
      .peer_id = m_identity.peer_id,
      .port = m_listen_port,
      .uploaded = m_uploaded.load(),
      .downloaded = m_downloaded.load(),
      .left = m_picker.bytes_left(),
      .event = event,
  };

  auto response = m_tracker.announce(req);
  if (!response) {
    logger::warn("Announce to {} failed: {}", m_tracker.url(), response.error());
    return {};
  }
  if (const auto *failure = response->as_failure()) {
    logger::warn("Tracker refused announce: {}", failure->failure_reason);
    return {};
  }

  const auto &success = *response->as_success();
  if (success.warning_message) {
    logger::warn("Tracker warning: {}", *success.warning_message);
  }
  logger::info("Found {} peers", success.peers.size());
  return success.peers;
}

}
