#pragma once

#include <error/error.hxx>
#include <format>
#include <string>

namespace bencode {

enum class BencodeErrorCode {
  missing_starting_character,
  missing_terminator,
  malformed_number,
  malformed_bytestring,
  malformed_list
};

constexpr std::string to_string(BencodeErrorCode ec) {
  switch (ec) {
  case BencodeErrorCode::missing_starting_character:
    return "Missing Starting Character";
  case BencodeErrorCode::missing_terminator:
    return "Missing Ending Character";
  case BencodeErrorCode::malformed_number:
    return "Malformed bencode number";
  case BencodeErrorCode::malformed_bytestring:
    return "Malformed bencode bytestring";
  case BencodeErrorCode::malformed_list:
    return "Malformed bencode list";
  }
  return "Unknown bencode error";
}

class Error : public error::BaseError {
public:
  BencodeErrorCode code;
  std::string detail;

  explicit Error(BencodeErrorCode c, std::string msg)
      : code(c), detail(std::move(msg)) {}

  std::string str() const override {
    return std::format("[{}] {} ({})", module(), to_string(code), detail);
  }

  constexpr std::string module() const override { return "bencode"; }
};

} // namespace bencode
