#include <download/peer/wire/bytes.hxx>
#include <download/peer/wire/stream.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <optional>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

using namespace download::peer::wire;

namespace {

constexpr std::size_t num_pieces = 10;

template <class T>
T with_server(std::vector<std::byte> server_bytes,
              std::function<asio::awaitable<T>(io::TcpConnection &)> client) {
  asio::io_context ctx;
  tcp::acceptor acceptor(ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  auto port = acceptor.local_endpoint().port();

  asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    co_await asio::async_write(socket, asio::buffer(server_bytes), asio::use_awaitable);
  }, asio::detached);

  std::optional<T> result;
  asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
    auto conn = co_await io::TcpConnection::connect("127.0.0.1", port);
    if (!conn) {
      ADD_FAILURE() << conn.error().str();
      co_return;
    }
    result.emplace(co_await client(*conn));
  }, [](std::exception_ptr e) { if (e) std::rethrow_exception(e); });

  ctx.run();
  return std::move(*result);
}

std::vector<std::byte> concat(std::initializer_list<std::vector<std::byte>> parts) {
  std::vector<std::byte> out;
  for (const auto &p : parts)
    out.insert(out.end(), p.begin(), p.end());
  return out;
}

}

TEST(WireStream, ReadsConsecutiveMessages) {
  auto stream = concat({encode(KeepAlive{}), encode(Unchoke{}),
                        encode(Piece{2, 0, {std::byte{7}, std::byte{8}}})});

  auto messages = with_server<std::vector<PeerMessage>>(
      stream, [](io::TcpConnection &conn) -> asio::awaitable<std::vector<PeerMessage>> {
        std::vector<PeerMessage> out;
        for (int i = 0; i < 3; ++i) {
          auto m = co_await read_message(conn, num_pieces);
          EXPECT_TRUE(m) << m.error().str();
          if (!m)
            break;
          out.push_back(std::move(*m));
        }
        co_return out;
      });

  ASSERT_EQ(messages.size(), 3u);
  EXPECT_EQ(messages[0], PeerMessage(KeepAlive{}));
  EXPECT_EQ(messages[1], PeerMessage(Unchoke{}));
  EXPECT_EQ(messages[2], PeerMessage(Piece{2, 0, {std::byte{7}, std::byte{8}}}));
}

TEST(WireStream, RejectsOversizedLengthBeforeReadingBody) {
  std::vector<std::byte> prefix;
  append_u32_be(prefix, max_message_length + 1);

  auto r = with_server<std::expected<PeerMessage, Error>>(
      prefix, [](io::TcpConnection &conn) { return read_message(conn, num_pieces); });

  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::message_too_large);
}

TEST(WireStream, ReportsTruncatedMessageAsEof) {
  auto frame = encode(Have{1});
  frame.pop_back();

  auto r = with_server<std::expected<PeerMessage, Error>>(
      frame, [](io::TcpConnection &conn) { return read_message(conn, num_pieces); });

  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, std::error_code(boost::system::error_code(asio::error::eof)));
  EXPECT_FALSE(r.error().context.empty());
}

TEST(WireStream, ReadsHandshake) {
  Handshake h;
  h.info_hash[0] = std::byte{0xab};
  auto bytes = h.encode();

  auto r = with_server<std::expected<Handshake, Error>>(
      {bytes.begin(), bytes.end()},
      [](io::TcpConnection &conn) { return read_handshake(conn); });

  ASSERT_TRUE(r) << r.error().str();
  EXPECT_EQ(*r, h);
}
