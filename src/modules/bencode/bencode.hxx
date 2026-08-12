#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace bencode {

class Value;

using Integer = int64_t;
using ByteString = std::string;
using List = std::vector<Value>;
using Dict = std::map<ByteString, Value>;

class Value {
public:
  std::variant<Integer, ByteString, List, Dict> data;

  Value() = default;
  Value(Integer i) : data(i) {}
  Value(ByteString s) : data(std::move(s)) {}
  Value(List l) : data(std::move(l)) {}
  Value(Dict d) : data(std::move(d)) {}
};

} // namespace bencode
