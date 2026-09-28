#include "httplib/httplib.h"
#include <download/tracker.hxx>
#include <format>
#include <print>
#include <expected>
#include <download/http_announce.hxx>

namespace download {

  Tracker::Tracker() : m_url{} {}

  Tracker::Tracker(std::string url)
    : m_url(std::move(url)) {}

  std::expected<HttpAnnounceResponse, std::string> Tracker::announce(HttpAnnounceRequest req) {
    if (m_url.empty()) {
      return std::unexpected("Tracker does not have a url");
    }

    std::string encoded_params = req.encode_params();
    std::string url = std::format("{}{}", m_url, encoded_params);

    std::println("Announcing to {}", url);

    httplib::detail::UrlComponents uc;
    if (!httplib::detail::parse_url(url, uc)) {
      return std::unexpected("Invalid announce URL: " + url);
    }

    std::string scheme_host_port = (uc.scheme.empty() ? "http" : uc.scheme) + "://" + uc.host;
    if (!uc.port.empty()) {
      scheme_host_port += ":" + uc.port;
    }

    std::string path = uc.path.empty() ? "/" : uc.path;
    path += uc.query;

    httplib::Client cli(scheme_host_port);

    if (auto res = cli.Get(path)) {
      if (res->status == 200) {
        return HttpAnnounceResponse::parse(res->body);
      }
      return std::unexpected(std::format("HTTP Error Status {}: {}", res->status, res->body));
    } else {
      auto err = res.error();
      return std::unexpected("Connection Error: " + to_string(err));
    }

  }

}
