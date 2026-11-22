#include <download/peer/peer_connection.hxx>
#include <download/peer/wire/bytes.hxx>
#include <download/peer/wire/handshake.hxx>
#include <download/peer/wire/message.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <memory>

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

struct FakeSwarm : Swarm {
  PiecePicker &picker;
  std::mutex mutex;
  std::map<std::uint32_t, std::vector<std::byte>> pieces;
  std::vector<std::byte> stored;
  bool admit_peers = true;
  bool upload_slots = true;
  int slots_held = 0;
  int admitted = 0;

  explicit FakeSwarm(PiecePicker &p) : picker(p) {}

  bool on_piece(std::uint32_t index, std::span<const std::byte> data) override {
    std::lock_guard lock(mutex);
    pieces[index].assign(data.begin(), data.end());
    picker.complete(index);
    return true;
  }

  std::optional<std::vector<std::byte>>
  read_block(std::uint32_t index, std::uint32_t begin, std::uint32_t length) override {
    const auto at = picker.layout().piece_offset(index) + begin;
    if (!picker.has(index) || at + length > stored.size())
      return std::nullopt;
    return std::vector<std::byte>(stored.begin() + at, stored.begin() + at + length);
  }

  bool admit(const PeerId &) override {
    if (admit_peers)
      ++admitted;
    return admit_peers;
  }
  void leave(const PeerId &) override { --admitted; }

  bool acquire_upload_slot() override {
    if (!upload_slots)
      return false;
    ++slots_held;
    return true;
  }
  void release_upload_slot() override { --slots_held; }
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
  PiecePicker picker;
  FakeSwarm sink{picker};

  Harness() : Harness(PieceLayout::make(total, piece_length, 2).value()) {}
  explicit Harness(PieceLayout layout) : picker(layout) {}

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

  template <class Client> void run_incoming(Client client) {
    std::shared_ptr<PeerConnection> conn;
    asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
      auto socket = co_await acceptor.async_accept(asio::use_awaitable);
      conn = std::make_shared<PeerConnection>(io::TcpConnection::adopt(std::move(socket)),
                                              identity(), picker, sink);
      co_await conn->run();
    }, [](std::exception_ptr e) { if (e) std::rethrow_exception(e); });
    asio::co_spawn(ctx, [&]() -> asio::awaitable<void> {
      tcp::socket s(ctx);
      co_await s.async_connect(acceptor.local_endpoint(), asio::use_awaitable);
      co_await client(s);
    }, [](std::exception_ptr e) { if (e) std::rethrow_exception(e); });
    ctx.run();
  }
};

asio::awaitable<std::optional<wire::Handshake>>
initiate_handshake(tcp::socket &s, std::array<std::byte, 20> info_hash) {
  wire::Handshake ours;
  ours.info_hash = info_hash;
  ours.peer_id.fill(std::byte{'r'});
  auto bytes = ours.encode();
  co_await asio::async_write(s, asio::buffer(bytes), asio::use_awaitable);
  std::array<std::byte, wire::Handshake::size> theirs;
  auto [ec, n] = co_await asio::async_read(s, asio::buffer(theirs),
                                           asio::as_tuple(asio::use_awaitable));
  if (ec)
    co_return std::nullopt;
  co_return wire::Handshake::decode(theirs).value();
}

asio::awaitable<std::size_t> pending_bytes_after(tcp::socket &s, std::chrono::milliseconds wait) {
  asio::steady_timer t(s.get_executor(), wait);
  co_await t.async_wait(asio::use_awaitable);
  co_return s.available();
}

asio::awaitable<bool> closed_by_peer(tcp::socket &s) {
  std::array<std::byte, 1> probe;
  auto [ec, n] = co_await asio::async_read(s, asio::buffer(probe),
                                           asio::as_tuple(asio::use_awaitable));
  co_return ec == asio::error::eof || ec == asio::error::connection_reset;
}

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

TEST(PeerConnection, CapsOutstandingRequestsAndRefillsOnePerBlock) {
  constexpr std::uint64_t big_piece = 4 * 1024 * 1024;
  Harness h(PieceLayout::make(big_piece, big_piece, 1).value());
  std::vector<wire::Request> first_batch;
  std::optional<wire::Request> refill;
  std::size_t extra_bytes = 0;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    co_await send(s, wire::Bitfield{{true}});
    co_await receive(s, 1);
    co_await send(s, wire::Unchoke{});

    for (int i = 0; i < 128; ++i)
      first_batch.push_back(std::get<wire::Request>(co_await receive(s, 1)));

    asio::steady_timer t(s.get_executor(), std::chrono::milliseconds(100));
    co_await t.async_wait(asio::use_awaitable);
    extra_bytes = s.available();

    const auto &r = first_batch.front();
    co_await send(s, wire::Piece{r.index, r.begin, std::vector<std::byte>(r.length)});
    refill = std::get<wire::Request>(co_await receive(s, 1));
    s.close();
  });

  ASSERT_EQ(first_batch.size(), 128u);
  for (std::size_t i = 0; i < first_batch.size(); ++i) {
    EXPECT_EQ(first_batch[i], (wire::Request{0, static_cast<std::uint32_t>(i * 16384), 16384}));
  }
  EXPECT_EQ(extra_bytes, 0u) << "client sent more than 128 requests before any block arrived";
  ASSERT_TRUE(refill);
  EXPECT_EQ(*refill, (wire::Request{0, 128 * 16384, 16384}));
}

