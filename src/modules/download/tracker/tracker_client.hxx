#pragma once

#include <download/tracker/announce.hxx>
#include <chrono>
#include <expected>
#include <string>

namespace download::tracker {

struct TrackerTimeouts {
  std::chrono::milliseconds connect{std::chrono::seconds(10)};
  std::chrono::milliseconds read{std::chrono::seconds(30)};
};

class TrackerClient {
public:
  explicit TrackerClient(std::string announce_url, TrackerTimeouts timeouts = {});

  std::expected<AnnounceResponse, std::string>
  announce(const AnnounceRequest &request) const;

  const std::string &url() const noexcept { return m_url; }

private:
  std::string m_url;
  TrackerTimeouts m_timeouts;
};

}
