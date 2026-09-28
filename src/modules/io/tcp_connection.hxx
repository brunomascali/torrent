#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

namespace io {

class TcpConnection {
public:
  TcpConnection() = delete;
  TcpConnection(asio::io_context &io_context, std::string_view ip, uint16_t port);

  asio::awaitable<std::expected<void, std::string>> async_connect();
  asio::awaitable<std::expected<void, std::string>> async_write(std::span<const std::byte> bytes);
  asio::awaitable<std::expected<std::vector<std::byte>, std::string>> async_read_exact(std::size_t n);
  asio::awaitable<std::expected<std::byte, std::string>> async_read_byte();
  asio::awaitable<std::expected<uint32_t, std::string>> async_read_u32();

private:
  asio::io_context &m_io_context;
  std::string m_ip;
  uint16_t m_port;
  std::optional<tcp::socket> m_socket{std::nullopt};
};

} // namespace io
