#pragma once

#include <cerrno>
#include <cstring>
#include <error/error.hxx>
#include <format>
#include <string>

namespace io {

enum class IOErrorCode {
  system_error,
  open_failure,
  read_failure,
  write_failure,
  flush_failure
};

constexpr std::string to_string(IOErrorCode ec) {
  switch (ec) {
  case IOErrorCode::system_error:
    return "System Error";
  case IOErrorCode::open_failure:
    return "Open Failure";
  case IOErrorCode::read_failure:
    return "Read Failure";
  case IOErrorCode::write_failure:
    return "Write Failure";
  case IOErrorCode::flush_failure:
    return "Flush Failure";
  }
  return "Unknown IO error";
}

class Error : public error::BaseError {
public:
  IOErrorCode code;
  int system_errno;
  std::string detail;

  explicit Error(IOErrorCode c, std::string msg)
      : code(c), system_errno(0), detail(std::move(msg)) {}

  static Error errno_to_error(const std::string &context) {
    int err = errno;
    std::string msg = context + ": " + std::strerror(err);
    return Error(std::move(msg), err);
  }

  std::string str() const override {
    if (system_errno != 0) {
      return std::format("[{}] {} ({})", module(), std::to_string(system_errno),
                         detail);
    }
    return std::format("[{}] {} ({})", module(), to_string(code), detail);
  }

  constexpr std::string module() const override { return "io"; }

private:
  explicit Error(std::string msg, int sys_errno = 0)
      : code(IOErrorCode::system_error), system_errno(sys_errno),
        detail(std::move(msg)) {}
};

} // namespace io