TEST(PeerConnection, AcceptsIncomingConnectionAndAnswersHandshake) {
  Harness h;
  std::optional<wire::Handshake> answer;
  std::optional<wire::PeerMessage> after;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    answer = co_await initiate_handshake(s, identity().info_hash);
    co_await send(s, wire::Bitfield{{true, true}});
    after = co_await receive(s, 2);
    s.close();
  });

  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->info_hash, identity().info_hash);
  EXPECT_EQ(answer->peer_id, identity().peer_id);
  ASSERT_TRUE(after);
  EXPECT_TRUE(std::holds_alternative<wire::Interested>(*after));
  EXPECT_EQ(h.sink.admitted, 0);
}

TEST(PeerConnection, IncomingPeerForAnotherTorrentGetsNoHandshake) {
  Harness h;
  std::optional<wire::Handshake> answer;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    std::array<std::byte, 20> other{};
    answer = co_await initiate_handshake(s, other);
  });

  EXPECT_FALSE(answer);
}

TEST(PeerConnection, HangsUpWhenSwarmRefusesPeer) {
  Harness h;
  h.sink.admit_peers = false;
  bool closed = false;

  h.run([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await exchange_handshake(s);
    closed = co_await closed_by_peer(s);
  });

  EXPECT_TRUE(closed);
}

TEST(PeerConnection, UploadsRequestedBlocksOfPiecesWeHave) {
  Harness h;
  h.sink.stored = torrent_data();
  h.picker.complete(0);
  std::optional<wire::PeerMessage> bitfield, unchoke, block;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await initiate_handshake(s, identity().info_hash);
    bitfield = co_await receive(s, 2);
    co_await send(s, wire::Interested{});
    unchoke = co_await receive(s, 2);
    co_await send(s, wire::Request{0, 16384, 16384});
    block = co_await receive(s, 2);
    s.close();
  });

  ASSERT_TRUE(bitfield && unchoke && block);
  EXPECT_EQ(*bitfield, wire::PeerMessage(wire::Bitfield{{true, false}}));
  EXPECT_TRUE(std::holds_alternative<wire::Unchoke>(*unchoke));
  const auto &data = h.sink.stored;
  EXPECT_EQ(*block, wire::PeerMessage(wire::Piece{
                        0, 16384, {data.begin() + 16384, data.begin() + 32768}}));
  EXPECT_EQ(h.sink.slots_held, 0);
}

TEST(PeerConnection, KeepsPeerChokedWithoutFreeUploadSlot) {
  Harness h;
  h.sink.stored = torrent_data();
  h.sink.upload_slots = false;
  h.picker.complete(0);
  std::size_t pending = 1;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await initiate_handshake(s, identity().info_hash);
    co_await receive(s, 2);
    co_await send(s, wire::Interested{});
    co_await send(s, wire::Request{0, 0, 16384});
    pending = co_await pending_bytes_after(s, std::chrono::milliseconds(100));
    s.close();
  });

  EXPECT_EQ(pending, 0u);
}

TEST(PeerConnection, RefusesOversizedAndUnavailableRequests) {
  Harness h;
  h.sink.stored = torrent_data();
  h.picker.complete(0);
  std::size_t pending = 1;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await initiate_handshake(s, identity().info_hash);
    co_await receive(s, 2);
    co_await send(s, wire::Interested{});
    co_await receive(s, 2);
    co_await send(s, wire::Request{0, 0, 32768});
    co_await send(s, wire::Request{1, 0, 16384});
    pending = co_await pending_bytes_after(s, std::chrono::milliseconds(100));
    s.close();
  });

  EXPECT_EQ(pending, 0u);
}

TEST(PeerConnection, AnnouncesNewlyCompletedPiecesWithHave) {
  Harness h;
  std::optional<wire::PeerMessage> have;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await initiate_handshake(s, identity().info_hash);
    h.picker.complete(1);
    co_await send(s, wire::KeepAlive{});
    have = co_await receive(s, 2);
    s.close();
  });

  ASSERT_TRUE(have);
  EXPECT_EQ(*have, wire::PeerMessage(wire::Have{1}));
}

TEST(PeerConnection, ReleasesUploadSlotWhenPeerLosesInterest) {
  Harness h;
  h.picker.complete(0);
  std::optional<wire::PeerMessage> choke;
  int held_while_interested = -1;

  h.run_incoming([&](tcp::socket &s) -> asio::awaitable<void> {
    co_await initiate_handshake(s, identity().info_hash);
    co_await receive(s, 2);
    co_await send(s, wire::Interested{});
    co_await receive(s, 2);
    held_while_interested = h.sink.slots_held;
    co_await send(s, wire::NotInterested{});
    choke = co_await receive(s, 2);
    s.close();
  });

  EXPECT_EQ(held_while_interested, 1);
  ASSERT_TRUE(choke);
  EXPECT_TRUE(std::holds_alternative<wire::Choke>(*choke));
  EXPECT_EQ(h.sink.slots_held, 0);
}
