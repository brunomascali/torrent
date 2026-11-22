#include <download/tracker/udp_tracker.hxx>

#include <boost/asio.hpp>
#include <charconv>
#include <format>
#include <logger/logger.hxx>
#include <optional>
#include <random>
#include <set>
#include <vector>

namespace download::tracker::udp {

namespace asio = boost::asio;
using asio::ip::udp;

namespace {

template <class T> void put_be(std::byte *out, T value) {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out[i] = static_cast<std::byte>(value >> (8 * (sizeof(T) - 1 - i)));
  }
}

template <class T> T get_be(std::span<const std::byte> in, std::size_t offset) {
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>((value << 8) | std::to_integer<T>(in[offset + i]));
  }
  return value;
}

std::uint32_t event_code(Event event) {
  switch (event) {
  case Event::none:
    return 0;
  case Event::completed:
    return 1;
  case Event::started:
    return 2;
  case Event::stopped:
    return 3;
  }
  return 0;
}

std::expected<void, std::string> check_header(std::span<const std::byte> packet,
                                              Action expected,
                                              std::uint32_t transaction_id,
                                              std::size_t min_size) {
  if (packet.size() < 8) {
    return std::unexpected(std::format("packet too short: {} bytes", packet.size()));
  }
  if (get_be<std::uint32_t>(packet, 4) != transaction_id) {
    return std::unexpected("transaction id mismatch");
  }
  const auto action = static_cast<Action>(get_be<std::uint32_t>(packet, 0));
  if (action == Action::error) {
    std::string message(reinterpret_cast<const char *>(packet.data() + 8), packet.size() - 8);
    return std::unexpected("tracker error: " + message);
  }
  if (action != expected) {
    return std::unexpected(
        std::format("unexpected action {}", static_cast<std::uint32_t>(action)));
  }
  if (packet.size() < min_size) {
    return std::unexpected(std::format("packet too short: {} bytes, need {}",
                                       packet.size(), min_size));
  }
  return {};
}

std::uint32_t random_u32() {
  static thread_local std::mt19937 gen{std::random_device{}()};
  return std::uniform_int_distribution<std::uint32_t>{}(gen);
}

class Exchange {
public:
  explicit Exchange(const TrackerTimeouts &timeouts) : m_timeouts(timeouts) {}

  std::expected<std::vector<asio::ip::address>, std::string>
  resolve(const Endpoint &endpoint) {
    std::optional<std::expected<udp::resolver::results_type, std::string>> resolved;
    udp::resolver resolver(m_ctx);
    resolver.async_resolve(endpoint.host, std::to_string(endpoint.port),
                           [&](const boost::system::error_code &ec,
                               udp::resolver::results_type results) {
                             if (ec) {
                               resolved = std::unexpected(ec.message());
                             } else {
                               resolved = std::move(results);
                             }
                           });
    run_for(m_timeouts.connect);
    if (!resolved) {
      resolver.cancel();
      drain();
      return std::unexpected(std::format("resolving {} timed out", endpoint.host));
    }
    if (!*resolved) {
      return std::unexpected(
          std::format("cannot resolve {}: {}", endpoint.host, resolved->error()));
    }

    std::optional<asio::ip::address> v4;
    std::optional<asio::ip::address> v6;
    for (const auto &entry : **resolved) {
      const auto address = entry.endpoint().address();
      if (address.is_v4() && !v4) {
        v4 = address;
      } else if (address.is_v6() && !v6) {
        v6 = address;
      }
    }

    std::vector<asio::ip::address> addresses;
    if (v4) {
      addresses.push_back(*v4);
    }
    if (v6) {
      addresses.push_back(*v6);
    }
    if (addresses.empty()) {
      return std::unexpected(std::format("no address for {}", endpoint.host));
    }
    return addresses;
  }

  std::expected<AddressFamily, std::string> open(const udp::endpoint &target) {
    boost::system::error_code ec;
    m_socket.open(target.protocol(), ec);
    if (!ec) {
      m_socket.connect(target, ec);
    }
    if (ec) {
      return std::unexpected(std::format("cannot open UDP socket: {}", ec.message()));
    }
    return target.address().is_v6() ? AddressFamily::v6 : AddressFamily::v4;
  }

  std::expected<std::vector<std::byte>, std::string>
  request(std::span<const std::byte> packet, std::uint32_t transaction_id) {
    for (int attempt = 0; attempt < m_timeouts.udp_attempts; ++attempt) {
      boost::system::error_code ec;
      m_socket.send(asio::buffer(packet.data(), packet.size()), 0, ec);
      if (ec) {
        return std::unexpected(std::format("send failed: {}", ec.message()));
      }

      const auto deadline = std::chrono::steady_clock::now() + m_timeouts.udp_response;
      while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
          break;
        }
        auto received = receive(remaining);
        if (!received) {
          return std::unexpected(received.error());
        }
        if (!*received) {
          break;
        }
        const auto &reply = **received;
        if (reply.size() >= 8 &&
            get_be<std::uint32_t>(reply, 4) == transaction_id) {
          return reply;
        }
      }
      logger::debug("UDP tracker did not answer attempt {}", attempt + 1);
    }
    return std::unexpected(std::format("no response after {} attempts",
                                       m_timeouts.udp_attempts));
  }

