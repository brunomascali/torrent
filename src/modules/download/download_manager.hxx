#pragma once

#include "boost/asio/io_context.hpp"
#include <download/tracker.hxx>
#include <download/peer/peer.hxx>
#include <download/peer/peer_connection.hxx>
#include <bencode/bencode.hxx>
#include <cstdint>
#include <metainfo/metainfo.hxx>
#include <mutex>

namespace download {

class DownloadManager {
public:
  explicit DownloadManager(std::unique_ptr<const metainfo::Metainfo> metainfo);

  void start_download();

  const metainfo::Metainfo& metainfo() const;

  uint64_t uploaded() const;
  uint64_t downloaded() const;
  uint64_t left() const;
  std::string info_hash() const;

  asio::io_context& io_context();

private:
  std::unique_ptr<const metainfo::Metainfo> m_metainfo;
  uint64_t m_uploaded{0};
  uint64_t m_downloaded{0};
  uint64_t m_left{0};

  Tracker m_tracker{};
  asio::io_context m_io_context;
  std::vector<std::unique_ptr<peer::PeerConnection>> m_peer_connections{};
  std::mutex m_mutex;

  std::vector<peer::Peer> fetch_peers();
};

std::string get_request_params(const DownloadManager& download_manager);
std::string escape_info_hash(const bencode::ByteString& info_hash);

} // namespace download
