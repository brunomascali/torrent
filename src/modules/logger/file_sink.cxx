#include <format>
#include <logger/file_sink.hxx>
#include <string_view>

namespace logger {

  FileSink::FileSink(const std::filesystem::path &path)
    : m_file(path) {}

  void FileSink::write(LogLevel level, std::string_view message) {
    m_file << std::format("[{}] {}\n", to_string(level), message);
    m_file.flush();
  }


}
