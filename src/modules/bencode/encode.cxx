#include <bencode/bencode.hxx>
#include <bencode/encode.hxx>
#include <variant>
#include <format>

namespace bencode {

std::string encode(const bencode::Value &bencode) {
  if (std::holds_alternative<bencode::Integer>(bencode.data)) {
    return std::format("i{}e", std::get<bencode::Integer>(bencode.data));
  }

  if (std::holds_alternative<bencode::ByteString>(bencode.data)) {
    const auto &str = std::get<bencode::ByteString>(bencode.data);
    return std::format("{}:{}", str.size(), str);
  }

  if (std::holds_alternative<bencode::List>(bencode.data)) {
    const auto &list = std::get<bencode::List>(bencode.data);
    std::string encoded{};
    for (const auto &item : list) {
      encoded += encode(item);
    }
    return std::format("l{}e", encoded);
  }

  if (std::holds_alternative<bencode::Dict>(bencode.data)) {
    const auto &dict = std::get<bencode::Dict>(bencode.data);
    std::string encoded{};
    for (const auto &[k, v] : dict) {
      encoded += std::format("{}:{}", k.size(), k);
      encoded += encode(v);
    }
    return std::format("d{}e", encoded);
  }
  return "";
}
} // namespace bencode