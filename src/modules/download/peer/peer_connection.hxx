#pragma once

#include "boost/asio/awaitable.hpp"
#include "download/peer/peer.hxx"
#include "download/peer/message.hxx"
#include "io/tcp_connection.hxx"
#include <download/peer/incoming_piece.hxx>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <boost/asio.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

namespace download {
  struct DownloadManager;
}

namespace download::peer {

  class PeerConnection {
    public:
      PeerConnection(const Peer& peer, DownloadManager& download_manager);

      asio::awaitable<void> prepare_connection();

    private:
      DownloadManager& m_download_manager_ref;
      const Peer m_peer;
      std::array<std::byte, 20> m_peer_id;
      // State
      bool m_choked{true};
      bool m_interested{false};
      std::vector<bool> m_bitfield;
      // TCP Connection
      std::unique_ptr<io::TcpConnection> m_tcp_connection;
      // uint32_t m_incoming_pieces{0};
      std::map<uint32_t, download::peer::IncomingPiece> m_incoming_pieces;

      asio::awaitable<void> main_loop();
      asio::awaitable<std::expected<void, std::string>> handshake(std::span<const std::byte, 20>);

      asio::awaitable<void> handle_msg(const Choke&);
      asio::awaitable<void> handle_msg(const Unchoke&);
      asio::awaitable<void> handle_msg(const Interested&);
      asio::awaitable<void> handle_msg(const NotInterested&);
      asio::awaitable<void> handle_msg(const Have&);
      asio::awaitable<void> handle_msg(const Bitfield&);
      asio::awaitable<void> handle_msg(const Request&);
      asio::awaitable<void> handle_msg(const Piece&);
      asio::awaitable<void> handle_msg(const Cancel&);
      asio::awaitable<void> handle_msg(const KeepAlive&);

      std::vector<std::byte> create_handshake_message(const std::string& info_hash);

      asio::awaitable<void> fill_requests();

      asio::awaitable<std::expected<PeerMessage, std::string>> receive_message();
      asio::awaitable<bool> send_message(const PeerMessage& message);
  };

  std::array<std::byte, 20> make_peer_id();
}
