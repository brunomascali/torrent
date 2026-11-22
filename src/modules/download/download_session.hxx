#pragma once

#include <array>
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
#include <expected>
#include <memory>
#include <mutex>
#include <set>
#include <metainfo/metainfo.hxx>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace download {

class DownloadSession : private peer::Swarm {
public:
  static std::expected<std::unique_ptr<DownloadSession>, std::string>
  create(std::unique_ptr<const metainfo::Metainfo> metainfo);

  DownloadSession(const DownloadSession &) = delete;
  DownloadSession &operator=(const DownloadSession &) = delete;
  ~DownloadSession() override;

  bool run();

private:
  DownloadSession(std::unique_ptr<const metainfo::Metainfo> metainfo,
                  PieceLayout layout);

  bool on_piece(std::uint32_t index, std::span<const std::byte> data) override;
  std::optional<std::vector<std::byte>>
  read_block(std::uint32_t index, std::uint32_t begin, std::uint32_t length) override;
  bool admit(const peer::PeerId &peer_id) override;
  void leave(const peer::PeerId &peer_id) override;
  bool acquire_upload_slot() override;
  void release_upload_slot() override;

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
  std::uint16_t m_listen_port{6881};

  std::mutex m_peers_mutex;
  std::set<peer::PeerId> m_connected;
  std::atomic<int> m_upload_slots_used{0};

  boost::asio::io_context m_io_context;
};

}
