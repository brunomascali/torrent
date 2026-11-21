#include <download/tracker/announce.hxx>

#include <bencode/encode.hxx>
#include <gtest/gtest.h>

using namespace download::tracker;

TEST(UrlEncode, KeepsUnreservedAndEncodesTheRest) {
  EXPECT_EQ(url_encode("aZ09-_.~"), "aZ09-_.~");
  EXPECT_EQ(url_encode(" /?&="), "%20%2F%3F%26%3D");
  std::array<std::byte, 2> raw{std::byte{0x00}, std::byte{0xff}};
  EXPECT_EQ(url_encode(raw), "%00%FF");
}

TEST(AnnounceRequest, QueryStringHasFieldsInOrder) {
  AnnounceRequest req;
  req.info_hash.fill(std::byte{'a'});
  req.peer_id.fill(std::byte{'b'});
  req.port = 6881;
  req.uploaded = 1;
  req.downloaded = 2;
  req.left = 3;

  EXPECT_EQ(req.query_string(),
            "info_hash=" + std::string(20, 'a') + "&peer_id=" + std::string(20, 'b') +
                "&port=6881&uploaded=1&downloaded=2&left=3&compact=1");
}

TEST(AnnounceRequest, IncludesEventAndIpWhenSet) {
  AnnounceRequest req;
  req.event = Event::completed;
  req.compact = false;
  req.ip = "1.2.3.4";
  auto q = req.query_string();
  EXPECT_EQ(q.find("compact"), std::string::npos);
  EXPECT_NE(q.find("&event=completed"), std::string::npos);
  EXPECT_NE(q.find("&ip=1.2.3.4"), std::string::npos);
}

TEST(AnnounceResponse, ParsesCompactPeers) {
  bencode::Dict d;
  d["interval"] = bencode::Integer{1800};
  d["peers"] = bencode::ByteString{std::string("\x7f\x00\x00\x01\x1a\xe1", 6)};
  auto r = AnnounceResponse::parse(bencode::encode(d));
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_success());
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].ip, "127.0.0.1");
  EXPECT_EQ(r->as_success()->peers[0].port, 6881);
}

TEST(AnnounceResponse, ParsesCompactIpv6Peers) {
  bencode::Dict d;
  d["interval"] = bencode::Integer{60};
  d["peers"] = bencode::ByteString{""};
  std::string v6(18, '\0');
  v6[15] = 1;
  v6[16] = '\x1a', v6[17] = '\xe1';
  d["peers6"] = bencode::ByteString{v6};
  auto r = AnnounceResponse::parse(bencode::encode(d));
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(r->as_success()->peers.size(), 1u);
  EXPECT_EQ(r->as_success()->peers[0].ip, "0:0:0:0:0:0:0:1");
  EXPECT_EQ(r->as_success()->peers[0].port, 6881);
}

TEST(AnnounceResponse, ParsesFailureReason) {
  bencode::Dict d;
  d["failure reason"] = bencode::ByteString{"nope"};
  auto r = AnnounceResponse::parse(bencode::encode(d));
  ASSERT_TRUE(r) << r.error();
  ASSERT_TRUE(r->is_failure());
  EXPECT_EQ(r->as_failure()->failure_reason, "nope");
}

TEST(AnnounceResponse, RejectsMissingInterval) {
  bencode::Dict d;
  d["peers"] = bencode::ByteString{""};
  EXPECT_FALSE(AnnounceResponse::parse(bencode::encode(d)));
}

TEST(AnnounceResponse, RejectsBadCompactLength) {
  bencode::Dict d;
  d["interval"] = bencode::Integer{60};
  d["peers"] = bencode::ByteString{"12345"};
  EXPECT_FALSE(AnnounceResponse::parse(bencode::encode(d)));
}

TEST(AnnounceResponse, RejectsNonBencode) {
  EXPECT_FALSE(AnnounceResponse::parse("<html>not a tracker</html>"));
}
