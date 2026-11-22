#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace logger {

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
};

constexpr std::string_view to_string(LogLevel level) {
  switch (level) {
  case LogLevel::Trace:
    return "Trace";
  case LogLevel::Debug:
    return "Debug";
  case LogLevel::Info:
    return "Info";
  case LogLevel::Warn:
    return "Warn";
  case LogLevel::Error:
    return "Error";
  }
  return "?";
}

struct LogRecord {
  LogLevel level;
  std::chrono::system_clock::time_point time;
  unsigned thread;
  std::string_view message;
};

std::string format_record(const LogRecord &record);

unsigned current_thread_number();

class LogSink {
public:
  virtual ~LogSink() = default;
  virtual void write(const LogRecord &record) = 0;
};

class Logger {
public:
  static Logger &instance() {
    static Logger logger;
    return logger;
  }

  bool enabled(LogLevel level) const noexcept {
    return m_has_sink.load(std::memory_order_relaxed) &&
           level >= m_min_level.load(std::memory_order_relaxed);
  }

  void log(LogLevel level, std::string_view message);

  void set_sink(std::unique_ptr<LogSink> sink);
  void set_min_level(LogLevel level);
  LogLevel min_level() const noexcept;

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;

private:
  Logger() = default;

  std::mutex m_mutex;
  std::unique_ptr<LogSink> m_sink;
  std::atomic<bool> m_has_sink{false};
  std::atomic<LogLevel> m_min_level{LogLevel::Debug};
};

template <typename... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args &&...args) {
  auto &logger = Logger::instance();
  if (logger.enabled(level)) {
    logger.log(level, std::format(fmt, std::forward<Args>(args)...));
  }
}

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args &&...args) {
  log(LogLevel::Trace, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args &&...args) {
  log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args &&...args) {
  log(LogLevel::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args &&...args) {
  log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args &&...args) {
  log(LogLevel::Error, fmt, std::forward<Args>(args)...);
}

}
