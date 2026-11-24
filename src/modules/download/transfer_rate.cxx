#include <download/transfer_rate.hxx>

#include <algorithm>
#include <format>

namespace download {

namespace {

constexpr std::size_t bucket_count = TransferRate::window_seconds + 1;
constexpr auto window = static_cast<std::int64_t>(TransferRate::window_seconds);

std::size_t bucket_of(std::int64_t second) {
  return static_cast<std::size_t>(second) % bucket_count;
}

}

std::int64_t TransferRate::second_of(Clock::time_point now) const {
  if (now <= m_start) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(now - m_start).count();
}

void TransferRate::advance_to(std::int64_t second) {
  if (second <= m_current_second) {
    return;
  }
  const auto stale = std::min<std::int64_t>(second - m_current_second, window + 1);
  for (std::int64_t i = 1; i <= stale; ++i) {
    m_buckets[bucket_of(m_current_second + i)] = 0;
  }
  m_current_second = second;
}

void TransferRate::add(std::uint64_t bytes, Clock::time_point now) {
  std::lock_guard lock(m_mutex);
  advance_to(second_of(now));
  m_buckets[bucket_of(m_current_second)] += bytes;
}

double TransferRate::bytes_per_second(Clock::time_point now) const {
  std::lock_guard lock(m_mutex);
  const std::int64_t second = std::max(second_of(now), m_current_second);
  const std::int64_t oldest = std::max<std::int64_t>(0, second - window);

  std::uint64_t sum = 0;
  for (std::int64_t s = std::max(oldest, m_current_second - window);
       s <= m_current_second; ++s) {
    sum += m_buckets[bucket_of(s)];
  }

  const auto window_start = m_start + std::chrono::seconds{oldest};
  const double elapsed = std::chrono::duration<double>(std::max(now, window_start) - window_start).count();
  if (elapsed <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(sum) / std::max(elapsed, 1.0);
}

std::string format_rate(double bytes_per_second) {
  return format_bytes(static_cast<std::uint64_t>(bytes_per_second)) + "/s";
}

std::string format_bytes(std::uint64_t bytes) {
  constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < units.size()) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0) {
    return std::format("{} B", bytes);
  }
  return std::format("{:.1f} {}", value, units[unit]);
}

}
