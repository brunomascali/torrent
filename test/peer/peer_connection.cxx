#include <download/peer/peer_connection.hxx>
#include <download/peer/wire/bytes.hxx>
#include <download/peer/wire/handshake.hxx>
#include <download/peer/wire/message.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <map>
#include <mutex>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace download;
using namespace download::peer;

namespace {

constexpr std::uint64_t total = 40000;
constexpr std::uint64_t piece_length = 32768;

std::vector<std::byte> torrent_data() {
  std::vector<std::byte> d(total);
  for (std::size_t i = 0; i < d.size(); ++i)
    d[i] = static_cast<std::byte>(i * 7);
  return d;
}

Identity identity() {
  Identity id{};
  id.info_hash.fill(std::byte{0xab});
  id.peer_id.fill(std::byte{'c'});
  return id;
}

struct FakeSink : PieceSink {
  PiecePicker &picker;
  std::mutex mutex;
  std::map<std::uint32_t, std::vector<std::byte>> pieces;

  explicit FakeSink(PiecePicker &p) : picker(p) {}

  bool on_piece(std::uint32_t index, std::span<const std::byte> data) override {
    std::lock_guard lock(mutex);
    pieces[index].assign(data.begin(), data.end());
    picker.complete(index);
    return true;
  }
};

asio::awaitable<void> send(tcp::socket &s, const wire::PeerMessage &m) {
  auto bytes = wire::encode(m);
  co_await asio::async_write(s, asio::buffer(bytes), asio::use_awaitable);
}

asio::awaitable<wire::PeerMessage> receive(tcp::socket &s, std::size_t num_pieces) {
  std::array<std::byte, 4> prefix;
  co_await asio::async_read(s, asio::buffer(prefix), asio::use_awaitable);
  std::vector<std::byte> body(wire::load_u32_be(prefix));
  co_await asio::async_read(s, asio::buffer(body), asio::use_awaitable);
  co_return wire::decode(body, num_pieces).value();
}

asio::awaitable<void> exchange_handshake(tcp::socket &s) {
  std::array<std::byte, wire::Handshake::size> theirs;
  co_await asio::async_read(s, asio::buffer(theirs), asio::use_awaitable);
  wire::Handshake ours = wire::Handshake::decode(theirs).value();
  ours.peer_id.fill(std::byte{'s'});
  auto bytes = ours.encode();
  co_await asio::async_write(s, asio::buffer(bytes), asio::use_awaitable);
}

struct Harness {
  asio::io_context ctx;
  tcp::acceptor acceptor{ctx, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)};
  PiecePicker picker{PieceLayout::make(total, piece_length, 2).value()};
  FakeSink sink{picker};

  Peer peer() const {
    return Peer{.peer_id = std::nullopt, .ip = "127.0.0.1",
                .port = acceptor.local_endpoint().port()};
  }

  template <class Server> void run(Server server) {
    PeerConnection conn(peer(), identity(), picker, sink);
    asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
      auto socket = co_await acceptor.async_accept(asio::use_awaitable);
      co_await server(socket);
    }, [](std::exception_ptr e) { if (e) std::rethrow_exception(e); });
    asio::co_spawn(ctx, conn.run(), [](std::exception_ptr e) {
      if (e) std::rethrow_exception(e);
    });
    ctx.run();
  }
};

}

TEST(PeerConnection, DownloadsAllPiecesFromPeerThatOnlySendsHave) {
  Harness h;
  const auto data = torrent_data();
  std::vector<wire::Request> requests;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    co_await send(s, wire::Have{0});
    co_await send(s, wire::Have{1});

    auto interested = co_await receive(s, 2);
    EXPECT_TRUE(std::holds_alternative<wire::Interested>(interested));
    co_await send(s, wire::Unchoke{});

    for (int i = 0; i < 3; ++i) {
      auto m = co_await receive(s, 2);
      auto r = std::get<wire::Request>(m);
      requests.push_back(r);
      const auto at = piece_length * r.index + r.begin;
      co_await send(s, wire::Piece{r.index, r.begin,
                                   {data.begin() + at, data.begin() + at + r.length}});
    }
    s.close();
  });

  ASSERT_EQ(requests.size(), 3u);
  EXPECT_EQ(requests[0], (wire::Request{0, 0, 16384}));
  EXPECT_EQ(requests[1], (wire::Request{0, 16384, 16384}));
  EXPECT_EQ(requests[2], (wire::Request{1, 0, 7232}));

  ASSERT_EQ(h.sink.pieces.size(), 2u);
  EXPECT_TRUE(std::ranges::equal(h.sink.pieces[0], std::span(data).first(piece_length)));
  EXPECT_TRUE(std::ranges::equal(h.sink.pieces[1], std::span(data).subspan(piece_length)));
  EXPECT_TRUE(h.picker.is_complete());
}

TEST(PeerConnection, DoesNotRequestWhileChoked) {
  Harness h;
  bool got_request = false;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    co_await send(s, wire::Bitfield{{true, true}});
    auto m = co_await receive(s, 2);
    EXPECT_TRUE(std::holds_alternative<wire::Interested>(m));
    co_await send(s, wire::KeepAlive{});
    asio::steady_timer t(s.get_executor(), std::chrono::milliseconds(50));
    co_await t.async_wait(asio::use_awaitable);
    s.non_blocking(true);
    std::array<std::byte, 1> probe;
    boost::system::error_code ec;
    got_request = s.read_some(asio::buffer(probe), ec) > 0;
    s.close();
  });

  EXPECT_FALSE(got_request);
  EXPECT_TRUE(h.sink.pieces.empty());
}

TEST(PeerConnection, ReleasesPiecesWhenPeerChokes) {
  Harness h;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    co_await send(s, wire::Bitfield{{true, true}});
    co_await receive(s, 2);
    co_await send(s, wire::Unchoke{});
    for (int i = 0; i < 3; ++i)
      co_await receive(s, 2);

    EXPECT_EQ(h.picker.reserve({true, true}), std::nullopt);

    co_await send(s, wire::Choke{});
    co_await send(s, wire::KeepAlive{});
    asio::steady_timer t(s.get_executor(), std::chrono::milliseconds(50));
    co_await t.async_wait(asio::use_awaitable);

    EXPECT_TRUE(h.picker.reserve({true, true}).has_value());
    s.close();
  });
}

TEST(PeerConnection, ReleasesPiecesWhenPeerDisconnects) {
  Harness h;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    co_await send(s, wire::Bitfield{{true, false}});
    co_await receive(s, 2);
    co_await send(s, wire::Unchoke{});
    co_await receive(s, 2);
    s.close();
  });

  EXPECT_TRUE(h.sink.pieces.empty());
  EXPECT_EQ(h.picker.reserve({true, false}), 0u);
}

TEST(PeerConnection, DropsPeerWithWrongInfoHash) {
  Harness h;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    std::array<std::byte, wire::Handshake::size> theirs;
    co_await asio::async_read(s, asio::buffer(theirs), asio::use_awaitable);
    wire::Handshake ours;
    ours.info_hash.fill(std::byte{0x00});
    auto bytes = ours.encode();
    co_await asio::async_write(s, asio::buffer(bytes), asio::use_awaitable);
    std::array<std::byte, 1> probe;
    auto [ec, n] = co_await asio::async_read(s, asio::buffer(probe),
                                             asio::as_tuple(asio::use_awaitable));
    EXPECT_EQ(ec, asio::error::eof);
  });
}
