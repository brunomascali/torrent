#include "download/peer/peer.hxx"
#include "download/tracker.hxx"
#include "logger/logger.hxx"
#include <download/download_manager.hxx>
#include <download/http_announce.hxx>
#include <memory>
#include <metainfo/metainfo.hxx>

namespace download {

DownloadManager::DownloadManager(
    std::unique_ptr<const metainfo::Metainfo> metainfo)
    : m_metainfo(std::move(metainfo)) {
  m_left = m_metainfo->total_size().value();
  m_tracker = Tracker(m_metainfo->announce().value());
  m_pieces_downloaded = std::vector<bool>(m_metainfo->num_pieces().value(), false);
  m_pieces_in_progress = std::vector<bool>(m_metainfo->num_pieces().value(), false);
}

void DownloadManager::start_download() {
  logger::info("Starting download...");

  for (const auto &peer : fetch_peers()) {
    auto conn = std::make_unique<peer::PeerConnection>(peer, *this);
    auto *raw = conn.get();
    m_peer_connections.push_back(std::move(conn));

    asio::co_spawn(
        m_io_context, raw->prepare_connection(), [](std::exception_ptr e) {
          if (e) {
            try {
              std::rethrow_exception(e);
            } catch (const std::exception &ex) {
              logger::warn("Peer coroutine terminated: {}", ex.what());
            }
          }
        });
  }

  std::vector<std::thread> pool;
  unsigned n = std::max(1u, std::thread::hardware_concurrency());
  for (unsigned i = 0; i < n; ++i) {
    pool.emplace_back([this] { m_io_context.run(); });
  }
  for (auto &t : pool)
    t.join();
}

std::optional<uint32_t> DownloadManager::reserve_piece(const std::vector<std::byte>& peer_bitfield) {
    std::lock_guard lock(m_piece_mutex);

    for (uint32_t i = 0; i < m_pieces_downloaded.size(); ++i) {
        if (m_pieces_downloaded[i] || m_pieces_in_progress[i]) {
            continue;
        }
        if (!peer_has_piece(peer_bitfield, i)) {
            continue;
        }

        m_pieces_in_progress[i] = true;
        return i;
    }

    return std::nullopt;
}

void DownloadManager::release_piece(uint32_t piece_index) {
    std::lock_guard lock(m_piece_mutex);
    m_pieces_in_progress[piece_index] = false;
}

void DownloadManager::mark_piece_complete(uint32_t piece_index) {
    std::lock_guard lock(m_piece_mutex);
    m_pieces_downloaded[piece_index] = true;
    m_pieces_in_progress[piece_index] = false;
}

std::vector<peer::Peer> DownloadManager::fetch_peers() {
  auto req = HttpAnnounceRequest::build_request(*this);

  std::expected<HttpAnnounceResponse, std::string> response =
      m_tracker.announce(req);
  if (!response) {
    logger::warn("Failed to get response from tracker");
  }
  if (response && response->is_failure()) {
    logger::warn("Announce failed: ", response->as_failure()->failure_reason);
  }

  auto peers = response.value().as_success()->peers;
  logger::info("Found {} peers", peers.size());
  return peers;
}

const metainfo::Metainfo &DownloadManager::metainfo() const {
  return *m_metainfo;
}

uint64_t DownloadManager::uploaded() const { return m_uploaded; }

uint64_t DownloadManager::downloaded() const { return m_downloaded; }

uint64_t DownloadManager::left() const { return m_left; }

std::string DownloadManager::info_hash() const {
  return m_metainfo->info_hash().value();
}

asio::io_context &DownloadManager::io_context() { return m_io_context; }

bool DownloadManager::peer_has_piece(const std::vector<std::byte>& bitfield, uint32_t piece_index) {
    std::size_t byte_index = piece_index / 8;
    std::size_t bit_index = 7 - (piece_index % 8);

    if (byte_index >= bitfield.size()) {
        return false;
    }

    return (std::to_integer<uint8_t>(bitfield[byte_index]) >> bit_index) & 1;
}

std::string get_request_params(const DownloadManager &download_manager) {
  auto x = escape_info_hash(download_manager.metainfo().info_hash().value());
  return x;
}

std::string escape_info_hash(const bencode::ByteString &info_hash) {
  return url_encode(info_hash);
}

} // namespace download
