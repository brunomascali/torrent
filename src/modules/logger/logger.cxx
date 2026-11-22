#include <logger/logger.hxx>

#include <ctime>

namespace logger {

unsigned current_thread_number() {
  static std::atomic<unsigned> next{1};
  thread_local const unsigned number = next.fetch_add(1, std::memory_order_relaxed);
  return number;
}

std::string format_record(const LogRecord &record) {
  using namespace std::chrono;
  const auto since_epoch = record.time.time_since_epoch();
  const std::time_t seconds = duration_cast<std::chrono::seconds>(since_epoch).count();
  const auto millis = duration_cast<milliseconds>(since_epoch).count() % 1000;

  std::tm local{};
  localtime_r(&seconds, &local);

  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} T{:<2} {:<5} {}\n",
                     local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                     local.tm_hour, local.tm_min, local.tm_sec, millis, record.thread,
                     to_string(record.level), record.message);
}

void Logger::log(LogLevel level, std::string_view message) {
  if (!enabled(level)) {
    return;
  }
  const LogRecord record{
      .level = level,
      .time = std::chrono::system_clock::now(),
      .thread = current_thread_number(),
      .message = message,
  };

  std::lock_guard lock(m_mutex);
  if (m_sink) {
    m_sink->write(record);
  }
}

void Logger::set_sink(std::unique_ptr<LogSink> sink) {
  std::lock_guard lock(m_mutex);
  m_sink = std::move(sink);
  m_has_sink.store(m_sink != nullptr, std::memory_order_relaxed);
}

void Logger::set_min_level(LogLevel level) {
  m_min_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::min_level() const noexcept {
  return m_min_level.load(std::memory_order_relaxed);
}

}
