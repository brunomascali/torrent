#include <bencode/accessors.hxx>
#include "crypto/sha1.hxx"
#include "download/peer/peer.hxx"
#include "download/tracker.hxx"
#include "logger/logger.hxx"
#include <cstddef>
#include <cstdint>
#include <download/download_manager.hxx>
#include <download/http_announce.hxx>
#include <memory>
#include <metainfo/metainfo.hxx>
#include <span>

namespace download {


DownloadManager::DownloadManager(
    std::unique_ptr<const metainfo::Metainfo> metainfo)
    : m_metainfo(std::move(metainfo)) {
  m_left = m_metainfo->length();
  m_tracker = Tracker(m_metainfo->trackers().front());
  m_pieces_downloaded = std::vector<bool>(m_metainfo->num_pieces(), false);
  m_pieces_in_progress = std::vector<bool>(m_metainfo->num_pieces(), false);

  logger::debug("Total pieces: {}", m_pieces_in_progress.size());
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

std::optional<uint32_t> DownloadManager::reserve_piece(const std::vector<bool>& peer_bitfield) {
    if (peer_bitfield.size() != m_pieces_downloaded.size()) {
      logger::warn("size(bitfield) {} != size(pieces) {}", peer_bitfield.size(), m_pieces_downloaded.size());
      return std::nullopt;
    }
    std::lock_guard lock(m_piece_mutex);

    logger::debug("peer_bitfield size: {}", peer_bitfield.size());
    logger::debug("m_pieces      size: {}", m_pieces_downloaded.size());
    for (uint32_t i = 0; i < m_pieces_downloaded.size(); ++i) {
        if (m_pieces_downloaded[i] || m_pieces_in_progress[i]) {
            continue;
        }
        if (!peer_bitfield[i]) {
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

bool DownloadManager::verify_piece_hash(uint32_t index, std::span<const std::byte> bytes) const {
  auto expected = metainfo().piece_hash(index);
  auto actual = crypto::sha1(bytes);
  return std::equal(expected.begin(), expected.end(), actual.begin());
}

bool DownloadManager::write_to_disk(uint32_t index, std::span<const std::byte> bytes) {
  uint64_t piece_start = uint64_t{index} * metainfo().piece_length();
  uint64_t piece_end = piece_start + bytes.size();

  auto &file = metainfo().files().front();

  if (!file.handle.is_open()) {
    auto open = file.handle.open();
    if (!open.has_value()) {
      logger::error("Error opening file handle: {}", open.error().str());
    }
  }
  auto write_res = file.handle.write_at(piece_start, bytes);

  if (!write_res.has_value()) {
    logger::warn("Error writing to file: {}", write_res.error().str());
    return false;
  }

  return true;
}

const metainfo::Metainfo &DownloadManager::metainfo() const {
  return *m_metainfo;
}

uint64_t DownloadManager::uploaded() const { return m_uploaded; }

uint64_t DownloadManager::downloaded() const { return m_downloaded; }

uint64_t DownloadManager::left() const { return m_left; }

std::array<std::byte, 20> DownloadManager::info_hash() const {
  return metainfo().info_hash();
}

asio::io_context &DownloadManager::io_context() { return m_io_context; }

std::string get_request_params(const DownloadManager &download_manager) {
  auto x = escape_info_hash(std::span<const std::byte, 20>{download_manager.metainfo().info_hash()});
  return x;
}

std::string escape_info_hash(std::span<const std::byte, 20> info_hash) {
  return url_encode(info_hash);
}

} // namespace download
