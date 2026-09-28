#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <io/tcp_connection.hxx>

namespace io {

TcpConnection::TcpConnection(asio::io_context &io_context, std::string_view ip, uint16_t port)
  : m_io_context(io_context), m_ip(ip), m_port(port) {}

asio::awaitable<std::expected<void, std::string>>
TcpConnection::async_connect() {
  m_socket.emplace(m_io_context);
  tcp::endpoint endpoint(asio::ip::make_address(m_ip), m_port);

  auto [ec] = co_await m_socket->async_connect(
      endpoint, asio::as_tuple(asio::use_awaitable));

  if (ec) {
    co_return std::unexpected("connect failed: " + ec.message());
  }
  co_return std::expected<void, std::string>{};
}

asio::awaitable<std::expected<void, std::string>>
TcpConnection::async_write(std::span<const std::byte> bytes) {
  auto [ec, n] = co_await asio::async_write(
      *m_socket, asio::buffer(bytes), asio::as_tuple(asio::use_awaitable));

  if (ec) {
    co_return std::unexpected("write failed: " + ec.message());
  }
  co_return std::expected<void, std::string>{};
}

asio::awaitable<std::expected<std::vector<std::byte>, std::string>>
TcpConnection::async_read_exact(std::size_t n) {
  std::vector<std::byte> buf(n);
  auto [ec, bytes_read] = co_await asio::async_read(
      *m_socket, asio::buffer(buf), asio::as_tuple(asio::use_awaitable));

  if (ec) {
    co_return std::unexpected("read failed: " + ec.message());
  }
  co_return buf;
}

asio::awaitable<std::expected<uint32_t, std::string>>
TcpConnection::async_read_u32() {
  auto bytes = co_await async_read_exact(4);
  if (!bytes)
    co_return std::unexpected(bytes.error());

  uint32_t number = (std::to_integer<uint32_t>((*bytes)[0]) << 24) |
                    (std::to_integer<uint32_t>((*bytes)[1]) << 16) |
                    (std::to_integer<uint32_t>((*bytes)[2]) << 8) |
                    std::to_integer<uint32_t>((*bytes)[3]);
  co_return number;
}

asio::awaitable<std::expected<std::byte, std::string>>
TcpConnection::async_read_byte() {
  auto bytes = co_await async_read_exact(1);
  if (!bytes)
    co_return std::unexpected(bytes.error());
  co_return (*bytes)[0];
}

} // namespace io
