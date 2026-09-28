#pragma once

#include <filesystem>
#include <fstream>
#include <logger/logger.hxx>

namespace logger {

class FileSink : public LogSink {
public:
  explicit FileSink(const std::filesystem::path &path);
  void write(LogLevel level, std::string_view message) override;

private:
  std::ofstream m_file;
};

} // namespace logger
