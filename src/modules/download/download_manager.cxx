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

std::string get_request_params(const DownloadManager &download_manager) {
  auto x = escape_info_hash(download_manager.metainfo().info_hash().value());
  return x;
}

std::string escape_info_hash(const bencode::ByteString &info_hash) {
  return url_encode(info_hash);
}

} // namespace download
