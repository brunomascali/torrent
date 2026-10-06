#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>
#include <download/peer/byte_utils.hxx>

namespace download::peer {

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
  std::vector<std::byte> mask;
};
struct Request {
  uint32_t index;
  uint32_t begin;
  uint32_t length;
};
struct Piece {
  uint32_t index;
  uint32_t begin;
  std::span<std::byte> block;
};
struct Cancel {
  uint32_t index;
  uint32_t begin;
  uint32_t offset;
};
struct KeepAlive {};

using message_variants = std::variant<Choke, Unchoke, Interested, NotInterested,
                                      Have, Bitfield, Request, Piece, Cancel, KeepAlive>;


struct PeerMessage : public message_variants {
  using message_variants::variant;

  static PeerMessage from_bytes(std::byte id,
                                std::vector<std::byte> payload = {}) {
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
      return Bitfield{std::move(payload)};
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

  std::vector<std::byte> to_bytes() const {
      return std::visit(overloaded{
          [](const KeepAlive&) -> std::vector<std::byte> {
              return {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
          },
          [](const Choke&) {
              return make_message(0);
          },
          [](const Unchoke&) {
              return make_message(1);
          },
          [](const Interested&) {
              return make_message(2);
          },
          [](const NotInterested&) {
              return make_message(3);
          },
          [](const Have& h) {
              std::vector<std::byte> payload;
              append_u32(payload, h.index);
              return make_message(4, payload);
          },
          [](const Bitfield& b) {
              return make_message(5, b.mask);
          },
          [](const Request& r) {
              std::vector<std::byte> payload;
              append_u32(payload, r.index);
              append_u32(payload, r.begin);
              append_u32(payload, r.length);
              return make_message(6, payload);
          },
          [](const Piece& p) {
              std::vector<std::byte> payload;
              append_u32(payload, p.index);
              append_u32(payload, p.begin);
              payload.insert(payload.end(), p.block.begin(), p.block.end());
              return make_message(7, payload);
          },
          [](const Cancel& c) {
              std::vector<std::byte> payload;
              append_u32(payload, c.index);
              append_u32(payload, c.begin);
              append_u32(payload, c.offset);
              return make_message(8, payload);
          }
      }, *this);
  }

  static std::vector<std::byte> make_message(uint8_t id, std::span<const std::byte> payload = {}) {
      std::vector<std::byte> msg;
      uint32_t len = static_cast<uint32_t>(1 + payload.size());

      append_u32(msg, len);
      msg.push_back(std::byte{id});
      msg.insert(msg.end(), payload.begin(), payload.end());

      return msg;
  }
};

} // namespace download::peer
