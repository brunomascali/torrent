#include <logger/file_sink.hxx>

namespace logger {

FileSink::FileSink(const std::filesystem::path &path) : m_file(path) {}

void FileSink::write(const LogRecord &record) {
  m_file << format_record(record);
  m_file.flush();
}

}
