#include <io/error.hxx>

#include <cerrno>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace io {

namespace {

class IoCategory final : public std::error_category {
public:
  const char *name() const noexcept override { return "io"; }

  std::string message(int ev) const override {
    switch (static_cast<ErrC>(ev)) {
    case ErrC::unexpected_eof:
      return "unexpected end of file";
    case ErrC::short_write:
      return "short write";
    }
    return "unknown io error";
  }
};

}

const std::error_category &io_category() noexcept {
  static const IoCategory category;
  return category;
}

std::error_code make_error_code(ErrC e) noexcept {
  return {static_cast<int>(e), io_category()};
}

Error Error::from_errno(std::string context) {
  const int err = errno;
  Error e;
  e.code = std::error_code(err, std::system_category());
  e.context = std::move(context);
  return e;
}

std::string Error::str() const {
  if (context.empty()) {
    return std::format("[{}] {}", module(), code.message());
  }
  return std::format("[{}] {}: {}", module(), context, code.message());
}

}