private:
  std::expected<std::optional<std::vector<std::byte>>, std::string>
  receive(std::chrono::steady_clock::duration timeout) {
    std::vector<std::byte> buffer(65536);
    std::optional<std::pair<boost::system::error_code, std::size_t>> result;
    m_socket.async_receive(asio::buffer(buffer),
                           [&](const boost::system::error_code &ec, std::size_t n) {
                             result.emplace(ec, n);
                           });
    run_for(timeout);
    if (!result) {
      m_socket.cancel();
      drain();
      return std::nullopt;
    }
    if (result->first) {
      return std::unexpected(std::format("receive failed: {}", result->first.message()));
    }
    buffer.resize(result->second);
    return buffer;
  }

  template <class Duration> void run_for(Duration timeout) {
    m_ctx.restart();
    m_ctx.run_for(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout));
  }

  void drain() {
    m_ctx.restart();
    m_ctx.run();
  }

  const TrackerTimeouts &m_timeouts;
  asio::io_context m_ctx;
  udp::socket m_socket{m_ctx};
};

}

std::expected<Endpoint, std::string> parse_url(std::string_view url) {
  constexpr std::string_view scheme = "udp://";
  if (!url.starts_with(scheme)) {
    return std::unexpected(std::format("not a udp:// URL: {}", url));
  }
  std::string_view rest = url.substr(scheme.size());
  rest = rest.substr(0, rest.find_first_of("/?"));

  std::string_view host;
  std::string_view port;
  if (rest.starts_with('[')) {
    const auto close = rest.find(']');
    if (close == std::string_view::npos || close + 1 >= rest.size() || rest[close + 1] != ':') {
      return std::unexpected(std::format("invalid udp tracker address: {}", url));
    }
    host = rest.substr(1, close - 1);
    port = rest.substr(close + 2);
  } else {
    const auto colon = rest.rfind(':');
    if (colon == std::string_view::npos) {
      return std::unexpected(std::format("udp tracker URL has no port: {}", url));
    }
    host = rest.substr(0, colon);
    port = rest.substr(colon + 1);
  }

  std::uint16_t port_number = 0;
  const auto [ptr, ec] = std::from_chars(port.data(), port.data() + port.size(), port_number);
  if (host.empty() || ec != std::errc{} || ptr != port.data() + port.size() || port_number == 0) {
    return std::unexpected(std::format("invalid udp tracker address: {}", url));
  }
  return Endpoint{std::string(host), port_number};
}

std::array<std::byte, 16> encode_connect(std::uint32_t transaction_id) {
  std::array<std::byte, 16> out{};
  put_be(out.data(), protocol_id);
  put_be(out.data() + 8, static_cast<std::uint32_t>(Action::connect));
  put_be(out.data() + 12, transaction_id);
  return out;
}

std::expected<std::uint64_t, std::string>
decode_connect(std::span<const std::byte> packet, std::uint32_t transaction_id) {
  if (auto ok = check_header(packet, Action::connect, transaction_id, 16); !ok) {
    return std::unexpected(ok.error());
  }
  return get_be<std::uint64_t>(packet, 8);
}

std::array<std::byte, 98> encode_announce(std::uint64_t connection_id,
                                          std::uint32_t transaction_id,
                                          const AnnounceRequest &request,
                                          std::uint32_t key) {
  std::uint32_t ip = 0;
  if (request.ip) {
    boost::system::error_code ec;
    const auto address = asio::ip::make_address_v4(*request.ip, ec);
    if (!ec) {
      ip = address.to_uint();
    }
  }

  std::array<std::byte, 98> out{};
  put_be(out.data(), connection_id);
  put_be(out.data() + 8, static_cast<std::uint32_t>(Action::announce));
  put_be(out.data() + 12, transaction_id);
  std::ranges::copy(request.info_hash, out.begin() + 16);
  std::ranges::copy(request.peer_id, out.begin() + 36);
  put_be(out.data() + 56, request.downloaded);
  put_be(out.data() + 64, request.left);
  put_be(out.data() + 72, request.uploaded);
  put_be(out.data() + 80, event_code(request.event));
  put_be(out.data() + 84, ip);
  put_be(out.data() + 88, key);
  put_be(out.data() + 92, static_cast<std::uint32_t>(-1));
  put_be(out.data() + 96, request.port);
  return out;
}

