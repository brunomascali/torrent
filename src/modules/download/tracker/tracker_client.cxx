#include <download/tracker/tracker_client.hxx>
#include <download/tracker/http_tracker.hxx>
#include <download/tracker/udp_tracker.hxx>

#include <format>
#include <random>

namespace download::tracker {

TrackerClient::TrackerClient(std::string announce_url, TrackerTimeouts timeouts)
    : m_url(std::move(announce_url)), m_timeouts(timeouts),
      m_key(static_cast<std::uint32_t>(std::random_device{}())) {}

std::expected<AnnounceResponse, std::string>
TrackerClient::announce(const AnnounceRequest &request) const {
  if (m_url.empty()) {
    return std::unexpected("tracker has no announce URL");
  }
  if (m_url.starts_with("udp://")) {
    return udp::announce(m_url, request, m_timeouts, m_key);
  }
  if (m_url.starts_with("http://") || m_url.starts_with("https://")) {
    return http::announce(m_url, request, m_timeouts);
  }
  return std::unexpected(std::format("unsupported tracker URL scheme: {}", m_url));
}

}
