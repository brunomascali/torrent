#include <bencode/bencode.hxx>
#include <gtest/gtest.h>
#include <span>
#include <string_view>

#include <bencode/decode.hxx>

std::span<const std::byte> as_bytes(std::string_view str) {
  return {reinterpret_cast<const std::byte *>(str.data()), str.size()};
}

// ==========================================
// DECODER TESTS
// ==========================================

TEST(BencodeDecoderTest, ParsesStrings) {
  auto res = bencode::decode(as_bytes("4:spam"));
  ASSERT_TRUE(res.has_value());

  auto str_val = std::get<bencode::ByteString>(res.value().data);
  EXPECT_EQ(str_val, "spam");

  auto empty_res = bencode::decode(as_bytes("0:"));
  ASSERT_TRUE(empty_res.has_value());
  EXPECT_EQ(std::get<std::string>(empty_res.value().data), "");
}

TEST(BencodeDecoderTest, ParsesIntegers) {
  auto res = bencode::decode(as_bytes("i42e"));
  ASSERT_TRUE(res.has_value());

  auto int_val = std::get<bencode::Integer>(res.value().data);
  EXPECT_EQ(int_val, 42);

  auto neg_res = bencode::decode(as_bytes("i-13e"));
  ASSERT_TRUE(neg_res.has_value());
  EXPECT_EQ(std::get<bencode::Integer>(neg_res.value().data), -13);

  auto zero_res = bencode::decode(as_bytes("i0e"));
  ASSERT_TRUE(zero_res.has_value());
  EXPECT_EQ(std::get<bencode::Integer>(zero_res.value().data), 0);
}

TEST(BencodeDecoderTest, ParsesLists) {
  auto res = bencode::decode(as_bytes("l4:spaml6:nestedei42ee"));
  ASSERT_TRUE(res.has_value());

  auto list = std::get<bencode::List>(res.value().data);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<bencode::ByteString>(list[0].data), "spam");
  EXPECT_EQ(std::get<bencode::Integer>((list)[2].data), 42);
}

// TEST(BencodeDecoderTest, ParsesDictionaries) {
//   // {"bar": "spam", "foo": 42}
//   auto res = bencode::Decoder::decode(as_bytes("d3:bar4:spam3:fooi42ee"));
//   ASSERT_TRUE(res.has_value());

//   auto *dict_val = boost::get<bencode::Dict>(&res.value());

//   auto foo_val = dict_val->get("foo");
//   ASSERT_TRUE(foo_val.has_value());
//   EXPECT_EQ(*boost::get<int64_t>(&foo_val.value()), 42);
// }

TEST(BencodeDecoderTest, RejectsMalformedData) {
  EXPECT_FALSE(bencode::decode(as_bytes("i42")).has_value());

  EXPECT_FALSE(bencode::decode(as_bytes("10:spam")).has_value());

  EXPECT_FALSE(bencode::decode(as_bytes("d3:fooe")).has_value());
}
