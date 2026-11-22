#include <download/tracker/http_tracker.hxx>

#include <format>
#include <httplib/httplib.h>
#include <logger/logger.hxx>

namespace download::tracker::http {

std::expected<AnnounceResponse, std::string>
announce(std::string_view announce_url, const AnnounceRequest &request,
         const TrackerTimeouts &timeouts) {
  const char separator = announce_url.find('?') == std::string_view::npos ? '?' : '&';
  const std::string url =
      std::format("{}{}{}", announce_url, separator, request.query_string());
  logger::debug("Announcing to {}", url);

  httplib::detail::UrlComponents uc;
  if (!httplib::detail::parse_url(url, uc)) {
    return std::unexpected(std::format("invalid announce URL: {}", announce_url));
  }

  std::string scheme_host_port = (uc.scheme.empty() ? "http" : uc.scheme) + "://" + uc.host;
  if (!uc.port.empty()) {
    scheme_host_port += ":" + uc.port;
  }
  std::string path = (uc.path.empty() ? "/" : uc.path) + uc.query;

  httplib::Client cli(scheme_host_port);
  cli.set_connection_timeout(timeouts.connect);
  cli.set_read_timeout(timeouts.read);
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
