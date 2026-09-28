#pragma once

#include <expected>
#include <string>

namespace download {

  struct HttpAnnounceRequest;
  struct HttpAnnounceResponse;

  class Tracker {
    public:
      Tracker();
      explicit Tracker(std::string url);

      std::expected<HttpAnnounceResponse, std::string> announce(HttpAnnounceRequest req);

      bool operator==(const Tracker& other) const {
        return m_url == other.m_url;
      }

    private:
      std::string m_url;

  };
}
