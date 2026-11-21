#include <download/peer/wire/bytes.hxx>
#include <download/peer/wire/message.hxx>

#include <format>

namespace download::peer::wire {

namespace {

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

std::vector<std::byte> frame(MessageId id, std::span<const std::byte> payload = {}) {
  std::vector<std::byte> msg;
  msg.reserve(5 + payload.size());
  append_u32_be(msg, static_cast<std::uint32_t>(1 + payload.size()));
  msg.push_back(static_cast<std::byte>(id));
  msg.insert(msg.end(), payload.begin(), payload.end());
  return msg;
}

std::vector<std::byte> u32s(std::initializer_list<std::uint32_t> values) {
  std::vector<std::byte> out;
  out.reserve(values.size() * 4);
  for (auto v : values) {
    append_u32_be(out, v);
  }
  return out;
}

Error length_error(MessageId id, std::size_t got, std::string_view expected) {
  return Error::make(Errc::bad_payload_length,
                     std::format("id {}: {} bytes, expected {}",
                                 static_cast<int>(id), got, expected));
}

}

std::vector<std::byte> encode(const PeerMessage &message) {
  return std::visit(
      overloaded{
          [](const KeepAlive &) { return std::vector<std::byte>(4, std::byte{0}); },
          [](const Choke &) { return frame(MessageId::choke); },
          [](const Unchoke &) { return frame(MessageId::unchoke); },
          [](const Interested &) { return frame(MessageId::interested); },
          [](const NotInterested &) { return frame(MessageId::not_interested); },
          [](const Have &m) { return frame(MessageId::have, u32s({m.index})); },
          [](const Bitfield &m) {
            return frame(MessageId::bitfield, pack_bitfield(m.mask));
          },
          [](const Request &m) {
            return frame(MessageId::request, u32s({m.index, m.begin, m.length}));
          },
          [](const Piece &m) {
            std::vector<std::byte> payload = u32s({m.index, m.begin});
            payload.insert(payload.end(), m.block.begin(), m.block.end());
            return frame(MessageId::piece, payload);
          },
          [](const Cancel &m) {
            return frame(MessageId::cancel, u32s({m.index, m.begin, m.length}));
          },
      },
      message);
}

std::expected<PeerMessage, Error> decode(std::span<const std::byte> body,
                                         std::size_t num_pieces) {
  if (body.empty()) {
    return std::unexpected(Error::make(Errc::bad_payload_length, "empty body"));
  }

  const auto raw_id = std::to_integer<std::uint8_t>(body[0]);
  const auto id = static_cast<MessageId>(raw_id);
  const auto payload = body.subspan(1);

  auto expect_size = [&](std::size_t n) -> std::expected<void, Error> {
    if (payload.size() != n) {
      return std::unexpected(length_error(id, payload.size(), std::to_string(n)));
    }
    return {};
  };

  switch (id) {
  case MessageId::choke:
  case MessageId::unchoke:
  case MessageId::interested:
  case MessageId::not_interested: {
    if (auto ok = expect_size(0); !ok)
      return std::unexpected(ok.error());
    switch (id) {
    case MessageId::choke:
      return Choke{};
    case MessageId::unchoke:
      return Unchoke{};
    case MessageId::interested:
      return Interested{};
    default:
      return NotInterested{};
    }
  }

  case MessageId::have: {
    if (auto ok = expect_size(4); !ok)
      return std::unexpected(ok.error());
    std::uint32_t index = load_u32_be(payload);
    if (index >= num_pieces) {
      return std::unexpected(Error::make(
          Errc::bad_piece_index, std::format("have {} of {}", index, num_pieces)));
    }
    return Have{index};
  }

  case MessageId::bitfield: {
    auto mask = unpack_bitfield(payload, num_pieces);
    if (!mask)
      return std::unexpected(mask.error());
    return Bitfield{std::move(*mask)};
  }

  case MessageId::request:
  case MessageId::cancel: {
    if (auto ok = expect_size(12); !ok)
      return std::unexpected(ok.error());
    std::uint32_t index = load_u32_be(payload, 0);
    std::uint32_t begin = load_u32_be(payload, 4);
    std::uint32_t length = load_u32_be(payload, 8);
    if (id == MessageId::request)
      return Request{index, begin, length};
    return Cancel{index, begin, length};
  }

  case MessageId::piece: {
    if (payload.size() < 8) {
      return std::unexpected(length_error(id, payload.size(), "at least 8"));
    }
    return Piece{.index = load_u32_be(payload, 0),
                 .begin = load_u32_be(payload, 4),
                 .block = {payload.begin() + 8, payload.end()}};
  }
  }

  return std::unexpected(
      Error::make(Errc::unknown_message_id, std::format("id {}", raw_id)));
}

std::vector<std::byte> pack_bitfield(const std::vector<bool> &bits) {
  std::vector<std::byte> bytes((bits.size() + 7) / 8, std::byte{0});
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (bits[i]) {
      bytes[i / 8] |= std::byte{static_cast<unsigned char>(0x80u >> (i % 8))};
    }
  }
  return bytes;
}

std::expected<std::vector<bool>, Error>
unpack_bitfield(std::span<const std::byte> bytes, std::size_t num_pieces) {
  const std::size_t expected_bytes = (num_pieces + 7) / 8;
  if (bytes.size() != expected_bytes) {
    return std::unexpected(Error::make(
        Errc::bad_bitfield, std::format("{} bytes for {} pieces, expected {}",
                                        bytes.size(), num_pieces, expected_bytes)));
  }

  std::vector<bool> bits(num_pieces);
  for (std::size_t i = 0; i < bytes.size() * 8; ++i) {
    bool set = (std::to_integer<unsigned>(bytes[i / 8]) >> (7 - i % 8)) & 1u;
    if (i < num_pieces) {
      bits[i] = set;
    } else if (set) {
      return std::unexpected(
          Error::make(Errc::bad_bitfield, "padding bits are set"));
    }
  }
  return bits;
}

}
