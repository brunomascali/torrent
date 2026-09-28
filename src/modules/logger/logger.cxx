#include <logger/logger.hxx>

namespace logger {

void Logger::set_sink(std::unique_ptr<LogSink> sink) {
  m_sink = std::move(sink);
}

void Logger::set_min_level(LogLevel level) { m_min_level = level; }

} // namespace logger
