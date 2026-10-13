#include <bencode/bencode.hxx>
#include <gtest/gtest.h>
#include <span>
#include <string_view>

#include <bencode/decode.hxx>

namespace {
std::span<const std::byte> as_bytes(std::string_view str) {
  return {reinterpret_cast<const std::byte *>(str.data()), str.size()};
}
}

// ==========================================
// DECODER TESTS
// ==========================================

TEST(BencodeDecoderTest, ParsesStrings) {
  auto res = bencode::decode(as_bytes("4:spam"));
  ASSERT_TRUE(res.has_value());

  auto str_val = std::get<bencode::ByteString>(res.value().data);
  EXPECT_EQ(str_val, "spam");
  EXPECT_EQ(res.value().start, 0);
  EXPECT_EQ(res.value().end, 5);

  auto empty_res = bencode::decode(as_bytes("0:"));
  ASSERT_TRUE(empty_res.has_value());
  EXPECT_EQ(std::get<std::string>(empty_res.value().data), "");
  EXPECT_EQ(empty_res.value().start, 0);
  EXPECT_EQ(empty_res.value().end, 1);
}

TEST(BencodeDecoderTest, ParsesIntegers) {
  auto res = bencode::decode(as_bytes("i42e"));
  ASSERT_TRUE(res.has_value());

  auto int_val = std::get<bencode::Integer>(res.value().data);
  EXPECT_EQ(int_val, 42);
  EXPECT_EQ(res.value().start, 0);
  EXPECT_EQ(res.value().end, 3);

  auto neg_res = bencode::decode(as_bytes("i-13e"));
  ASSERT_TRUE(neg_res.has_value());
  EXPECT_EQ(std::get<bencode::Integer>(neg_res.value().data), -13);
  EXPECT_EQ(neg_res.value().start, 0);
  EXPECT_EQ(neg_res.value().end, 4);

  auto zero_res = bencode::decode(as_bytes("i0e"));
  ASSERT_TRUE(zero_res.has_value());
  EXPECT_EQ(std::get<bencode::Integer>(zero_res.value().data), 0);
  EXPECT_EQ(zero_res.value().start, 0);
  EXPECT_EQ(zero_res.value().end, 2);
}

TEST(BencodeDecoderTest, ParsesLists) {
  auto res = bencode::decode(as_bytes("l4:spaml6:nestedei42ee"));
  ASSERT_TRUE(res.has_value());

  auto list = std::get<bencode::List>(res.value().data);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<bencode::ByteString>(list[0].data), "spam");
  EXPECT_EQ(std::get<bencode::Integer>((list)[2].data), 42);
  EXPECT_EQ(res.value().start, 0);
  EXPECT_EQ(res.value().end, 21);
}

TEST(BencodeDecoderTest, ParsesDictionaries) {
  // {"bar": "spam", "foo": 42}
  auto res = bencode::decode(as_bytes("d3:bar4:spam3:fooi42ee"));
  ASSERT_TRUE(res.has_value());

  auto dict = std::get<bencode::Dict>(res.value().data);

  ASSERT_TRUE(dict.contains("foo"));
  EXPECT_EQ(std::get<bencode::Integer>(dict["foo"].data),  42);
  EXPECT_EQ(res.value().start, 0);
  EXPECT_EQ(res.value().end, 21);
}

TEST(BencodeDecoderTest, RejectsMalformedData) {
  EXPECT_FALSE(bencode::decode(as_bytes("i42")).has_value());

  EXPECT_FALSE(bencode::decode(as_bytes("10:spam")).has_value());

  EXPECT_FALSE(bencode::decode(as_bytes("d3:fooe")).has_value());
}

TEST(BencodeDecoderTest, NestedValuePositionsPointIntoInput) {
  const std::string input = "d4:infod3:fooi1eee";
  auto bytes = as_bytes(input);

  auto res = bencode::decode(bytes);
  ASSERT_TRUE(res.has_value());

  const auto &root = std::get<bencode::Dict>(res.value().data);
  const auto &info = root.at("info");

  EXPECT_EQ(info.start, 7);
  EXPECT_EQ(info.end, 16);

  auto slice = bytes.subspan(info.start, info.end - info.start + 1);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(slice.data()), slice.size()),
            "d3:fooi1ee");
}
