#include <filesystem>
#include <io/error.hxx>
#include <io/file_handle.hxx>

#include <cerrno>
#include <fcntl.h>
#include <format>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace io {

FileHandle::FileHandle(const std::filesystem::path &path) : m_path(path) {}

FileHandle::~FileHandle() { close(); }

FileHandle::FileHandle(FileHandle &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1)), m_path(other.m_path) {}

FileHandle &FileHandle::operator=(FileHandle &&other) noexcept {
  if (this != &other) {
    close();
    m_fd = std::exchange(other.m_fd, -1);
    m_path = std::move(other.m_path);
  }
  return *this;
}

[[nodiscard]] std::expected<void, io::Error>
FileHandle::open(bool create_if_missing) {
  if (m_fd != -1) {
    return {};
  }

  int flags = O_RDWR;
  if (create_if_missing) {
    flags |= O_CREAT;
  }

  // 0644: owner read/write, group/other read
  m_fd = ::open(m_path.c_str(), flags, 0644);

  if (m_fd == -1) {
    return std::unexpected(Error::errno_to_error(m_path.string()));
  }
  return {};
}

void FileHandle::close() {
  if (m_fd != -1) {
    ::close(m_fd);
    m_fd = -1;
  }
}

std::expected<std::uint64_t, io::Error> FileHandle::size() const {
  if (m_fd == -1) {
    return std::unexpected(
        Error{io::IOErrorCode::read_failure, m_path.string() + ": not open"});
  }

  struct stat stat_buf;

  if (::fstat(m_fd, &stat_buf) == -1) {
    return std::unexpected(Error::errno_to_error(m_path.string()));
  }

  return static_cast<std::uint64_t>(stat_buf.st_size);
}

std::expected<void, io::Error> FileHandle::flush() {
  if (m_fd == -1) {
    return std::unexpected(
        Error{io::IOErrorCode::flush_failure, m_path.string() + ": not open"});
  }

  // fsync asks the OS kernel to flush all pending in-memory modifications
  // for this specific file descriptor down to the actual hardware.
  if (::fsync(m_fd) == -1) {
    return std::unexpected(Error::errno_to_error(m_path.string()));
  }

  return {};
}

bool FileHandle::is_open() { return m_fd != -1; }

std::expected<std::size_t, io::Error>
FileHandle::read_at(std::uint64_t offset, std::span<std::byte> buffer) {
  if (m_fd == -1) {
    return std::unexpected(Error(IOErrorCode::read_failure,
                                 std::format("{} not open", m_path.string())));
  }

  std::size_t total_read = 0;
  while (total_read < buffer.size()) {
    ssize_t n =
        ::pread(m_fd, buffer.data() + total_read, buffer.size() - total_read,
                static_cast<off_t>(offset + total_read));

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(Error::errno_to_error(m_path.string()));
    }
    if (n == 0) {
      break;
    }

    total_read += static_cast<std::size_t>(n);
  }

  return total_read;
}

std::expected<std::size_t, io::Error>
FileHandle::write_at(std::uint64_t offset, std::span<const std::byte> buffer) {
  if (m_fd == -1) {
    return std::unexpected(Error(IOErrorCode::read_failure,
                                 std::format("{} not open", m_path.string())));
  }

  std::size_t total_written = 0;
  while (total_written < buffer.size()) {
    ssize_t n = ::pwrite(m_fd, buffer.data() + total_written,
                         buffer.size() - total_written,
                         static_cast<off_t>(offset + total_written));

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(Error::errno_to_error(m_path.string()));
    }

    if (n == 0) {
      break;
    }

    total_written += static_cast<std::size_t>(n);
  }

  return total_written;
}
} // namespace io
