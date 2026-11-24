#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace download {

class TransferRate {
public:
  using Clock = std::chrono::steady_clock;

  static constexpr std::size_t window_seconds = 5;

  explicit TransferRate(Clock::time_point start = Clock::now()) : m_start(start) {}

  void add(std::uint64_t bytes, Clock::time_point now = Clock::now());
  double bytes_per_second(Clock::time_point now = Clock::now()) const;

private:
  std::int64_t second_of(Clock::time_point now) const;
  void advance_to(std::int64_t second);

  Clock::time_point m_start;
  mutable std::mutex m_mutex;
  std::array<std::uint64_t, window_seconds + 1> m_buckets{};
  std::int64_t m_current_second{0};
};

std::string format_rate(double bytes_per_second);
std::string format_bytes(std::uint64_t bytes);

}
