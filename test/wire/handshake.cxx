#include <download/peer/wire/handshake.hxx>

#include <gtest/gtest.h>

using namespace download::peer::wire;

namespace {
Handshake sample() {
  Handshake h;
  for (std::size_t i = 0; i < 20; ++i) {
    h.info_hash[i] = static_cast<std::byte>(i);
    h.peer_id[i] = static_cast<std::byte>(100 + i);
  }
  h.reserved[7] = std::byte{0x04};
  return h;
}
}

TEST(WireHandshake, EncodesStandardLayout) {
  auto bytes = sample().encode();
  ASSERT_EQ(bytes.size(), 68u);
  EXPECT_EQ(bytes[0], std::byte{19});
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(bytes.data() + 1), 19),
            "BitTorrent protocol");
  EXPECT_EQ(bytes[27], std::byte{0x04});
  EXPECT_EQ(bytes[28], std::byte{0});
  EXPECT_EQ(bytes[48], std::byte{100});
}

TEST(WireHandshake, RoundTrips) {
  auto bytes = sample().encode();
  auto decoded = Handshake::decode(bytes);
  ASSERT_TRUE(decoded) << decoded.error().str();
  EXPECT_EQ(*decoded, sample());
}

TEST(WireHandshake, RejectsOtherProtocol) {
  auto bytes = sample().encode();
  bytes[1] = std::byte{'X'};
  auto decoded = Handshake::decode(bytes);
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error().code, Errc::bad_protocol);
}

TEST(WireHandshake, RejectsWrongProtocolLength) {
  auto bytes = sample().encode();
  bytes[0] = std::byte{18};
  ASSERT_FALSE(Handshake::decode(bytes));
}
