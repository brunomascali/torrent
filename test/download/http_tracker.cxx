#include <arpa/inet.h>
#include <bencode/encode.hxx>
#include <chrono>
#include <cstring>
#include <download/http_announce.hxx>
#include <download/tracker.hxx>
#include <gtest/gtest.h>
#include <httplib/httplib.h>
#include <thread>
#include <vector>

namespace {

std::string to_compact_peer(const std::string& ip_str, uint16_t port) {
  std::string compact;
  compact.resize(6);

  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str.c_str(), &addr) == 1) {
    std::memcpy(compact.data(), &addr, 4);
  }

  uint16_t network_port = htons(port);
  std::memcpy(compact.data() + 4, &network_port, 2);
  return compact;
}

std::string make_compact_tracker_response(
    int64_t interval,
    const std::vector<download::Peer>& peers,
    std::optional<int64_t> complete = std::nullopt,
    std::optional<int64_t> incomplete = std::nullopt) {
  bencode::Dict dict;
  dict["interval"] = bencode::Integer{interval};

  std::string compact_peers;
  for (const auto& peer : peers) {
    compact_peers += to_compact_peer(peer.ip, peer.port);
  }
  dict["peers"] = bencode::ByteString{compact_peers};

  if (complete) dict["complete"] = bencode::Integer{*complete};
  if (incomplete) dict["incomplete"] = bencode::Integer{*incomplete};

  return bencode::encode(dict);
}

std::string make_dict_tracker_response(
    int64_t interval,
    const std::vector<download::Peer>& peers,
    std::optional<int64_t> complete = std::nullopt,
    std::optional<int64_t> incomplete = std::nullopt) {
  bencode::Dict dict;
  dict["interval"] = bencode::Integer{interval};

  bencode::List peer_list;
  for (const auto& peer : peers) {
    bencode::Dict peer_dict;
    peer_dict["ip"] = bencode::ByteString{peer.ip};
    peer_dict["port"] = bencode::Integer{peer.port};
    if (peer.peer_id.has_value()) {
      peer_dict["peer id"] = bencode::ByteString{*peer.peer_id};
    }
    peer_list.push_back(peer_dict);
  }
  dict["peers"] = std::move(peer_list);

  if (complete) dict["complete"] = bencode::Integer{*complete};
  if (incomplete) dict["incomplete"] = bencode::Integer{*incomplete};

  return bencode::encode(dict);
}

std::string make_failure_tracker_response(const std::string& reason) {
  bencode::Dict dict;
  dict["failure reason"] = bencode::ByteString{reason};
  return bencode::encode(dict);
}

} // namespace

class MockHttpTrackerTest : public ::testing::Test {
protected:
  httplib::Server svr;
  std::thread server_thread;
  int port = 0;
  std::string base_url;

  void SetUp() override {
    // 1. Compact peers route
    svr.Get("/announce_compact", [](const httplib::Request&, httplib::Response& res) {
      std::vector<download::Peer> peers = {
        {.peer_id = std::nullopt, .ip = "127.0.0.1", .port = 6881},
        {.peer_id = std::nullopt, .ip = "192.168.1.50", .port = 8080}
      };
      res.status = 200;
      res.set_content(make_compact_tracker_response(1800, peers, 10, 2), "text/plain");
    });

    // 2. Dictionary list peers route
    svr.Get("/announce_dict", [](const httplib::Request&, httplib::Response& res) {
      std::vector<download::Peer> peers = {
        {.peer_id = "-TR2940-peer12345678", .ip = "127.0.0.1", .port = 6881},
        {.peer_id = "-TR2940-peer87654321", .ip = "10.0.0.1", .port = 51413}
      };
      res.status = 200;
      res.set_content(make_dict_tracker_response(900, peers, 5, 1), "text/plain");
    });

    // 3. Failure route
    svr.Get("/announce_fail", [](const httplib::Request&, httplib::Response& res) {
      res.status = 200;
      res.set_content(make_failure_tracker_response("unregistered torrent"), "text/plain");
    });

    port = svr.bind_to_any_port("127.0.0.1");
    base_url = std::format("http://127.0.0.1:{}", port);

    server_thread = std::thread([this]() {
      svr.listen_after_bind();
    });

    while (!svr.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void TearDown() override {
    svr.stop();
    if (server_thread.joinable()) {
      server_thread.join();
    }
  }
};

TEST_F(MockHttpTrackerTest, HandlesCompactRepresentation) {
  download::Tracker tracker = download::Tracker(std::format("{}/announce_compact", base_url));
  download::HttpAnnounceRequest req{
    .info_hash = std::string(20, 'a'),
    .peer_id = "-TR2940-123456789012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = 0,
    .downloaded = 0,
    .left = 1000,
  };

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_success());

  const download::HttpAnnounceResponseSuccess* success = res->as_success();
  EXPECT_EQ(success->interval, 1800);
  ASSERT_EQ(success->peers.size(), 2);
  EXPECT_EQ(success->peers[0].ip, "127.0.0.1");
  EXPECT_EQ(success->peers[0].port, 6881);
  EXPECT_EQ(success->peers[1].ip, "192.168.1.50");
  EXPECT_EQ(success->peers[1].port, 8080);
  EXPECT_EQ(success->complete, 10);
  EXPECT_EQ(success->incomplete, 2);
}

TEST_F(MockHttpTrackerTest, HandlesDictionaryRepresentation) {
  download::Tracker tracker = download::Tracker(std::format("{}/announce_dict", base_url));
  download::HttpAnnounceRequest req{
    .info_hash = std::string(20, 'a'),
    .peer_id = "-TR2940-123456789012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = 0,
    .downloaded = 0,
    .left = 1000,
  };

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_success());

  const download::HttpAnnounceResponseSuccess* success = res->as_success();
  EXPECT_EQ(success->interval, 900);
  ASSERT_EQ(success->peers.size(), 2);
  EXPECT_EQ(success->peers[0].peer_id, "-TR2940-peer12345678");
  EXPECT_EQ(success->peers[0].ip, "127.0.0.1");
  EXPECT_EQ(success->peers[0].port, 6881);
  EXPECT_EQ(success->peers[1].peer_id, "-TR2940-peer87654321");
  EXPECT_EQ(success->peers[1].ip, "10.0.0.1");
  EXPECT_EQ(success->peers[1].port, 51413);
  EXPECT_EQ(success->complete, 5);
  EXPECT_EQ(success->incomplete, 1);
}

TEST_F(MockHttpTrackerTest, HandlesFailureResponse) {
  download::Tracker tracker = download::Tracker(std::format("{}/announce_fail", base_url));
  download::HttpAnnounceRequest req{
    .info_hash = std::string(20, 'a'),
    .peer_id = "-TR2940-123456789012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = 0,
    .downloaded = 0,
    .left = 1000,
  };

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_failure());

  const download::HttpAnnounceResponseFailure* failure = res->as_failure();
  EXPECT_EQ(failure->failure_reason, "unregistered torrent");
}
