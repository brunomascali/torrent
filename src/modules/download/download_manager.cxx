#include "download/tracker.hxx"
#include <download/download_manager.hxx>
#include <download/http_announce.hxx>
#include <metainfo/metainfo.hxx>
#include <print>

namespace download {

DownloadManager::DownloadManager(const metainfo::Metainfo &metainfo)
    : m_metainfo(metainfo) {
  m_left = m_metainfo.total_size().value();
  m_tracker = Tracker(m_metainfo.announce().value());
}

void DownloadManager::start_download() {
  this->refresh_peers();
}

void DownloadManager::refresh_peers() {
  HttpAnnounceRequest req{
    .info_hash = m_metainfo.info_hash().value_or(""),
    .peer_id = "-TR2940-123456789012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = m_uploaded,
    .downloaded = m_downloaded,
    .left = m_left,
  };

  auto response = m_tracker.announce(req);
}

const metainfo::Metainfo& DownloadManager::metainfo() const {
  return m_metainfo;
}

uint64_t DownloadManager::uploaded() const {
  return m_uploaded;
}

uint64_t DownloadManager::downloaded() const {
  return m_downloaded;
}

uint64_t DownloadManager::left() const {
  return m_left;
}

std::string get_request_params(const DownloadManager& download_manager) {
  auto x = escape_info_hash(download_manager.metainfo().info_hash().value());
  return x;
}

std::string escape_info_hash(const bencode::ByteString& info_hash) {
  std::println("info_hash = {}", info_hash);
  return url_encode(info_hash);
}

} // namespace download
