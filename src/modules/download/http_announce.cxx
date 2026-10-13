#include <bencode/decode.hxx>
#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <httplib/httplib.h>

#include <download/http_announce.hxx>

namespace download {

HttpAnnounceRequest HttpAnnounceRequest::build_request(const download::DownloadManager& manager) {
  return HttpAnnounceRequest{
    .info_hash = manager.info_hash(),
    .peer_id = "-TR2940-614361349012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = manager.uploaded(),
    .downloaded = manager.downloaded(),
    .left = manager.left(),
  };
}

std::string HttpAnnounceRequest::encode_params() const {
  std::string url{};

  url += "?info_hash=" + url_encode(info_hash);
  url += "&peer_id=" + url_encode(peer_id);
  url += "&port=" + std::to_string(port);
  url += "&uploaded=" + std::to_string(uploaded);
  url += "&downloaded=" + std::to_string(downloaded);
  url += "&left=" + std::to_string(left);

  if (ip.has_value()) {
    url += "&ip=" + url_encode(*ip);
  }

  return url;
}

std::string url_encode(std::span<const std::byte> data) {
  std::string encoded;
  encoded.reserve(data.size() * 3);
  static constexpr char hex_chars[] = "0123456789ABCDEF";

  for (std::byte b : data) {
    char c = static_cast<char>(b);
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex_chars[(c >> 4) & 0x0F]);
      encoded.push_back(hex_chars[c & 0x0F]);
    }
  }
  return encoded;
}

std::expected<HttpAnnounceResponse, std::string>
HttpAnnounceResponse::parse(std::string_view data) {
  auto span = std::as_bytes(std::span(data.data(), data.size()));
  auto decode_res = bencode::decode(span);
  if (!decode_res.has_value()) {
    return std::unexpected("Failed to decode bencode tracker response: " + decode_res.error().str());
  }
  return from_bencode(decode_res.value());
}

