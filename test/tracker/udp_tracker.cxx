#include <download/tracker/tracker_client.hxx>
#include <download/tracker/udp_tracker.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <functional>
#include <mutex>
#include <cerrno>
#include <sys/socket.h>
#include <thread>

using namespace download::tracker;
namespace asio = boost::asio;
using udp_proto = asio::ip::udp;

namespace {

template <class T> void put(std::vector<std::byte> &out, T value) {
  for (std::size_t i = 0; i < sizeof(T); ++i)
    out.push_back(static_cast<std::byte>(value >> (8 * (sizeof(T) - 1 - i))));
}

template <class T> T get(std::span<const std::byte> in, std::size_t at) {
  T v = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i)
    v = static_cast<T>((v << 8) | std::to_integer<T>(in[at + i]));
  return v;
}

std::vector<std::byte> connect_reply(std::uint32_t tid, std::uint64_t connection_id) {
  std::vector<std::byte> out;
  put<std::uint32_t>(out, 0);
  put(out, tid);
  put(out, connection_id);
  return out;
}

std::vector<std::byte> announce_reply(std::uint32_t tid, std::uint32_t interval,
                                      std::uint32_t leechers, std::uint32_t seeders,
                                      std::vector<std::byte> peers = {}) {
  std::vector<std::byte> out;
  put<std::uint32_t>(out, 1);
  put(out, tid);
  put(out, interval);
  put(out, leechers);
  put(out, seeders);
  out.insert(out.end(), peers.begin(), peers.end());
  return out;
}

std::vector<std::byte> error_reply(std::uint32_t tid, std::string_view message) {
  std::vector<std::byte> out;
  put<std::uint32_t>(out, 3);
  put(out, tid);
  for (char c : message)
    out.push_back(static_cast<std::byte>(c));
  return out;
}

std::vector<std::byte> peer(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                            std::uint8_t d, std::uint16_t port) {
  std::vector<std::byte> out{std::byte{a}, std::byte{b}, std::byte{c}, std::byte{d}};
  put(out, port);
  return out;
}

AnnounceRequest sample_request() {
  AnnounceRequest req;
  req.info_hash.fill(std::byte{0xaa});
  req.peer_id.fill(std::byte{'p'});
  req.port = 6881;
  req.downloaded = 1;
  req.left = 2;
  req.uploaded = 3;
  req.event = Event::started;
  return req;
}

TrackerTimeouts fast() {
  return {.connect = std::chrono::seconds(2),
          .read = std::chrono::seconds(2),
          .udp_response = std::chrono::milliseconds(200),
          .udp_attempts = 3};
}

using Handler = std::function<std::optional<std::vector<std::byte>>(
    std::span<const std::byte> packet, int index)>;

class FakeUdpTracker {
public:
  explicit FakeUdpTracker(Handler handler, bool v6 = false, std::uint16_t port = 0)
      : m_socket(m_ctx, udp_proto::endpoint(v6 ? asio::ip::make_address("::1")
                                         : asio::ip::make_address("127.0.0.1"),
                                      port)),
        m_handler(std::move(handler)) {
    timeval tv{.tv_sec = 0, .tv_usec = 100'000};
    ::setsockopt(m_socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    m_thread = std::thread([this] { serve(); });
  }

  ~FakeUdpTracker() {
    m_stop = true;
    m_thread.join();
  }

  std::uint16_t port() const { return m_socket.local_endpoint().port(); }

  std::string url() const {
    auto ep = m_socket.local_endpoint();
    return ep.address().is_v6()
               ? std::format("udp://[{}]:{}/announce", ep.address().to_string(), ep.port())
               : std::format("udp://{}:{}/announce", ep.address().to_string(), ep.port());
  }

  std::vector<std::vector<std::byte>> received() {
    std::lock_guard lock(m_mutex);
    return m_received;
  }

private:
  void serve() {
    for (int index = 0; !m_stop; ++index) {
      std::vector<std::byte> buf(2048);
      udp_proto::endpoint from;
      socklen_t from_len = static_cast<socklen_t>(from.capacity());
      const auto n = ::recvfrom(m_socket.native_handle(), buf.data(), buf.size(), 0,
                                from.data(), &from_len);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          continue;
        }
        return;
      }
      from.resize(from_len);
      buf.resize(static_cast<std::size_t>(n));
      boost::system::error_code ec;
      {
        std::lock_guard lock(m_mutex);
        m_received.push_back(buf);
      }
      if (auto reply = m_handler(buf, index)) {
        m_socket.send_to(asio::buffer(*reply), from, 0, ec);
      }
    }
  }

