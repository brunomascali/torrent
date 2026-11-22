#pragma once

#include <filesystem>
#include <fstream>
#include <logger/logger.hxx>

namespace logger {

class FileSink : public LogSink {
public:
  explicit FileSink(const std::filesystem::path &path);
  void write(const LogRecord &record) override;

  bool is_open() const { return m_file.is_open(); }

private:
  std::ofstream m_file;
};

}