std::expected<AnnounceResponse, std::string>
decode_announce(std::span<const std::byte> packet, std::uint32_t transaction_id,
                AddressFamily family) {
  if (packet.size() >= 8 && get_be<std::uint32_t>(packet, 4) == transaction_id &&
      static_cast<Action>(get_be<std::uint32_t>(packet, 0)) == Action::error) {
    return AnnounceResponse(AnnounceFailure{
        .failure_reason = std::string(reinterpret_cast<const char *>(packet.data() + 8),
                                      packet.size() - 8),
        .warning_message = std::nullopt,
    });
  }
  if (auto ok = check_header(packet, Action::announce, transaction_id, 20); !ok) {
    return std::unexpected(ok.error());
  }

  const auto peer_bytes = packet.subspan(20);
  auto peers = family == AddressFamily::v6 ? parse_compact_peers6(peer_bytes)
                                           : parse_compact_peers(peer_bytes);
  if (!peers) {
    return std::unexpected(peers.error());
  }

  AnnounceSuccess success;
  success.interval = get_be<std::uint32_t>(packet, 8);
  success.incomplete = get_be<std::uint32_t>(packet, 12);
  success.complete = get_be<std::uint32_t>(packet, 16);
  success.peers = std::move(*peers);
  return AnnounceResponse(std::move(success));
}

namespace {

std::expected<AnnounceResponse, std::string>
announce_one(const udp::endpoint &target, const AnnounceRequest &request,
             const TrackerTimeouts &timeouts, std::uint32_t key) {
  Exchange exchange(timeouts);
  auto family = exchange.open(target);
  if (!family) {
    return std::unexpected(family.error());
  }

  const std::uint32_t connect_tid = random_u32();
  auto connect_reply = exchange.request(encode_connect(connect_tid), connect_tid);
  if (!connect_reply) {
    return std::unexpected("connect: " + connect_reply.error());
  }
  auto connection_id = decode_connect(*connect_reply, connect_tid);
  if (!connection_id) {
    return std::unexpected("connect: " + connection_id.error());
  }

  const std::uint32_t announce_tid = random_u32();
  auto announce_reply = exchange.request(
      encode_announce(*connection_id, announce_tid, request, key), announce_tid);
  if (!announce_reply) {
    return std::unexpected("announce: " + announce_reply.error());
  }
  return decode_announce(*announce_reply, announce_tid, *family);
}

void remove_duplicate_peers(std::vector<download::peer::Peer> &peers) {
  std::set<std::pair<std::string, std::uint16_t>> seen;
  std::erase_if(peers, [&](const download::peer::Peer &p) {
    return !seen.emplace(p.ip, p.port).second;
  });
}

}

std::expected<AnnounceResponse, std::string>
announce_addresses(std::span<const std::string> addresses, std::uint16_t port,
                   const AnnounceRequest &request, const TrackerTimeouts &timeouts,
                   std::uint32_t key) {
  std::optional<AnnounceSuccess> merged;
  std::optional<AnnounceFailure> failure;
  std::vector<std::string> errors;

  for (const auto &text : addresses) {
    boost::system::error_code ec;
    const auto address = asio::ip::make_address(text, ec);
    if (ec) {
      errors.push_back(std::format("{}: invalid address", text));
      continue;
    }

    auto result = announce_one(udp::endpoint(address, port), request, timeouts, key);
    if (!result) {
      logger::debug("UDP announce via {} failed: {}", text, result.error());
      errors.push_back(std::format("{}: {}", text, result.error()));
    } else if (const auto *refused = result->as_failure()) {
      if (!failure) {
        failure = *refused;
      }
    } else {
      const auto &success = *result->as_success();
      logger::debug("UDP announce via {} returned {} peers", text, success.peers.size());
      if (!merged) {
        merged = success;
      } else {
        merged->peers.insert(merged->peers.end(), success.peers.begin(),
                             success.peers.end());
      }
    }
  }

  if (merged) {
    remove_duplicate_peers(merged->peers);
    return AnnounceResponse(std::move(*merged));
  }
  if (failure) {
    return AnnounceResponse(std::move(*failure));
  }
  std::string message;
  for (const auto &e : errors) {
    message += message.empty() ? e : "; " + e;
  }
  return std::unexpected(message.empty() ? "no tracker address to announce to" : message);
}

std::expected<AnnounceResponse, std::string>
announce(std::string_view url, const AnnounceRequest &request,
         const TrackerTimeouts &timeouts, std::uint32_t key) {
  auto endpoint = parse_url(url);
  if (!endpoint) {
    return std::unexpected(endpoint.error());
  }
  logger::debug("Announcing to {}", url);

  auto resolved = Exchange(timeouts).resolve(*endpoint);
  if (!resolved) {
    return std::unexpected(resolved.error());
  }

  std::vector<std::string> addresses;
  for (const auto &address : *resolved) {
    addresses.push_back(address.to_string());
  }
  return announce_addresses(addresses, endpoint->port, request, timeouts, key);
}

}
