#pragma once

#include <download/tracker/announce.hxx>
#include <download/tracker/timeouts.hxx>
#include <expected>
#include <string>
#include <string_view>

namespace download::tracker::http {

std::expected<AnnounceResponse, std::string>
announce(std::string_view url, const AnnounceRequest &request,
         const TrackerTimeouts &timeouts);

}
