#include "download/peer/message.hxx"

#include <cstddef>
#include <cstdint>
#include <download/peer/byte_utils.hxx>
#include <stdexcept>

namespace download::peer {

namespace {

// Frames a message as <4-byte length><1-byte id><payload>.
std::vector<std::byte> make_message(uint8_t id,
                                    std::span<const std::byte> payload = {}) {
  std::vector<std::byte> msg;
  msg.reserve(5 + payload.size());

  uint32_t len = static_cast<uint32_t>(1 + payload.size());
  append_u32(msg, len);
  msg.push_back(std::byte{id});
  msg.insert(msg.end(), payload.begin(), payload.end());

  return msg;
}

} // namespace

std::vector<bool> vec_of_bytes_to_bools(std::span<const std::byte> bytes,
                                        std::optional<std::size_t> num_pieces) {
  std::vector<bool> bits;
  bits.reserve(bytes.size() * 8);

  for (std::byte b : bytes) {
    for (int i = 7; i >= 0; --i) {
      bits.push_back((std::to_integer<unsigned>(b) >> i) & 1u);
    }
  }

  if (num_pieces) {
    bits.resize(*num_pieces);
  }
  return bits;
}

std::vector<std::byte> bools_to_bitfield(const std::vector<bool> &bits) {
  std::vector<std::byte> bytes((bits.size() + 7) / 8, std::byte{0});

  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (bits[i]) {
      bytes[i / 8] |= std::byte{static_cast<unsigned char>(0x80u >> (i % 8))};
    }
  }
  return bytes;
}

PeerMessage PeerMessage::from_bytes(std::byte id,
                                    std::vector<std::byte> payload, std::size_t bitfield_size) {
  switch (id) {
  case std::byte{0}:
    return Choke{};
  case std::byte{1}:
    return Unchoke{};
  case std::byte{2}:
    return Interested{};
  case std::byte{3}:
    return NotInterested{};
  case std::byte{4}:
    return Have{extract_u32(payload)};
  case std::byte{5}:
    return Bitfield{vec_of_bytes_to_bools(payload, bitfield_size)};
  case std::byte{6}:
    return Request{.index = extract_u32(payload, 0),
                   .begin = extract_u32(payload, 4),
                   .length = extract_u32(payload, 8)};
  case std::byte{7}:
    return Piece{.index = extract_u32(payload),
                 .begin = extract_u32(payload, 4),
                 .block = {payload.begin() + 8, payload.end()}};
  case std::byte{8}:
    return Cancel{.index = extract_u32(payload, 0),
                  .begin = extract_u32(payload, 4),
                  .offset = extract_u32(payload, 8)};
  }

  throw std::logic_error("invalid id received from peer");
}

std::vector<std::byte> PeerMessage::to_bytes(const PeerMessage& message) {
  return std::visit(
      overloaded{
          [](const KeepAlive &) -> std::vector<std::byte> {
            return {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
          },
          [](const Choke &) { return make_message(0); },
          [](const Unchoke &) { return make_message(1); },
          [](const Interested &) { return make_message(2); },
          [](const NotInterested &) { return make_message(3); },
          [](const Have &h) {
            std::vector<std::byte> payload;
            append_u32(payload, h.index);
            return make_message(4, payload);
          },
          [](const Bitfield &b) {
            return make_message(5, bools_to_bitfield(b.mask));
          },
          [](const Request &r) {
            std::vector<std::byte> payload;
            append_u32(payload, r.index);
            append_u32(payload, r.begin);
            append_u32(payload, r.length);
            return make_message(6, payload);
          },
          [](const Piece &p) {
            std::vector<std::byte> payload;
            payload.reserve(8 + p.block.size());
            append_u32(payload, p.index);
            append_u32(payload, p.begin);
            payload.insert(payload.end(), p.block.begin(), p.block.end());
            return make_message(7, payload);
          },
          [](const Cancel &c) {
            std::vector<std::byte> payload;
            append_u32(payload, c.index);
            append_u32(payload, c.begin);
            append_u32(payload, c.offset);
            return make_message(8, payload);
          }},
      message);
}

} // namespace download::peer
