#pragma once

#include "download/peer/peer.hxx"
#include "download/peer/message.hxx"
#include "io/tcp_connection.hxx"
#include <map>
#include <string>
#include <type_traits>
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
      std::string m_info_hash;
      // State
      bool m_choked{true};
      bool m_interested{false};
      std::vector<std::byte> m_bitfield;
      // TCP Connection
      std::unique_ptr<io::TcpConnection> m_tcp_connection;

      asio::awaitable<void> main_loop();
      asio::awaitable<std::expected<void, std::string>> handshake(const std::string& info_hash);

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

      asio::awaitable<std::expected<PeerMessage, std::string>> receive_message();
      asio::awaitable<bool> send_message(const PeerMessage& message);

  };
}