  asio::io_context m_ctx;
  udp_proto::socket m_socket;
  Handler m_handler;
  std::mutex m_mutex;
  std::vector<std::vector<std::byte>> m_received;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

Handler standard_tracker(std::uint64_t connection_id, std::vector<std::byte> peers) {
  return [=](std::span<const std::byte> p, int) -> std::optional<std::vector<std::byte>> {
    const auto action = get<std::uint32_t>(p, 8);
    if (p.size() == 16 && action == 0)
      return connect_reply(get<std::uint32_t>(p, 12), connection_id);
    if (p.size() == 98 && get<std::uint64_t>(p, 0) == connection_id)
      return announce_reply(get<std::uint32_t>(p, 12), 1800, 4, 7, peers);
    return std::nullopt;
  };
}

}

TEST(UdpTrackerUrl, ParsesHostPortAndIgnoresPath) {
  auto e = udp::parse_url("udp://tracker.example.org:1337/announce");
  ASSERT_TRUE(e) << e.error();
  EXPECT_EQ(e->host, "tracker.example.org");
  EXPECT_EQ(e->port, 1337);

  auto bare = udp::parse_url("udp://1.2.3.4:80");
  ASSERT_TRUE(bare);
  EXPECT_EQ(bare->host, "1.2.3.4");
}

TEST(UdpTrackerUrl, ParsesBracketedIpv6) {
  auto e = udp::parse_url("udp://[2001:db8::1]:6969/announce");
  ASSERT_TRUE(e) << e.error();
  EXPECT_EQ(e->host, "2001:db8::1");
  EXPECT_EQ(e->port, 6969);
}

TEST(UdpTrackerUrl, RejectsMalformed) {
  for (auto bad : {"http://x:1", "udp://host", "udp://host:", "udp://host:0",
                   "udp://host:99999", "udp://:80", "udp://[::1]80", "udp://host:8x"}) {
    EXPECT_FALSE(udp::parse_url(bad)) << bad;
  }
}

TEST(UdpTrackerCodec, ConnectRequestLayout) {
  auto p = udp::encode_connect(0xdeadbeef);
  EXPECT_EQ(get<std::uint64_t>(p, 0), 0x41727101980ull);
  EXPECT_EQ(get<std::uint32_t>(p, 8), 0u);
  EXPECT_EQ(get<std::uint32_t>(p, 12), 0xdeadbeefu);
}

TEST(UdpTrackerCodec, DecodesConnectReply) {
  auto r = udp::decode_connect(connect_reply(7, 0x1122334455667788), 7);
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(*r, 0x1122334455667788ull);
}

TEST(UdpTrackerCodec, ConnectReplyErrors) {
  EXPECT_FALSE(udp::decode_connect(connect_reply(7, 1), 8));
  auto short_reply = connect_reply(7, 1);
  short_reply.resize(12);
  EXPECT_FALSE(udp::decode_connect(short_reply, 7));
  auto err = udp::decode_connect(error_reply(7, "go away"), 7);
  ASSERT_FALSE(err);
  EXPECT_NE(err.error().find("go away"), std::string::npos);
  EXPECT_FALSE(udp::decode_connect(announce_reply(7, 1, 0, 0), 7));
}

