#pragma once

#include <array>
#include <chrono>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <cstddef>
#include <cstdint>
#include <download/peer/peer.hxx>
#include <download/peer/peer_connection.hxx>
#include <download/peer/swarm.hxx>
#include <download/piece_picker.hxx>
#include <download/storage.hxx>
#include <download/tracker/tracker_client.hxx>
#include <download/transfer_rate.hxx>
#include <boost/asio/awaitable.hpp>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <metainfo/metainfo.hxx>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace download {

struct TransferStats {
  std::uint64_t downloaded;
  std::uint64_t uploaded;
  double download_rate;
  double upload_rate;
  std::size_t completed_pieces;
  std::size_t total_pieces;
  std::uint64_t bytes_left;
  std::size_t peers;

  double progress() const noexcept;
  std::optional<std::chrono::seconds> eta() const noexcept;
};

std::string describe(const TransferStats &stats);

class DownloadSession : private peer::Swarm {
public:
  static std::expected<std::unique_ptr<DownloadSession>, std::string>
  create(std::unique_ptr<const metainfo::Metainfo> metainfo);

  DownloadSession(const DownloadSession &) = delete;
  DownloadSession &operator=(const DownloadSession &) = delete;
  ~DownloadSession() override;

  bool run();

  using ProgressHandler = std::function<void(const TransferStats &)>;

  TransferStats stats() const;
  void set_progress_handler(ProgressHandler handler);

private:
  DownloadSession(std::unique_ptr<const metainfo::Metainfo> metainfo,
                  PieceLayout layout);

  void on_block_received(std::size_t bytes) override;
  bool on_piece(std::uint32_t index, std::span<const std::byte> data) override;
  std::optional<std::vector<std::byte>>
  read_block(std::uint32_t index, std::uint32_t begin, std::uint32_t length) override;
  bool admit(const peer::PeerId &peer_id) override;
  void leave(const peer::PeerId &peer_id) override;
  bool acquire_upload_slot() override;
  void release_upload_slot() override;

  boost::asio::awaitable<void> report_progress();
  void spawn(std::shared_ptr<peer::PeerConnection> connection);

  bool open_storage();
  std::vector<peer::Peer> announce(tracker::Event event);
  bool hash_matches(std::uint32_t index, std::span<const std::byte> data) const;

  std::unique_ptr<const metainfo::Metainfo> m_metainfo;
  PiecePicker m_picker;
  tracker::TrackerClient m_tracker;
  peer::Identity m_identity;
  std::optional<Storage> m_storage;
  std::atomic<std::uint64_t> m_downloaded{0};
  std::atomic<std::uint64_t> m_uploaded{0};
  TransferRate m_download_rate;
  TransferRate m_upload_rate;
  std::uint16_t m_listen_port{6881};
  ProgressHandler m_progress_handler;

  mutable std::mutex m_peers_mutex;
  std::set<peer::PeerId> m_connected;
  std::atomic<int> m_upload_slots_used{0};

  boost::asio::io_context m_io_context;
};

}
