#include <download/peer/wire/message.hxx>

#include <gtest/gtest.h>

using namespace download::peer::wire;

namespace {

constexpr std::size_t num_pieces = 10;

std::vector<std::byte> bytes(std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (int v : values)
    out.push_back(static_cast<std::byte>(v));
  return out;
}

PeerMessage round_trip(const PeerMessage &m) {
  auto frame = encode(m);
  auto decoded = decode(std::span(frame).subspan(4), num_pieces);
  EXPECT_TRUE(decoded.has_value()) << decoded.error().str();
  return *decoded;
}

}

TEST(WireMessage, KeepAliveIsFourZeroBytes) {
  EXPECT_EQ(encode(KeepAlive{}), bytes({0, 0, 0, 0}));
}

TEST(WireMessage, EncodesFrameWithLengthAndId) {
  EXPECT_EQ(encode(Interested{}), bytes({0, 0, 0, 1, 2}));
  EXPECT_EQ(encode(Have{0x01020304}), bytes({0, 0, 0, 5, 4, 1, 2, 3, 4}));
  EXPECT_EQ(encode(Request{1, 0x4000, 0x4000}),
            bytes({0, 0, 0, 13, 6, 0, 0, 0, 1, 0, 0, 0x40, 0, 0, 0, 0x40, 0}));
}

TEST(WireMessage, RoundTripsEveryMessageType) {
  std::vector<PeerMessage> messages = {
      Choke{},
      Unchoke{},
      Interested{},
      NotInterested{},
      Have{9},
      Bitfield{{true, false, true, true, false, false, false, false, true, true}},
      Request{3, 16384, 16384},
      Piece{3, 16384, bytes({0xde, 0xad, 0xbe, 0xef})},
      Cancel{3, 16384, 16384},
  };
  for (const auto &m : messages) {
    EXPECT_EQ(round_trip(m), m) << "message index " << m.index();
  }
}

TEST(WireMessage, PieceWithEmptyBlockIsValid) {
  EXPECT_EQ(round_trip(Piece{1, 0, {}}), PeerMessage(Piece{1, 0, {}}));
}

TEST(WireMessage, RejectsEmptyBody) {
  auto r = decode({}, num_pieces);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::bad_payload_length);
}

TEST(WireMessage, RejectsUnknownId) {
  auto body = bytes({20, 0, 1});
  auto r = decode(body, num_pieces);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::unknown_message_id);
}

TEST(WireMessage, RejectsWrongPayloadLengths) {
  for (auto body : {bytes({0, 1}),
                    bytes({4, 0, 0, 1}),
                    bytes({6, 0, 0, 0, 1, 0, 0}),
                    bytes({8, 0}),
                    bytes({7, 0, 0, 0, 1})}) {
    auto r = decode(body, num_pieces);
    ASSERT_FALSE(r) << "id " << std::to_integer<int>(body[0]);
    EXPECT_EQ(r.error().code, Errc::bad_payload_length);
  }
}

TEST(WireMessage, RejectsHaveOutOfRange) {
  auto body = bytes({4, 0, 0, 0, 10});
  auto r = decode(body, num_pieces);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::bad_piece_index);
}

TEST(WireBitfield, PacksMsbFirst) {
  std::vector<bool> bits{true, false, false, false, false, false, false, true, true};
  EXPECT_EQ(pack_bitfield(bits), bytes({0x81, 0x80}));
}

TEST(WireBitfield, UnpacksToExactlyNumPieces) {
  auto r = unpack_bitfield(bytes({0x81, 0x80}), 9);
  ASSERT_TRUE(r) << r.error().str();
  EXPECT_EQ(*r, (std::vector<bool>{true, false, false, false, false, false, false, true, true}));
}

TEST(WireBitfield, RejectsWrongByteCount) {
  auto r = unpack_bitfield(bytes({0xff}), 9);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::bad_bitfield);
}

TEST(WireBitfield, RejectsPaddingBits) {
  auto r = unpack_bitfield(bytes({0x00, 0x40}), 9);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, Errc::bad_bitfield);
}