TEST(UdpTrackerCodec, AnnounceRequestLayout) {
  AnnounceRequest req = sample_request();
  req.ip = "10.0.0.2";
  auto p = udp::encode_announce(0x0102030405060708, 99, req, 0xcafef00d);

  EXPECT_EQ(get<std::uint64_t>(p, 0), 0x0102030405060708ull);
  EXPECT_EQ(get<std::uint32_t>(p, 8), 1u);
  EXPECT_EQ(get<std::uint32_t>(p, 12), 99u);
  EXPECT_TRUE(std::ranges::all_of(std::span(p).subspan(16, 20),
                                  [](std::byte b) { return b == std::byte{0xaa}; }));
  EXPECT_TRUE(std::ranges::all_of(std::span(p).subspan(36, 20),
                                  [](std::byte b) { return b == std::byte{'p'}; }));
  EXPECT_EQ(get<std::uint64_t>(p, 56), 1u);
  EXPECT_EQ(get<std::uint64_t>(p, 64), 2u);
  EXPECT_EQ(get<std::uint64_t>(p, 72), 3u);
  EXPECT_EQ(get<std::uint32_t>(p, 80), 2u);
  EXPECT_EQ(get<std::uint32_t>(p, 84), 0x0a000002u);
  EXPECT_EQ(get<std::uint32_t>(p, 88), 0xcafef00du);
  EXPECT_EQ(get<std::uint32_t>(p, 92), 0xffffffffu);
  EXPECT_EQ(get<std::uint16_t>(p, 96), 6881u);
}

TEST(UdpTrackerCodec, EventCodesFollowBep15) {
  AnnounceRequest req;
  const std::pair<Event, std::uint32_t> cases[] = {
      {Event::none, 0}, {Event::completed, 1}, {Event::started, 2}, {Event::stopped, 3}};
  for (auto [event, code] : cases) {
    req.event = event;
    EXPECT_EQ(get<std::uint32_t>(udp::encode_announce(0, 0, req, 0), 80), code);
  }
}

TEST(UdpTrackerCodec, DecodesAnnounceReply) {
  auto peers = peer(127, 0, 0, 1, 6881);
  auto more = peer(10, 1, 2, 3, 51413);
  peers.insert(peers.end(), more.begin(), more.end());

  auto r = udp::decode_announce(announce_reply(5, 900, 3, 8, peers), 5, udp::AddressFamily::v4);
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_success());
  const auto &s = *r->as_success();
  EXPECT_EQ(s.interval, 900);
  EXPECT_EQ(s.incomplete, 3);
  EXPECT_EQ(s.complete, 8);
  ASSERT_EQ(s.peers.size(), 2u);
  EXPECT_EQ(s.peers[0].ip, "127.0.0.1");
  EXPECT_EQ(s.peers[0].port, 6881);
  EXPECT_EQ(s.peers[1].ip, "10.1.2.3");
  EXPECT_EQ(s.peers[1].port, 51413);
}

TEST(UdpTrackerCodec, DecodesIpv6Peers) {
  std::vector<std::byte> v6(18, std::byte{0});
  v6[15] = std::byte{1};
  v6[16] = std::byte{0x1a};
  v6[17] = std::byte{0xe1};
  auto r = udp::decode_announce(announce_reply(5, 60, 0, 0, v6), 5, udp::AddressFamily::v6);
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].ip, "0:0:0:0:0:0:0:1");
  EXPECT_EQ(r->as_success()->peers[0].port, 6881);
}

TEST(UdpTrackerCodec, ErrorActionBecomesFailureResponse) {
  auto r = udp::decode_announce(error_reply(5, "unregistered torrent"), 5,
                                udp::AddressFamily::v4);
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_failure());
  EXPECT_EQ(r->as_failure()->failure_reason, "unregistered torrent");
}

TEST(UdpTrackerCodec, RejectsMalformedAnnounceReply) {
  EXPECT_FALSE(udp::decode_announce(announce_reply(5, 1, 0, 0), 6, udp::AddressFamily::v4));
  auto truncated = announce_reply(5, 1, 0, 0);
  truncated.resize(19);
  EXPECT_FALSE(udp::decode_announce(truncated, 5, udp::AddressFamily::v4));
  auto odd = announce_reply(5, 1, 0, 0, {std::byte{1}, std::byte{2}, std::byte{3}});
  EXPECT_FALSE(udp::decode_announce(odd, 5, udp::AddressFamily::v4));
  EXPECT_FALSE(udp::decode_announce(connect_reply(5, 1), 5, udp::AddressFamily::v4));
}

