#include <download/peer/wire/error.hxx>

#include <format>
#include <utility>

namespace download::peer::wire {

namespace {

class WireCategory final : public std::error_category {
public:
  const char *name() const noexcept override { return "peer-wire"; }

  std::string message(int ev) const override {
    switch (static_cast<Errc>(ev)) {
    case Errc::unknown_message_id:
      return "unknown message id";
    case Errc::bad_payload_length:
      return "payload length doesn't match message type";
    case Errc::bad_piece_index:
      return "piece index out of range";
    case Errc::bad_bitfield:
      return "invalid bitfield";
    case Errc::bad_protocol:
      return "not a BitTorrent handshake";
    case Errc::message_too_large:
      return "message too large";
    }
    return "unknown wire error";
  }
};

}

const std::error_category &wire_category() noexcept {
  static const WireCategory category;
  return category;
}

std::error_code make_error_code(Errc e) noexcept {
  return {static_cast<int>(e), wire_category()};
}

Error Error::make(std::error_code code, std::string context) {
  Error e;
  e.code = code;
  e.context = std::move(context);
  return e;
}

Error Error::from_io(const io::Error &e) { return make(e.code, e.context); }

std::string Error::str() const {
  if (context.empty()) {
    return std::format("[{}] {}", module(), code.message());
  }
  return std::format("[{}] {}: {}", module(), context, code.message());
}

}
