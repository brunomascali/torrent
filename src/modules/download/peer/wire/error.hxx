#pragma once

#include <error/error.hxx>
#include <io/error.hxx>
#include <string>
#include <system_error>

namespace download::peer::wire {

enum class Errc {
  unknown_message_id = 1,
  bad_payload_length,
  bad_piece_index,
  bad_bitfield,
  bad_protocol,
  message_too_large,
};

const std::error_category &wire_category() noexcept;
std::error_code make_error_code(Errc) noexcept;

struct Error : error::BaseError {
  std::error_code code;
  std::string context;

  static Error make(std::error_code code, std::string context = {});
  static Error from_io(const io::Error &e);

  std::string str() const override;
  std::string module() const override { return "wire"; }
};

}

template <>
struct std::is_error_code_enum<download::peer::wire::Errc> : std::true_type {};
