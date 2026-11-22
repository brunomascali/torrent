#pragma once

#include <cstdint>
#include <download/tracker/announce.hxx>
#include <download/tracker/timeouts.hxx>
#include <expected>
#include <string>

namespace download::tracker {

class TrackerClient {
public:
  explicit TrackerClient(std::string announce_url, TrackerTimeouts timeouts = {});

  std::expected<AnnounceResponse, std::string>
  announce(const AnnounceRequest &request) const;

  const std::string &url() const noexcept { return m_url; }

private:
  std::string m_url;
  TrackerTimeouts m_timeouts;
  std::uint32_t m_key;
};

}
