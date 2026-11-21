#include <io/tcp_connection.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <optional>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

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

}

TEST(TcpConnectionTest, ConnectsAndExchangesBytes) {
  asio::io_context ctx;
  tcp::acceptor acceptor(ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  auto port = acceptor.local_endpoint().port();

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

    std::array<std::byte, 4> head;
    if (auto r = co_await conn->async_read_exact(head); !r)
      co_return std::unexpected(r.error());
    EXPECT_EQ(head, (std::array<std::byte, 4>{msg[0], msg[1], msg[2], msg[3]}));

    co_return co_await conn->async_read_exact(1);
  }());

  ASSERT_TRUE(result.has_value()) << result.error().str();
  EXPECT_EQ(result->front(), std::byte{42});
}

TEST(TcpConnectionTest, ReportsRefusedConnection) {
  asio::io_context ctx;
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
