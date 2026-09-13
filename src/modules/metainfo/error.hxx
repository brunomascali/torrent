#pragma once

#include <error/error.hxx>
#include <format>
#include <string>

namespace metainfo {

enum class MetainfoErrorCode {
  missing_key
};

constexpr std::string to_string(MetainfoErrorCode ec) {
  switch (ec) {
  case MetainfoErrorCode::missing_key:
    return "Missing key";
  }
  return "Unknown metainfo error";
}

class Error : public error::BaseError {
public:
  MetainfoErrorCode code;
  std::string detail;

  explicit Error(MetainfoErrorCode c, std::string msg)
      : code(c), detail(std::move(msg)) {}

  std::string str() const override {
    return std::format("[{}] {} ({})", module(), to_string(code), detail);
  }

  constexpr std::string module() const override { return "bencode"; }
};

} // namespace bencode
