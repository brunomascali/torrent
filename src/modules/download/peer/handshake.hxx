#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <array>

namespace download::peer {
  struct HandshakeMessage {
    static std::array<std::byte, 68> as_bytes(std::string_view info_hash) {
        constexpr std::string_view pstr = "BitTorrent protocol";
        constexpr std::string_view peer_id = "12312412312315123512";

        std::array<std::byte, 68> handshake_msg{};
        auto it = handshake_msg.begin();

        // pstrlen
        *it++ = std::byte{static_cast<unsigned char>(pstr.size())};

        // pstr
        it = std::transform(pstr.begin(), pstr.end(), it,
                             [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

        // reserved (8 bytes, all zero)
        it = std::fill_n(it, 8, std::byte{0});

        // info_hash (20 bytes)
        it = std::transform(info_hash.begin(), info_hash.end(), it,
                             [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

        // peer_id (20 bytes)
        it = std::transform(peer_id.begin(), peer_id.end(), it,
                             [](char c) { return std::byte{static_cast<unsigned char>(c)}; });

        return handshake_msg;
    }
  };
}
