#pragma once

#include "download/download_manager.hxx"
#include <bencode/bencode.hxx>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <download/peer/peer.hxx>

namespace download {

std::string url_encode(std::string_view data);

struct HttpAnnounceRequest {
  std::string info_hash;
  std::string peer_id;
  std::optional<std::string> ip;
  uint16_t port;
  uint64_t uploaded;
  uint64_t downloaded;
  uint64_t left;
  // Event event

  [[nodiscard]] static HttpAnnounceRequest build_request(const download::DownloadManager& manager);
  [[nodiscard]] std::string encode_params() const;
};

struct HttpAnnounceResponseSuccess {
  int64_t interval{0};
  std::vector<download::peer::Peer> peers;
  std::optional<int64_t> min_interval;
  std::optional<std::string> tracker_id;
  std::optional<int64_t> complete;
  std::optional<int64_t> incomplete;
  std::optional<std::string> warning_message;

  bool operator==(const HttpAnnounceResponseSuccess& other) const = default;
};

struct HttpAnnounceResponseFailure {
  std::string failure_reason;
  std::optional<std::string> warning_message;

  bool operator==(const HttpAnnounceResponseFailure& other) const = default;
};

struct HttpAnnounceResponse : public std::variant<HttpAnnounceResponseSuccess, HttpAnnounceResponseFailure> {
  using std::variant<HttpAnnounceResponseSuccess, HttpAnnounceResponseFailure>::variant;

  [[nodiscard]] bool is_success() const {
    return std::holds_alternative<HttpAnnounceResponseSuccess>(*this);
  }

  [[nodiscard]] bool is_failure() const {
    return std::holds_alternative<HttpAnnounceResponseFailure>(*this);
  }

  [[nodiscard]] const HttpAnnounceResponseSuccess* as_success() const {
    return std::get_if<HttpAnnounceResponseSuccess>(this);
  }

  [[nodiscard]] const HttpAnnounceResponseFailure* as_failure() const {
    return std::get_if<HttpAnnounceResponseFailure>(this);
  }

  static std::expected<HttpAnnounceResponse, std::string> parse(std::string_view data);
  static std::expected<HttpAnnounceResponse, std::string> from_bencode(const bencode::Value& value);
};

} // namespace download
