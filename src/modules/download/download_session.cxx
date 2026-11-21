#include <download/download_session.hxx>

#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <crypto/sha1.hxx>
#include <download/peer_id.hxx>
#include <logger/logger.hxx>
#include <thread>

namespace download {

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

  auto peers = announce(tracker::Event::started);
  if (peers.empty()) {
    logger::error("No peers to download from");
    return false;
  }

  peer::PieceSink &sink = *this;
  for (auto &peer : peers) {
    auto conn = std::make_unique<peer::PeerConnection>(std::move(peer), m_identity,
                                                       m_picker, sink);
    auto *raw = conn.get();
    m_peer_connections.push_back(std::move(conn));

    boost::asio::co_spawn(m_io_context, raw->run(), [](std::exception_ptr e) {
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
      .port = 6881,
      .uploaded = m_uploaded,
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
