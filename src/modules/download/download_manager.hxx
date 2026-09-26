#pragma once

#include "bencode/bencode.hxx"
#include "download/tracker.hxx"
#include <cstdint>
#include <metainfo/metainfo.hxx>

namespace download {

class DownloadManager {
public:
  explicit DownloadManager(const metainfo::Metainfo& metainfo);

  void start_download();

  const metainfo::Metainfo& metainfo() const;

  uint64_t uploaded() const;
  uint64_t downloaded() const;
  uint64_t left() const;

private:
  const metainfo::Metainfo &m_metainfo;
  uint64_t m_uploaded{0};
  uint64_t m_downloaded{0};
  uint64_t m_left{0};

  Tracker m_tracker{};

  void announce();
  void refresh_peers();
};

std::string get_request_params(const DownloadManager& download_manager);
std::string escape_info_hash(const bencode::ByteString& info_hash);

} // namespace download