TEST(UdpTrackerClient, AnnouncesThroughConnectHandshake) {
  FakeUdpTracker tracker(standard_tracker(0xabcdef, peer(192, 168, 1, 9, 7000)));
  TrackerClient client(tracker.url(), fast());

  auto r = client.announce(sample_request());
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_success());
  EXPECT_EQ(r->as_success()->interval, 1800);
  EXPECT_EQ(r->as_success()->complete, 7);
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].ip, "192.168.1.9");
  EXPECT_EQ(r->as_success()->peers[0].port, 7000);

  const auto received = tracker.received();
  ASSERT_EQ(received.size(), 2u);
  EXPECT_EQ(received[0].size(), 16u);
  EXPECT_EQ(received[1].size(), 98u);
}

TEST(UdpTrackerClient, WorksOverIpv6) {
  FakeUdpTracker tracker(standard_tracker(42, {}), true);
  TrackerClient client(tracker.url(), fast());
  auto r = client.announce(sample_request());
  ASSERT_TRUE(r) << r.error();
  EXPECT_TRUE(r->is_success());
}

TEST(UdpTrackerClient, RetriesWhenPacketsAreLost) {
  auto inner = standard_tracker(1, {});
  FakeUdpTracker tracker([&](std::span<const std::byte> p, int index) {
    return index == 0 || index == 2 ? std::nullopt : inner(p, index);
  });
  TrackerClient client(tracker.url(), fast());

  auto r = client.announce(sample_request());
  ASSERT_TRUE(r) << r.error();
  EXPECT_TRUE(r->is_success());
  EXPECT_EQ(tracker.received().size(), 4u);
}

TEST(UdpTrackerClient, IgnoresRepliesWithWrongTransactionId) {
  auto inner = standard_tracker(1, {});
  FakeUdpTracker tracker([&](std::span<const std::byte> p, int index)
                             -> std::optional<std::vector<std::byte>> {
    if (index == 0)
      return connect_reply(get<std::uint32_t>(p, 12) + 1, 999);
    return inner(p, index);
  });
  TrackerClient client(tracker.url(), fast());

  auto r = client.announce(sample_request());
  ASSERT_TRUE(r) << r.error();
  EXPECT_TRUE(r->is_success());
}

TEST(UdpTrackerClient, ReportsTrackerError) {
  FakeUdpTracker tracker([](std::span<const std::byte> p, int)
                             -> std::optional<std::vector<std::byte>> {
    if (get<std::uint32_t>(p, 8) == 0 && p.size() == 16)
      return connect_reply(get<std::uint32_t>(p, 12), 5);
    return error_reply(get<std::uint32_t>(p, 12), "torrent not registered");
  });
  TrackerClient client(tracker.url(), fast());

  auto r = client.announce(sample_request());
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_failure());
  EXPECT_EQ(r->as_failure()->failure_reason, "torrent not registered");
}

TEST(UdpTrackerClient, GivesUpWhenTrackerNeverAnswers) {
  FakeUdpTracker tracker([](std::span<const std::byte>, int) { return std::nullopt; });
  TrackerClient client(tracker.url(), fast());

  const auto start = std::chrono::steady_clock::now();
  auto r = client.announce(sample_request());
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(r);
  EXPECT_EQ(tracker.received().size(), 3u);
  EXPECT_LT(elapsed, std::chrono::seconds(2));
}

