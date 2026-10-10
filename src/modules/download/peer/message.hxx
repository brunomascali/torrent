#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace download::peer {

// Unpacks an MSB-first bitfield. If num_pieces is given, the result is trimmed
// to that length (dropping the padding bits of the last byte).
std::vector<bool>
vec_of_bytes_to_bools(std::span<const std::byte> bytes,
                      std::optional<std::size_t> num_pieces = std::nullopt);

std::vector<std::byte> bools_to_bitfield(const std::vector<bool> &bits);

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Choke {};
struct Unchoke {};
struct Interested {};
struct NotInterested {};
struct Have {
  uint32_t index;
};
struct Bitfield {
  std::vector<bool> mask;
};
struct Request {
  uint32_t index;
  uint32_t begin;
  uint32_t length;
};
struct Piece {
  uint32_t index;
  uint32_t begin;
  std::vector<std::byte> block;
};
struct Cancel {
  uint32_t index;
  uint32_t begin;
  uint32_t offset;
};
struct KeepAlive {};

using message_variants =
    std::variant<Choke, Unchoke, Interested, NotInterested, Have, Bitfield,
                 Request, Piece, Cancel, KeepAlive>;

struct PeerMessage : public message_variants {
  using message_variants::variant;

  static PeerMessage from_bytes(std::byte id,
                                std::vector<std::byte> payload = {}, std::size_t bitfield_size = 0);

  static std::vector<std::byte> to_bytes(const PeerMessage&);
};

} // namespace download::peer
