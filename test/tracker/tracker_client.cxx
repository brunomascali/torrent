#include <arpa/inet.h>
#include <bencode/encode.hxx>
#include <chrono>
#include <cstring>
#include <download/tracker/tracker_client.hxx>
#include <gtest/gtest.h>
#include <httplib/httplib.h>
#include <thread>
#include <vector>

using namespace download::tracker;

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
    const std::vector<download::peer::Peer>& peers,
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
    const std::vector<download::peer::Peer>& peers,
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

AnnounceRequest sample_request() {
  AnnounceRequest req;
  req.info_hash.fill(std::byte{'a'});
  std::string_view id = "-TR2940-123456789012";
  for (std::size_t i = 0; i < 20; ++i)
    req.peer_id[i] = static_cast<std::byte>(id[i]);
  req.port = 6881;
  req.left = 1000;
  return req;
}

}

class MockHttpTrackerTest : public ::testing::Test {
protected:
  httplib::Server svr;
  std::thread server_thread;
  int port = 0;
  std::string base_url;
  httplib::Params last_params;

  void SetUp() override {
    svr.Get("/announce_compact", [](const httplib::Request&, httplib::Response& res) {
      std::vector<download::peer::Peer> peers = {
        {.peer_id = std::nullopt, .ip = "127.0.0.1", .port = 6881},
        {.peer_id = std::nullopt, .ip = "192.168.1.50", .port = 8080}
      };
      res.status = 200;
      res.set_content(make_compact_tracker_response(1800, peers, 10, 2), "text/plain");
    });

    svr.Get("/announce_dict", [](const httplib::Request&, httplib::Response& res) {
      std::vector<download::peer::Peer> peers = {
        {.peer_id = "-TR2940-peer12345678", .ip = "127.0.0.1", .port = 6881},
        {.peer_id = "-TR2940-peer87654321", .ip = "10.0.0.1", .port = 51413}
      };
      res.status = 200;
      res.set_content(make_dict_tracker_response(900, peers, 5, 1), "text/plain");
    });

    svr.Get("/announce_fail", [](const httplib::Request&, httplib::Response& res) {
      res.status = 200;
      res.set_content(make_failure_tracker_response("unregistered torrent"), "text/plain");
    });

    svr.Get("/announce_echo", [this](const httplib::Request& req, httplib::Response& res) {
      last_params = req.params;
      res.status = 200;
      res.set_content(make_compact_tracker_response(60, {}), "text/plain");
    });

    svr.Get("/announce_500", [](const httplib::Request&, httplib::Response& res) {
      res.status = 500;
      res.set_content("boom", "text/plain");
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
  TrackerClient tracker(std::format("{}/announce_compact", base_url));
  AnnounceRequest req = sample_request();

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_success());

  const AnnounceSuccess* success = res->as_success();
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
  TrackerClient tracker(std::format("{}/announce_dict", base_url));
  AnnounceRequest req = sample_request();

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_success());

  const AnnounceSuccess* success = res->as_success();
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
  TrackerClient tracker(std::format("{}/announce_fail", base_url));
  AnnounceRequest req = sample_request();

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value());
  ASSERT_TRUE(res->is_failure());

  const AnnounceFailure* failure = res->as_failure();
  EXPECT_EQ(failure->failure_reason, "unregistered torrent");
}

TEST_F(MockHttpTrackerTest, SendsAllRequiredParameters) {
  TrackerClient tracker(std::format("{}/announce_echo", base_url));
  AnnounceRequest req = sample_request();
  req.info_hash[0] = std::byte{0xff};
  req.event = Event::started;

  auto res = tracker.announce(req);
  ASSERT_TRUE(res.has_value()) << res.error();

  auto param = [&](const std::string& key) {
    auto it = last_params.find(key);
    return it == last_params.end() ? std::string("<missing>") : it->second;
  };
  EXPECT_EQ(param("info_hash"), "\xff" + std::string(19, 'a'));
  EXPECT_EQ(param("peer_id"), "-TR2940-123456789012");
  EXPECT_EQ(param("port"), "6881");
  EXPECT_EQ(param("left"), "1000");
  EXPECT_EQ(param("compact"), "1");
  EXPECT_EQ(param("event"), "started");
}

TEST_F(MockHttpTrackerTest, KeepsExistingQueryInAnnounceUrl) {
  TrackerClient tracker(std::format("{}/announce_echo?passkey=secret", base_url));
  auto res = tracker.announce(sample_request());
  ASSERT_TRUE(res.has_value()) << res.error();
  EXPECT_EQ(last_params.find("passkey")->second, "secret");
  EXPECT_EQ(last_params.find("port")->second, "6881");
}

TEST_F(MockHttpTrackerTest, ReportsHttpErrorStatus) {
  TrackerClient tracker(std::format("{}/announce_500", base_url));
  auto res = tracker.announce(sample_request());
  ASSERT_FALSE(res.has_value());
  EXPECT_NE(res.error().find("500"), std::string::npos);
}

TEST(TrackerClient, ReportsUnreachableTrackerWithinTimeout) {
  TrackerClient tracker("http://127.0.0.1:1/announce",
                        {.connect = std::chrono::milliseconds(300),
                         .read = std::chrono::milliseconds(300)});
  const auto start = std::chrono::steady_clock::now();
  auto res = tracker.announce(sample_request());
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(res.has_value());
  EXPECT_LT(elapsed, std::chrono::seconds(5));
}
