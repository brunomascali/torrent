#include <io/tcp_connection.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <optional>

namespace {

// Runs a coroutine to completion on a fresh io_context and returns its result.
// The result is stored from inside the coroutine because co_spawn's handler
// needs a default-constructible value type, which these results are not.
template <class T> T run(asio::io_context &ctx, asio::awaitable<T> aw) {
  std::optional<T> result;
  asio::co_spawn(
      ctx,
      [&]() -> asio::awaitable<void> { result.emplace(co_await std::move(aw)); },
      [](std::exception_ptr e) {
        if (e)
          std::rethrow_exception(e);
      });
  ctx.run();
  return std::move(*result);
}

} // namespace

TEST(TcpConnectionTest, ConnectsAndExchangesBytes) {
  asio::io_context ctx;
  tcp::acceptor acceptor(ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  auto port = acceptor.local_endpoint().port();

  // Server: accept one client, echo 5 bytes back.
  asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    std::array<std::byte, 5> buf;
    co_await asio::async_read(socket, asio::buffer(buf), asio::use_awaitable);
    co_await asio::async_write(socket, asio::buffer(buf), asio::use_awaitable);
  }, asio::detached);

  auto result = run(ctx, [&]() -> asio::awaitable<std::expected<std::vector<std::byte>, io::Error>> {
    auto conn = co_await io::TcpConnection::connect("127.0.0.1", port);
    if (!conn)
      co_return std::unexpected(conn.error());

    const std::array<std::byte, 5> msg{std::byte{0}, std::byte{0}, std::byte{1},
                                       std::byte{2}, std::byte{42}};
    if (auto w = co_await conn->async_write(msg); !w)
      co_return std::unexpected(w.error());

    auto number = co_await conn->async_read_u32();
    if (!number)
      co_return std::unexpected(number.error());
    EXPECT_EQ(*number, 0x00000102u);

    auto byte = co_await conn->async_read_byte();
    if (!byte)
      co_return std::unexpected(byte.error());
    co_return std::vector<std::byte>{*byte};
  }());

  ASSERT_TRUE(result.has_value()) << result.error().str();
  EXPECT_EQ(result->front(), std::byte{42});
}

TEST(TcpConnectionTest, ReportsRefusedConnection) {
  asio::io_context ctx;
  // Bind and close to get a port with nothing listening on it.
  std::uint16_t port;
  {
    tcp::acceptor acceptor(ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    port = acceptor.local_endpoint().port();
  }

  auto result = run(ctx, io::TcpConnection::connect("127.0.0.1", port));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, std::errc::connection_refused);
  EXPECT_EQ(result.error().context, std::format("127.0.0.1:{}", port));
}

TEST(TcpConnectionTest, RejectsInvalidAddressWithoutThrowing) {
  asio::io_context ctx;
  auto result = run(ctx, io::TcpConnection::connect("not-an-ip", 6881));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().context, "not-an-ip:6881");
}

TEST(TcpConnectionTest, ReadFailsWhenPeerCloses) {
  asio::io_context ctx;
  tcp::acceptor acceptor(ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  auto port = acceptor.local_endpoint().port();

  // Server: accept and immediately close.
  asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    socket.close();
  }, asio::detached);

  auto result = run(ctx, [&]() -> asio::awaitable<std::expected<std::vector<std::byte>, io::Error>> {
    auto conn = co_await io::TcpConnection::connect("127.0.0.1", port);
    if (!conn)
      co_return std::unexpected(conn.error());
    co_return co_await conn->async_read_exact(4);
  }());

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, std::error_code(boost::system::error_code(asio::error::eof)));
}
