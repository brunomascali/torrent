#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace download {
  struct Peer {
    std::optional<std::string> peer_id;
    std::string ip;
    uint16_t port{0};

    bool operator==(const Peer& other) const = default;
  };
}
