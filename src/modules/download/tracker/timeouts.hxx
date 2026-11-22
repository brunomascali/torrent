#pragma once

#include <chrono>

namespace download::tracker {

struct TrackerTimeouts {
  std::chrono::milliseconds connect{std::chrono::seconds(10)};
  std::chrono::milliseconds read{std::chrono::seconds(30)};
  std::chrono::milliseconds udp_response{std::chrono::seconds(5)};
  int udp_attempts{3};
};

}
