#include <download/tracker/tracker_client.hxx>

#include <format>
#include <httplib/httplib.h>
#include <logger/logger.hxx>

namespace download::tracker {

TrackerClient::TrackerClient(std::string announce_url, TrackerTimeouts timeouts)
    : m_url(std::move(announce_url)), m_timeouts(timeouts) {}

std::expected<AnnounceResponse, std::string>
TrackerClient::announce(const AnnounceRequest &request) const {
  if (m_url.empty()) {
    return std::unexpected("tracker has no announce URL");
  }

  const char separator = m_url.find('?') == std::string::npos ? '?' : '&';
  const std::string url = std::format("{}{}{}", m_url, separator, request.query_string());
  logger::debug("Announcing to {}", url);

  httplib::detail::UrlComponents uc;
  if (!httplib::detail::parse_url(url, uc)) {
    return std::unexpected("invalid announce URL: " + m_url);
  }

  std::string scheme_host_port = (uc.scheme.empty() ? "http" : uc.scheme) + "://" + uc.host;
  if (!uc.port.empty()) {
    scheme_host_port += ":" + uc.port;
  }
  std::string path = (uc.path.empty() ? "/" : uc.path) + uc.query;

  httplib::Client cli(scheme_host_port);
  cli.set_connection_timeout(m_timeouts.connect);
  cli.set_read_timeout(m_timeouts.read);
  auto res = cli.Get(path);
  if (!res) {
    return std::unexpected("connection error: " + httplib::to_string(res.error()));
  }
  if (res->status != 200) {
    return std::unexpected(std::format("HTTP {}: {}", res->status, res->body));
  }
  return AnnounceResponse::parse(res->body);
}

}
