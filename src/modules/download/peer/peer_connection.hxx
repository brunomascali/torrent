#pragma once

#include <array>
#include <boost/asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <download/peer/incoming_piece.hxx>
#include <download/peer/peer.hxx>
#include <download/peer/piece_sink.hxx>
#include <download/peer/wire/message.hxx>
#include <download/piece_picker.hxx>
#include <expected>
#include <io/tcp_connection.hxx>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace download::peer {

struct Identity {
  std::array<std::byte, 20> info_hash;
  std::array<std::byte, 20> peer_id;
};

class PeerConnection {
public:
  PeerConnection(Peer peer, Identity identity, PiecePicker &picker,
                 PieceSink &sink);

  boost::asio::awaitable<void> run();

private:
  const Peer m_peer;
  const Identity m_identity;
  PiecePicker &m_picker;
  PieceSink &m_sink;

  bool m_choked{true};
  bool m_interested{false};
  std::vector<bool> m_peer_has;

  std::optional<io::TcpConnection> m_tcp_connection;
  std::map<std::uint32_t, IncomingPiece> m_incoming_pieces;

  boost::asio::awaitable<void> main_loop();
  boost::asio::awaitable<std::expected<void, std::string>> handshake();

  boost::asio::awaitable<void> handle_msg(const wire::Choke &);
  boost::asio::awaitable<void> handle_msg(const wire::Unchoke &);
  boost::asio::awaitable<void> handle_msg(const wire::Interested &);
  boost::asio::awaitable<void> handle_msg(const wire::NotInterested &);
  boost::asio::awaitable<void> handle_msg(const wire::Have &);
  boost::asio::awaitable<void> handle_msg(const wire::Bitfield &);
  boost::asio::awaitable<void> handle_msg(const wire::Request &);
  boost::asio::awaitable<void> handle_msg(const wire::Piece &);
  boost::asio::awaitable<void> handle_msg(const wire::Cancel &);
  boost::asio::awaitable<void> handle_msg(const wire::KeepAlive &);

  boost::asio::awaitable<void> update_interest();
  boost::asio::awaitable<void> fill_requests();
  void release_pending_pieces();

  boost::asio::awaitable<bool> send_message(const wire::PeerMessage &message);
};

}
