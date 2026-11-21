#pragma once

#include <error/error.hxx>
#include <string>
#include <system_error>

namespace io {

enum class ErrC { unexpected_eof = 1, short_write };

const std::error_category &io_category() noexcept;
std::error_code make_error_code(ErrC) noexcept;

struct Error : error::BaseError {
  std::error_code code;
  std::string context;

  static Error from_errno(std::string context);
  std::string str() const override;
  std::string module() const override { return "io"; }
};

}

template <> struct std::is_error_code_enum<io::ErrC> : std::true_type {};
