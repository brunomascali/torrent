#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <io/error.hxx>

namespace asio = boost::asio;
using asio::ip::tcp;

namespace io {

class TcpConnection {
public:
  [[nodiscard]] static asio::awaitable<std::expected<TcpConnection, Error>>
  connect(std::string_view ip, std::uint16_t port,
          std::chrono::steady_clock::duration timeout = std::chrono::seconds(5));

  TcpConnection(TcpConnection &&) noexcept = default;
  TcpConnection &operator=(TcpConnection &&) noexcept = default;
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;

  asio::awaitable<std::expected<void, Error>> async_write(std::span<const std::byte> bytes);
  asio::awaitable<std::expected<std::vector<std::byte>, Error>> async_read_exact(std::size_t n);
  asio::awaitable<std::expected<std::byte, Error>> async_read_byte();
  asio::awaitable<std::expected<std::uint32_t, Error>> async_read_u32();

private:
  TcpConnection(tcp::socket socket, std::string remote);

  tcp::socket m_socket;
  std::string m_remote; // "ip:port", used as error context
};

} // namespace io
