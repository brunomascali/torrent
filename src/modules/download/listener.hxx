#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <io/tcp_connection.hxx>
#include <string>
#include <vector>

namespace download {

class Listener {
public:
  using AcceptHandler = std::function<void(io::TcpConnection)>;

  static std::expected<Listener, std::string>
  open(boost::asio::any_io_executor executor, std::uint16_t first_port,
       std::uint16_t last_port);

  std::uint16_t port() const noexcept { return m_port; }
  bool accepts_ipv6() const noexcept { return m_acceptors.size() > 1; }

  static void start(Listener listener, AcceptHandler on_accept);

private:
  Listener(std::vector<boost::asio::ip::tcp::acceptor> acceptors, std::uint16_t port)
      : m_acceptors(std::move(acceptors)), m_port(port) {}

  std::vector<boost::asio::ip::tcp::acceptor> m_acceptors;
  std::uint16_t m_port;
};

}
