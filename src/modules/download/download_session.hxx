#pragma once

#include <array>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <cstddef>
#include <cstdint>
#include <download/peer/peer.hxx>
#include <download/peer/peer_connection.hxx>
#include <download/peer/piece_sink.hxx>
#include <download/piece_picker.hxx>
#include <download/storage.hxx>
#include <download/tracker/tracker_client.hxx>
#include <expected>
#include <memory>
#include <metainfo/metainfo.hxx>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace download {

class DownloadSession : private peer::PieceSink {
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

  bool open_storage();
  std::vector<peer::Peer> announce(tracker::Event event);
  bool hash_matches(std::uint32_t index, std::span<const std::byte> data) const;

  std::unique_ptr<const metainfo::Metainfo> m_metainfo;
  PiecePicker m_picker;
  tracker::TrackerClient m_tracker;
  peer::Identity m_identity;
  std::optional<Storage> m_storage;
  std::atomic<std::uint64_t> m_downloaded{0};
  std::uint64_t m_uploaded{0};
  std::vector<std::unique_ptr<peer::PeerConnection>> m_peer_connections;
  boost::asio::io_context m_io_context;
};

}
