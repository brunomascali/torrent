#include <io/tcp_connection.hxx>

#include <format>
#include <utility>

namespace io {

namespace {

Error net_error(std::error_code ec, std::string context) {
  Error e;
  e.code = ec;
  e.context = std::move(context);
  return e;
}

}

TcpConnection::TcpConnection(tcp::socket socket, std::string remote)
    : m_socket(std::move(socket)), m_remote(std::move(remote)) {}

TcpConnection TcpConnection::adopt(tcp::socket socket) {
  boost::system::error_code ec;
  const auto endpoint = socket.remote_endpoint(ec);
  std::string remote = ec ? std::string("unknown")
                          : std::format("{}:{}", endpoint.address().to_string(), endpoint.port());
  return TcpConnection(std::move(socket), std::move(remote));
}

tcp::endpoint TcpConnection::remote_endpoint() const {
  boost::system::error_code ec;
  return m_socket.remote_endpoint(ec);
}

asio::awaitable<std::expected<TcpConnection, Error>>
TcpConnection::connect(std::string_view ip, std::uint16_t port,
                       std::chrono::steady_clock::duration timeout) {
  std::string remote = std::format("{}:{}", ip, port);

  boost::system::error_code ec;
  auto address = asio::ip::make_address(ip, ec);
  if (ec) {
    co_return std::unexpected(net_error(ec, std::move(remote)));
  }

  tcp::socket socket(co_await asio::this_coro::executor);
  auto [connect_ec] = co_await socket.async_connect(
      tcp::endpoint(address, port),
      asio::cancel_after(timeout, asio::as_tuple(asio::use_awaitable)));

  if (connect_ec) {
    std::error_code reported = connect_ec == asio::error::operation_aborted
                                   ? std::make_error_code(std::errc::timed_out)
                                   : std::error_code(connect_ec);
    co_return std::unexpected(net_error(reported, std::move(remote)));
  }

  co_return TcpConnection(std::move(socket), std::move(remote));
}

asio::awaitable<std::expected<void, Error>>
TcpConnection::async_write(std::span<const std::byte> bytes) {
  auto [ec, n] = co_await asio::async_write(
      m_socket, asio::buffer(bytes), asio::as_tuple(asio::use_awaitable));

  if (ec) {
    co_return std::unexpected(net_error(ec, m_remote));
  }
  co_return std::expected<void, Error>{};
}

asio::awaitable<std::expected<void, Error>>
TcpConnection::async_read_exact(std::span<std::byte> buffer) {
  auto [ec, bytes_read] = co_await asio::async_read(
      m_socket, asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));

  if (ec) {
    co_return std::unexpected(net_error(ec, m_remote));
  }
  co_return std::expected<void, Error>{};
}

asio::awaitable<std::expected<std::vector<std::byte>, Error>>
TcpConnection::async_read_exact(std::size_t n) {
  std::vector<std::byte> buf(n);
  if (auto got = co_await async_read_exact(std::span(buf)); !got) {
    co_return std::unexpected(std::move(got.error()));
  }
  co_return buf;
}

}
