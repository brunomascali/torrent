#pragma once

#include "download/http_announce.hxx"
#include <string>

namespace download {

  class Tracker {
    public:
      Tracker();
      explicit Tracker(std::string url);

      std::expected<HttpAnnounceResponse, std::string> announce(const HttpAnnounceRequest& req);

      bool operator==(const Tracker& other) const {
        return m_url == other.m_url;
      }

    private:
      std::string m_url;

  };
}
