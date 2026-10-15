#pragma once

#include "download/peer/message.hxx"
#include <download/tracker.hxx>
#include <download/peer/peer.hxx>
#include <download/peer/peer_connection.hxx>
#include <bencode/bencode.hxx>
#include <cstdint>
#include <metainfo/metainfo.hxx>
#include <mutex>

#include <boost/asio/io_context.hpp>

namespace download {

class DownloadManager {
public:
  explicit DownloadManager(std::unique_ptr<const metainfo::Metainfo> metainfo);

  void start_download();

  std::optional<uint32_t> reserve_piece(const std::vector<bool>& peer_bitfield);
  void release_piece(uint32_t piece_index);
  void mark_piece_complete(uint32_t piece_index);
  bool verify_piece_hash(uint32_t index, std::span<const std::byte> bytes) const;
  bool write_to_disk(uint32_t index, std::span<const std::byte> bytes);

  const metainfo::Metainfo& metainfo() const;

  uint64_t uploaded() const;
  uint64_t downloaded() const;
  uint64_t left() const;
  std::array<std::byte, 20> info_hash() const;

  asio::io_context& io_context();

private:
  std::unique_ptr<const metainfo::Metainfo> m_metainfo;
  uint64_t m_uploaded{0};
  uint64_t m_downloaded{0};
  uint64_t m_left{0};

  std::mutex m_piece_mutex;
  std::vector<bool> m_pieces_downloaded;
  std::vector<bool> m_pieces_in_progress;

  Tracker m_tracker{};
  asio::io_context m_io_context;
  std::vector<std::unique_ptr<peer::PeerConnection>> m_peer_connections{};
  std::mutex m_mutex;

  std::vector<peer::Peer> fetch_peers();
  bool peer_has_piece(const std::vector<std::byte>& bitfield, uint32_t piece_index);
};

std::string get_request_params(const DownloadManager& download_manager);
std::string escape_info_hash(std::span<const std::byte, 20> info_hash);


} // namespace download
