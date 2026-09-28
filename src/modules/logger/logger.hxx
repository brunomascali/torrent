#pragma once

#include <format>
#include <memory>
#include <string_view>

namespace logger {

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
};

constexpr std::string to_string(LogLevel level) {
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
}

class LogSink {
public:
  virtual ~LogSink() = default;
  virtual void write(LogLevel level, std::string_view message) = 0;
};

class Logger {
public:
  static Logger& instance() {
      static Logger logger;
      return logger;
  }

  // Todo: filter messages with level less than min_level
  void log(LogLevel level, std::string_view message) {
    m_sink->write(level, message);
  }

  void set_sink(std::unique_ptr<LogSink> sink);
  void set_min_level(LogLevel level);

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;

private:
  Logger() = default;

  std::unique_ptr<LogSink> m_sink;
  LogLevel m_min_level{LogLevel::Debug};
};

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args &&...args) {
  Logger::instance().log(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args &&...args) {
  Logger::instance().log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args &&...args) {
  Logger::instance().log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args &&...args) {
  Logger::instance().log(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args &&...args) {
  Logger::instance().log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace logger
