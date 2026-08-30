#include <bencode/bencode.hxx>
#include <gtest/gtest.h>
#include <span>
#include <string_view>

#include <bencode/decode.hxx>
#include <bencode/encode.hxx>

namespace {
std::span<const std::byte> as_bytes(std::string_view str) {
  return {reinterpret_cast<const std::byte *>(str.data()), str.size()};
}
}

// ==========================================
// ENCODER TESTS
// ==========================================

TEST(BencodeEncoderTest, EncodesStrings) {
  bencode::Value val{bencode::ByteString{"spam"}};
  EXPECT_EQ(bencode::encode(val), "4:spam");

  bencode::Value empty_val{bencode::ByteString{""}};
  EXPECT_EQ(bencode::encode(empty_val), "0:");
}

TEST(BencodeEncoderTest, EncodesIntegers) {
  bencode::Value val{bencode::Integer{42}};
  EXPECT_EQ(bencode::encode(val), "i42e");

  bencode::Value neg_val{bencode::Integer{-13}};
  EXPECT_EQ(bencode::encode(neg_val), "i-13e");

  bencode::Value zero_val{bencode::Integer{0}};
  EXPECT_EQ(bencode::encode(zero_val), "i0e");
}

TEST(BencodeEncoderTest, EncodesLists) {
  bencode::List list{
      bencode::Value{bencode::ByteString{"spam"}},
      bencode::Value{bencode::List{
                         bencode::Value{bencode::ByteString{"nested"}},
                     }},
      bencode::Value{bencode::Integer{42}},
  };
  bencode::Value val{list};

  EXPECT_EQ(bencode::encode(val), "l4:spaml6:nestedei42ee");
}

TEST(BencodeEncoderTest, EncodesEmptyList) {
  bencode::Value val{bencode::List{}};
  EXPECT_EQ(bencode::encode(val), "le");
}

TEST(BencodeEncoderTest, EncodesDictionaries) {
  // {"bar": "spam", "foo": 42}
  bencode::Dict dict{
      {"bar", bencode::Value{bencode::ByteString{"spam"}}},
      {"foo", bencode::Value{bencode::Integer{42}}},
  };
  bencode::Value val{dict};

  EXPECT_EQ(bencode::encode(val), "d3:bar4:spam3:fooi42ee");
}

TEST(BencodeEncoderTest, EncodesDictionaryKeysInSortedOrder) {
  // Insertion order deliberately not sorted; bencode requires
  // dict keys to appear in raw byte order regardless of input order.
  bencode::Dict dict{
      {"zebra", bencode::Value{bencode::Integer{1}}},
      {"apple", bencode::Value{bencode::Integer{2}}},
      {"mango", bencode::Value{bencode::Integer{3}}},
  };
  bencode::Value val{dict};

  EXPECT_EQ(bencode::encode(val), "d5:applei2e5:mangoi3e5:zebrai1ee");
}

TEST(BencodeEncoderTest, EncodesEmptyDict) {
  bencode::Value val{bencode::Dict{}};
  EXPECT_EQ(bencode::encode(val), "de");
}

TEST(BencodeEncoderTest, EncodesNestedListsAndDicts) {
  // {"files": [{"length": 100}, {"length": 200}]}
  bencode::List files{
      bencode::Value{bencode::Dict{
                         {"length", bencode::Value{bencode::Integer{100}}},
                     }},
      bencode::Value{bencode::Dict{
                         {"length", bencode::Value{bencode::Integer{200}}},
                     }},
  };
  bencode::Dict dict{{"files", bencode::Value{files}}};
  bencode::Value val{dict};

  EXPECT_EQ(bencode::encode(val), "d5:filesld6:lengthi100eed6:lengthi200eeee");
}

TEST(BencodeEncoderTest, RoundTripsThroughDecode) {
  auto original = as_bytes("d3:bar4:spam3:fooi42ee");
  auto decoded = bencode::decode(original);
  ASSERT_TRUE(decoded.has_value());

  EXPECT_EQ(bencode::encode(decoded.value()), "d3:bar4:spam3:fooi42ee");
}