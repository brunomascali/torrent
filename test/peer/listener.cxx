#include <download/listener.hxx>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using download::Listener;

namespace {

std::uint16_t free_port() {
  asio::io_context ctx;
  tcp::acceptor a(ctx, tcp::endpoint(tcp::v4(), 0));
  return a.local_endpoint().port();
}

}

TEST(Listener, AcceptsIpv4AndIpv6OnTheSamePort) {
  asio::io_context ctx;
  auto listener = Listener::open(ctx.get_executor(), 0, 0);
  ASSERT_TRUE(listener) << listener.error();
  const auto port = listener->port();
  EXPECT_NE(port, 0);
  ASSERT_TRUE(listener->accepts_ipv6());

  std::vector<std::string> remotes;
  Listener::start(std::move(*listener), [&](io::TcpConnection c) {
    remotes.push_back(c.remote_endpoint().address().to_string());
    if (remotes.size() == 2)
      ctx.stop();
  });

  tcp::socket v4(ctx), v6(ctx);
  v4.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
  v6.connect(tcp::endpoint(asio::ip::make_address("::1"), port));
  ctx.run_for(std::chrono::seconds(2));

  ASSERT_EQ(remotes.size(), 2u);
  std::ranges::sort(remotes);
  EXPECT_EQ(remotes[0], "127.0.0.1");
  EXPECT_EQ(remotes[1], "::1");
}

TEST(Listener, SkipsPortsThatAreInUse) {
  asio::io_context ctx;
  std::uint16_t base = 0;
  std::optional<tcp::acceptor> busy;
  for (int attempt = 0; attempt < 20 && !busy; ++attempt) {
    base = free_port();
    if (base == 65535)
      continue;
    boost::system::error_code ec;
    tcp::acceptor a(ctx);
    a.open(tcp::v4());
    a.bind(tcp::endpoint(tcp::v4(), base), ec);
    if (!ec) {
      a.listen();
      busy.emplace(std::move(a));
    }
  }
  ASSERT_TRUE(busy);

  auto listener = Listener::open(ctx.get_executor(), base, static_cast<std::uint16_t>(base + 1));
  if (!listener)
    GTEST_SKIP() << "port " << base + 1 << " also busy";
  EXPECT_EQ(listener->port(), base + 1);
}

TEST(Listener, FailsWhenNoPortInRangeIsFree) {
  asio::io_context ctx;
  tcp::acceptor busy(ctx, tcp::endpoint(tcp::v4(), 0));
  const auto port = busy.local_endpoint().port();
  auto listener = Listener::open(ctx.get_executor(), port, port);
  EXPECT_FALSE(listener);
}