std::expected<HttpAnnounceResponse, std::string>
HttpAnnounceResponse::from_bencode(const bencode::Value& value) {
  if (!std::holds_alternative<bencode::Dict>(value.data)) {
    return std::unexpected("Tracker response root is not a dictionary");
  }

  const auto& dict = std::get<bencode::Dict>(value.data);

  if (auto it = dict.find("failure reason"); it != dict.end()) {
    if (!std::holds_alternative<bencode::ByteString>(it->second.data)) {
      return std::unexpected("Field 'failure reason' is not a byte string");
    }

    HttpAnnounceResponseFailure failure{
      .failure_reason = std::get<bencode::ByteString>(it->second.data),
      .warning_message = std::nullopt,
    };

    if (auto warn_it = dict.find("warning message"); warn_it != dict.end()) {
      if (std::holds_alternative<bencode::ByteString>(warn_it->second.data)) {
        failure.warning_message = std::get<bencode::ByteString>(warn_it->second.data);
      }
    }

    return HttpAnnounceResponse(std::move(failure));
  }

  HttpAnnounceResponseSuccess success;

  auto interval_it = dict.find("interval");
  if (interval_it == dict.end()) {
    return std::unexpected("Missing required key 'interval' in tracker response");
  }
  if (!std::holds_alternative<bencode::Integer>(interval_it->second.data)) {
    return std::unexpected("Field 'interval' is not an integer");
  }
  success.interval = std::get<bencode::Integer>(interval_it->second.data);

  // 3. Peers (required for success)
  auto peers_it = dict.find("peers");
  if (peers_it == dict.end()) {
    return std::unexpected("Missing required key 'peers' in tracker response");
  }

  if (std::holds_alternative<bencode::ByteString>(peers_it->second.data)) {
    const auto& compact_peers = std::get<bencode::ByteString>(peers_it->second.data);
    if (compact_peers.size() % 6 != 0) {
      return std::unexpected(std::format(
          "Invalid compact peers length: {} bytes (must be a multiple of 6)",
          compact_peers.size()));
    }

    size_t num_peers = compact_peers.size() / 6;
    success.peers.reserve(num_peers);
    for (size_t i = 0; i < num_peers; ++i) {
      const auto* p = reinterpret_cast<const uint8_t*>(compact_peers.data() + i * 6);
      std::string ip = std::format("{}.{}.{}.{}", p[0], p[1], p[2], p[3]);
      uint16_t port = (static_cast<uint16_t>(p[4]) << 8) | static_cast<uint16_t>(p[5]);
      success.peers.push_back(download::peer::Peer{
        .peer_id = std::nullopt,
        .ip = std::move(ip),
        .port = port,
      });
    }
  } else if (std::holds_alternative<bencode::List>(peers_it->second.data)) {
    // Standard dictionary peer list
    const auto& peer_list = std::get<bencode::List>(peers_it->second.data);
    success.peers.reserve(peer_list.size());

    for (const auto& peer_val : peer_list) {
      if (!std::holds_alternative<bencode::Dict>(peer_val.data)) {
        return std::unexpected("Peer item in 'peers' list is not a dictionary");
      }
      const auto& peer_dict = std::get<bencode::Dict>(peer_val.data);

      auto ip_it = peer_dict.find("ip");
      if (ip_it == peer_dict.end() || !std::holds_alternative<bencode::ByteString>(ip_it->second.data)) {
        return std::unexpected("Missing or invalid 'ip' in peer dictionary");
      }

      auto port_it = peer_dict.find("port");
      if (port_it == peer_dict.end() || !std::holds_alternative<bencode::Integer>(port_it->second.data)) {
        return std::unexpected("Missing or invalid 'port' in peer dictionary");
      }

      download::peer::Peer peer{
        .peer_id = std::nullopt,
        .ip = std::get<bencode::ByteString>(ip_it->second.data),
        .port = static_cast<uint16_t>(std::get<bencode::Integer>(port_it->second.data)),
      };

      if (auto id_it = peer_dict.find("peer id"); id_it != peer_dict.end()) {
        if (std::holds_alternative<bencode::ByteString>(id_it->second.data)) {
          peer.peer_id = std::get<bencode::ByteString>(id_it->second.data);
        }
      }

      success.peers.push_back(std::move(peer));
    }
  } else {
    return std::unexpected("Field 'peers' is neither a byte string nor a list");
  }

  // 4. Optional fields
  if (auto it = dict.find("min interval"); it != dict.end()) {
    if (std::holds_alternative<bencode::Integer>(it->second.data)) {
      success.min_interval = std::get<bencode::Integer>(it->second.data);
    }
  }

  if (auto it = dict.find("tracker id"); it != dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(it->second.data)) {
      success.tracker_id = std::get<bencode::ByteString>(it->second.data);
    }
  }

  if (auto it = dict.find("complete"); it != dict.end()) {
    if (std::holds_alternative<bencode::Integer>(it->second.data)) {
      success.complete = std::get<bencode::Integer>(it->second.data);
    }
  }

  if (auto it = dict.find("incomplete"); it != dict.end()) {
    if (std::holds_alternative<bencode::Integer>(it->second.data)) {
      success.incomplete = std::get<bencode::Integer>(it->second.data);
    }
  }

  if (auto it = dict.find("warning message"); it != dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(it->second.data)) {
      success.warning_message = std::get<bencode::ByteString>(it->second.data);
    }
  }

  // 5. Peers6 (BEP 7 compact IPv6, optional)
  if (auto it = dict.find("peers6"); it != dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(it->second.data)) {
      const auto& compact_ipv6 = std::get<bencode::ByteString>(it->second.data);
      if (compact_ipv6.size() % 18 == 0) {
        size_t num_ipv6 = compact_ipv6.size() / 18;
        for (size_t i = 0; i < num_ipv6; ++i) {
          const auto* p = reinterpret_cast<const uint8_t*>(compact_ipv6.data() + i * 18);
          std::string ip;
          for (size_t g = 0; g < 8; ++g) {
            uint16_t group = (static_cast<uint16_t>(p[g * 2]) << 8) | static_cast<uint16_t>(p[g * 2 + 1]);
            if (g > 0) ip += ':';
            ip += std::format("{:x}", group);
          }
          uint16_t port = (static_cast<uint16_t>(p[16]) << 8) | static_cast<uint16_t>(p[17]);
          success.peers.push_back(download::peer::Peer{
            .peer_id = std::nullopt,
            .ip = std::move(ip),
            .port = port,
          });
        }
      }
    }
  }

  return HttpAnnounceResponse(std::move(success));
}

} // namespace download