TEST(UdpTrackerClient, ReportsUnreachableTracker) {
  std::uint16_t port;
  {
    asio::io_context ctx;
    udp_proto::socket s(ctx, udp_proto::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    port = s.local_endpoint().port();
  }
  TrackerClient client(std::format("udp://127.0.0.1:{}/announce", port), fast());
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(client.announce(sample_request()));
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}

TEST(TrackerClient, RejectsUnsupportedScheme) {
  TrackerClient client("wss://tracker.example.org/announce");
  auto r = client.announce(sample_request());
  ASSERT_FALSE(r);
  EXPECT_NE(r.error().find("unsupported"), std::string::npos);
}

namespace {

std::vector<std::byte> peer6(std::uint8_t last, std::uint16_t port) {
  std::vector<std::byte> out(16, std::byte{0});
  out[0] = std::byte{0x20};
  out[1] = std::byte{0x01};
  out[15] = std::byte{last};
  put(out, port);
  return out;
}

const std::vector<std::string> both_families = {"127.0.0.1", "::1"};

}

TEST(UdpTrackerDualStack, MergesPeersFromIpv4AndIpv6Announces) {
  FakeUdpTracker v4(standard_tracker(1, peer(10, 0, 0, 1, 1000)));
  FakeUdpTracker v6(standard_tracker(2, peer6(9, 2000)), true, v4.port());

  auto r = udp::announce_addresses(both_families, v4.port(), sample_request(), fast(), 0);
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_success());
  const auto &peers = r->as_success()->peers;
  ASSERT_EQ(peers.size(), 2u);
  EXPECT_EQ(peers[0].ip, "10.0.0.1");
  EXPECT_EQ(peers[0].port, 1000);
  EXPECT_EQ(peers[1].ip, "2001:0:0:0:0:0:0:9");
  EXPECT_EQ(peers[1].port, 2000);
}

TEST(UdpTrackerDualStack, KeepsIpv4PeersWhenIpv6TrackerIsSilent) {
  FakeUdpTracker v4(standard_tracker(1, peer(10, 0, 0, 1, 1000)));
  FakeUdpTracker v6([](std::span<const std::byte>, int) { return std::nullopt; }, true,
                    v4.port());

  auto r = udp::announce_addresses(both_families, v4.port(), sample_request(), fast(), 0);
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].ip, "10.0.0.1");
}

TEST(UdpTrackerDualStack, FallsBackToIpv6WhenIpv4TrackerIsSilent) {
  FakeUdpTracker v4([](std::span<const std::byte>, int) { return std::nullopt; });
  FakeUdpTracker v6(standard_tracker(2, peer6(9, 2000)), true, v4.port());

  auto r = udp::announce_addresses(both_families, v4.port(), sample_request(), fast(), 0);
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].port, 2000);
}

TEST(UdpTrackerDualStack, SuccessWinsOverFailureResponse) {
  FakeUdpTracker v4([](std::span<const std::byte> p, int) -> std::optional<std::vector<std::byte>> {
    if (p.size() == 16)
      return connect_reply(get<std::uint32_t>(p, 12), 1);
    return error_reply(get<std::uint32_t>(p, 12), "busy");
  });
  FakeUdpTracker v6(standard_tracker(2, peer6(9, 2000)), true, v4.port());

  auto r = udp::announce_addresses(both_families, v4.port(), sample_request(), fast(), 0);
  ASSERT_TRUE(r) << r.error();
  EXPECT_TRUE(r->is_success());
}

TEST(UdpTrackerDualStack, ReportsBothErrorsWhenEverythingFails) {
  FakeUdpTracker v4([](std::span<const std::byte>, int) { return std::nullopt; });
  FakeUdpTracker v6([](std::span<const std::byte>, int) { return std::nullopt; }, true,
                    v4.port());

  auto r = udp::announce_addresses(both_families, v4.port(), sample_request(), fast(), 0);
  ASSERT_FALSE(r);
  EXPECT_NE(r.error().find("127.0.0.1"), std::string::npos) << r.error();
  EXPECT_NE(r.error().find("::1"), std::string::npos) << r.error();
}

TEST(UdpTrackerDualStack, RemovesDuplicatePeers) {
  auto twice = peer(10, 0, 0, 1, 1000);
  auto again = peer(10, 0, 0, 1, 1000);
  twice.insert(twice.end(), again.begin(), again.end());
  FakeUdpTracker v4(standard_tracker(1, twice));

  const std::vector<std::string> only_v4 = {"127.0.0.1"};
  auto r = udp::announce_addresses(only_v4, v4.port(), sample_request(), fast(), 0);
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(r->as_success()->peers.size(), 1u);
}
