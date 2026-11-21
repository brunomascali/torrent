#pragma once

#include <array>
#include <bencode/bencode.hxx>
#include <cstddef>
#include <cstdint>
#include <download/peer/peer.hxx>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace download::tracker {

std::string url_encode(std::span<const std::byte> data);

inline std::string url_encode(std::string_view text) {
  return url_encode(std::as_bytes(std::span(text)));
}

enum class Event { none, started, completed, stopped };

struct AnnounceRequest {
  std::array<std::byte, 20> info_hash{};
  std::array<std::byte, 20> peer_id{};
  std::uint16_t port{0};
  std::uint64_t uploaded{0};
  std::uint64_t downloaded{0};
  std::uint64_t left{0};
  Event event{Event::none};
  bool compact{true};
  std::optional<std::string> ip;

  [[nodiscard]] std::string query_string() const;
};

struct AnnounceSuccess {
  std::int64_t interval{0};
  std::vector<download::peer::Peer> peers;
  std::optional<std::int64_t> min_interval;
  std::optional<std::string> tracker_id;
  std::optional<std::int64_t> complete;
  std::optional<std::int64_t> incomplete;
  std::optional<std::string> warning_message;

  bool operator==(const AnnounceSuccess &other) const = default;
};

struct AnnounceFailure {
  std::string failure_reason;
  std::optional<std::string> warning_message;

  bool operator==(const AnnounceFailure &other) const = default;
};

struct AnnounceResponse : public std::variant<AnnounceSuccess, AnnounceFailure> {
  using std::variant<AnnounceSuccess, AnnounceFailure>::variant;

  [[nodiscard]] bool is_success() const {
    return std::holds_alternative<AnnounceSuccess>(*this);
  }

  [[nodiscard]] bool is_failure() const {
    return std::holds_alternative<AnnounceFailure>(*this);
  }

  [[nodiscard]] const AnnounceSuccess *as_success() const {
    return std::get_if<AnnounceSuccess>(this);
  }

  [[nodiscard]] const AnnounceFailure *as_failure() const {
    return std::get_if<AnnounceFailure>(this);
  }

  static std::expected<AnnounceResponse, std::string> parse(std::string_view data);
  static std::expected<AnnounceResponse, std::string>
  from_bencode(const bencode::Value &value);
};

}
