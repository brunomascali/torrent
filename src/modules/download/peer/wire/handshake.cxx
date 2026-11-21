#include <download/peer/wire/handshake.hxx>

#include <algorithm>

namespace download::peer::wire {

namespace {
constexpr std::size_t reserved_offset = 1 + Handshake::protocol.size();
constexpr std::size_t info_hash_offset = reserved_offset + 8;
constexpr std::size_t peer_id_offset = info_hash_offset + 20;
}

std::array<std::byte, Handshake::size> Handshake::encode() const {
  std::array<std::byte, size> out{};
  out[0] = static_cast<std::byte>(protocol.size());
  std::ranges::transform(protocol, out.begin() + 1,
                         [](char c) { return static_cast<std::byte>(c); });
  std::ranges::copy(reserved, out.begin() + reserved_offset);
  std::ranges::copy(info_hash, out.begin() + info_hash_offset);
  std::ranges::copy(peer_id, out.begin() + peer_id_offset);
  return out;
}

std::expected<Handshake, Error>
Handshake::decode(std::span<const std::byte, size> bytes) {
  bool protocol_ok =
      std::to_integer<std::size_t>(bytes[0]) == protocol.size() &&
      std::ranges::equal(bytes.subspan(1, protocol.size()), protocol,
                         [](std::byte b, char c) {
                           return b == static_cast<std::byte>(c);
                         });
  if (!protocol_ok) {
    return std::unexpected(Error::make(Errc::bad_protocol));
  }

  Handshake h;
  std::ranges::copy(bytes.subspan(reserved_offset, 8), h.reserved.begin());
  std::ranges::copy(bytes.subspan(info_hash_offset, 20), h.info_hash.begin());
  std::ranges::copy(bytes.subspan(peer_id_offset, 20), h.peer_id.begin());
  return h;
}

}
