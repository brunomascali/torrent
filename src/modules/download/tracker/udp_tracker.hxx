#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <download/tracker/announce.hxx>
#include <download/tracker/timeouts.hxx>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace download::tracker::udp {

inline constexpr std::uint64_t protocol_id = 0x41727101980;

enum class Action : std::uint32_t { connect = 0, announce = 1, scrape = 2, error = 3 };

enum class AddressFamily { v4, v6 };

struct Endpoint {
  std::string host;
  std::uint16_t port;
};

std::expected<Endpoint, std::string> parse_url(std::string_view url);

std::array<std::byte, 16> encode_connect(std::uint32_t transaction_id);

std::expected<std::uint64_t, std::string>
decode_connect(std::span<const std::byte> packet, std::uint32_t transaction_id);

std::array<std::byte, 98> encode_announce(std::uint64_t connection_id,
                                          std::uint32_t transaction_id,
                                          const AnnounceRequest &request,
                                          std::uint32_t key);

std::expected<AnnounceResponse, std::string>
decode_announce(std::span<const std::byte> packet, std::uint32_t transaction_id,
                AddressFamily family);

std::expected<AnnounceResponse, std::string>
announce_addresses(std::span<const std::string> addresses, std::uint16_t port,
                   const AnnounceRequest &request, const TrackerTimeouts &timeouts,
                   std::uint32_t key);

std::expected<AnnounceResponse, std::string>
announce(std::string_view url, const AnnounceRequest &request,
         const TrackerTimeouts &timeouts, std::uint32_t key);

}
