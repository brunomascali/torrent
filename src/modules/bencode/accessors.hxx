#pragma once

#include <bencode/bencode.hxx>
#include <expected>
#include <string>
#include <variant>

namespace bencode {

enum class AccessError { missing_key, wrong_type, not_a_dict };

template <typename T>
std::expected<std::reference_wrapper<const T>, AccessError>
get(const Dict &dict, const std::string &key) {
  auto it = dict.find(key);
  if (it == dict.end()) {
    return std::unexpected(AccessError::missing_key);
  }

  const T *value = std::get_if<T>(&it->second.data);
  if (!value) {
    return std::unexpected(AccessError::wrong_type);
  }
  return std::cref(*value);
}

} // namespace bencode
