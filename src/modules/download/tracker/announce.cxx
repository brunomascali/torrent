#include <bencode/decode.hxx>
#include <download/tracker/announce.hxx>

#include <cstdint>
#include <format>
#include <span>

namespace download::tracker {

namespace {

std::string_view to_string(Event event) {
  switch (event) {
  case Event::started:
    return "started";
  case Event::completed:
    return "completed";
  case Event::stopped:
    return "stopped";
  case Event::none:
    break;
  }
  return "";
}

}

std::string AnnounceRequest::query_string() const {
  std::string q;
  q += "info_hash=" + url_encode(info_hash);
  q += "&peer_id=" + url_encode(peer_id);
  q += "&port=" + std::to_string(port);
  q += "&uploaded=" + std::to_string(uploaded);
  q += "&downloaded=" + std::to_string(downloaded);
  q += "&left=" + std::to_string(left);
  if (compact) {
    q += "&compact=1";
  }
  if (event != Event::none) {
    q += "&event=";
    q += to_string(event);
  }
  if (ip) {
    q += "&ip=" + url_encode(*ip);
  }
  return q;
}

std::expected<std::vector<download::peer::Peer>, std::string>
parse_compact_peers(std::span<const std::byte> bytes) {
  if (bytes.size() % 6 != 0) {
    return std::unexpected(std::format(
        "Invalid compact peers length: {} bytes (must be a multiple of 6)", bytes.size()));
  }
  std::vector<download::peer::Peer> peers;
  peers.reserve(bytes.size() / 6);
  for (std::size_t i = 0; i < bytes.size(); i += 6) {
    auto b = [&](std::size_t k) { return std::to_integer<unsigned>(bytes[i + k]); };
    peers.push_back(download::peer::Peer{
        .peer_id = std::nullopt,
        .ip = std::format("{}.{}.{}.{}", b(0), b(1), b(2), b(3)),
        .port = static_cast<std::uint16_t>((b(4) << 8) | b(5)),
    });
  }
  return peers;
}

std::expected<std::vector<download::peer::Peer>, std::string>
parse_compact_peers6(std::span<const std::byte> bytes) {
  if (bytes.size() % 18 != 0) {
    return std::unexpected(std::format(
        "Invalid compact IPv6 peers length: {} bytes (must be a multiple of 18)",
        bytes.size()));
  }
  std::vector<download::peer::Peer> peers;
  peers.reserve(bytes.size() / 18);
  for (std::size_t i = 0; i < bytes.size(); i += 18) {
    auto b = [&](std::size_t k) { return std::to_integer<unsigned>(bytes[i + k]); };
    std::string ip;
    for (std::size_t g = 0; g < 8; ++g) {
      if (g > 0) {
        ip += ':';
      }
      ip += std::format("{:x}", (b(g * 2) << 8) | b(g * 2 + 1));
    }
    peers.push_back(download::peer::Peer{
        .peer_id = std::nullopt,
        .ip = std::move(ip),
        .port = static_cast<std::uint16_t>((b(16) << 8) | b(17)),
    });
  }
  return peers;
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

std::expected<AnnounceResponse, std::string>
AnnounceResponse::parse(std::string_view data) {
  auto span = std::as_bytes(std::span(data.data(), data.size()));
  auto decode_res = bencode::decode(span);
  if (!decode_res.has_value()) {
    return std::unexpected("Failed to decode bencode tracker response: " + decode_res.error().str());
  }
  return from_bencode(decode_res.value());
}

std::expected<AnnounceResponse, std::string>
AnnounceResponse::from_bencode(const bencode::Value& value) {
  if (!std::holds_alternative<bencode::Dict>(value.data)) {
    return std::unexpected("Tracker response root is not a dictionary");
  }

  const auto& dict = std::get<bencode::Dict>(value.data);

  if (auto it = dict.find("failure reason"); it != dict.end()) {
    if (!std::holds_alternative<bencode::ByteString>(it->second.data)) {
      return std::unexpected("Field 'failure reason' is not a byte string");
    }

    AnnounceFailure failure{
      .failure_reason = std::get<bencode::ByteString>(it->second.data),
      .warning_message = std::nullopt,
    };

    if (auto warn_it = dict.find("warning message"); warn_it != dict.end()) {
      if (std::holds_alternative<bencode::ByteString>(warn_it->second.data)) {
        failure.warning_message = std::get<bencode::ByteString>(warn_it->second.data);
      }
    }

    return AnnounceResponse(std::move(failure));
  }

  AnnounceSuccess success;

  auto interval_it = dict.find("interval");
  if (interval_it == dict.end()) {
    return std::unexpected("Missing required key 'interval' in tracker response");
  }
  if (!std::holds_alternative<bencode::Integer>(interval_it->second.data)) {
    return std::unexpected("Field 'interval' is not an integer");
  }
  success.interval = std::get<bencode::Integer>(interval_it->second.data);

  auto peers_it = dict.find("peers");
  if (peers_it == dict.end()) {
    return std::unexpected("Missing required key 'peers' in tracker response");
  }

  if (std::holds_alternative<bencode::ByteString>(peers_it->second.data)) {
    const auto& compact_peers = std::get<bencode::ByteString>(peers_it->second.data);
    auto peers = parse_compact_peers(std::as_bytes(std::span(compact_peers)));
    if (!peers) {
      return std::unexpected(peers.error());
    }
    success.peers = std::move(*peers);
  } else if (std::holds_alternative<bencode::List>(peers_it->second.data)) {
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

  if (auto it = dict.find("peers6"); it != dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(it->second.data)) {
      const auto& compact_ipv6 = std::get<bencode::ByteString>(it->second.data);
      if (auto peers6 = parse_compact_peers6(std::as_bytes(std::span(compact_ipv6)))) {
        success.peers.insert(success.peers.end(), peers6->begin(), peers6->end());
      }
    }
  }

  return AnnounceResponse(std::move(success));
}

}
