#pragma once

#include <cstddef>
#include <cstdint>
#include <download/peer/wire/error.hxx>
#include <expected>
#include <span>
#include <variant>
#include <vector>

namespace download::peer::wire {

enum class MessageId : std::uint8_t {
  choke = 0,
  unchoke = 1,
  interested = 2,
  not_interested = 3,
  have = 4,
  bitfield = 5,
  request = 6,
  piece = 7,
  cancel = 8,
};

struct KeepAlive {
  bool operator==(const KeepAlive &) const = default;
};
struct Choke {
  bool operator==(const Choke &) const = default;
};
struct Unchoke {
  bool operator==(const Unchoke &) const = default;
};
struct Interested {
  bool operator==(const Interested &) const = default;
};
struct NotInterested {
  bool operator==(const NotInterested &) const = default;
};
struct Have {
  std::uint32_t index;
  bool operator==(const Have &) const = default;
};
struct Bitfield {
  std::vector<bool> mask;
  bool operator==(const Bitfield &) const = default;
};
struct Request {
  std::uint32_t index;
  std::uint32_t begin;
  std::uint32_t length;
  bool operator==(const Request &) const = default;
};
struct Piece {
  std::uint32_t index;
  std::uint32_t begin;
  std::vector<std::byte> block;
  bool operator==(const Piece &) const = default;
};
struct Cancel {
  std::uint32_t index;
  std::uint32_t begin;
  std::uint32_t length;
  bool operator==(const Cancel &) const = default;
};

using PeerMessage =
    std::variant<KeepAlive, Choke, Unchoke, Interested, NotInterested, Have,
                 Bitfield, Request, Piece, Cancel>;

std::vector<std::byte> encode(const PeerMessage &message);

std::expected<PeerMessage, Error> decode(std::span<const std::byte> body,
                                         std::size_t num_pieces);

std::vector<std::byte> pack_bitfield(const std::vector<bool> &bits);
std::expected<std::vector<bool>, Error>
unpack_bitfield(std::span<const std::byte> bytes, std::size_t num_pieces);

}
