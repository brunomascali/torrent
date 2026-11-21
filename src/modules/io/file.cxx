#include <io/error.hxx>
#include <io/file.hxx>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace io {

namespace {

Error make_error(ErrC code, const std::filesystem::path &path) {
  Error e;
  e.code = code;
  e.context = path.string();
  return e;
}

}

std::expected<File, Error> File::open(const std::filesystem::path &path,
                                      OpenMode mode) {
  int flags = O_CLOEXEC;
  switch (mode) {
  case OpenMode::read_only:
    flags |= O_RDONLY;
    break;
  case OpenMode::read_write:
    flags |= O_RDWR;
    break;
  case OpenMode::create:
    flags |= O_RDWR | O_CREAT;
    break;
  }

  int fd;
  do {
    fd = ::open(path.c_str(), flags, 0644);
  } while (fd == -1 && errno == EINTR);

  if (fd == -1) {
    return std::unexpected(Error::from_errno(path.string()));
  }
  return File{fd, path};
}

File::File(int fd, std::filesystem::path path) noexcept
    : m_fd(fd), m_path(std::move(path)) {}

File::~File() { close(); }

File::File(File &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1)), m_path(std::move(other.m_path)) {}

File &File::operator=(File &&other) noexcept {
  if (this != &other) {
    close();
    m_fd = std::exchange(other.m_fd, -1);
    m_path = std::move(other.m_path);
  }
  return *this;
}

void File::close() noexcept {
  if (m_fd != -1) {
    ::close(m_fd);
    m_fd = -1;
  }
}

std::expected<std::size_t, Error>
File::read_at(std::uint64_t offset, std::span<std::byte> buffer) const {
  std::size_t total = 0;
  while (total < buffer.size()) {
    ssize_t n = ::pread(m_fd, buffer.data() + total, buffer.size() - total,
                        static_cast<off_t>(offset + total));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(Error::from_errno(m_path.string()));
    }
    if (n == 0) {
      break;
    }
    total += static_cast<std::size_t>(n);
  }
  return total;
}

std::expected<void, Error>
File::read_exact_at(std::uint64_t offset, std::span<std::byte> buffer) const {
  auto n = read_at(offset, buffer);
  if (!n) {
    return std::unexpected(std::move(n.error()));
  }
  if (*n != buffer.size()) {
    return std::unexpected(make_error(ErrC::unexpected_eof, m_path));
  }
  return {};
}

std::expected<void, Error>
File::write_all_at(std::uint64_t offset, std::span<const std::byte> buffer) {
  std::size_t total = 0;
  while (total < buffer.size()) {
    ssize_t n = ::pwrite(m_fd, buffer.data() + total, buffer.size() - total,
                         static_cast<off_t>(offset + total));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(Error::from_errno(m_path.string()));
    }
    if (n == 0) {
      return std::unexpected(make_error(ErrC::short_write, m_path));
    }
    total += static_cast<std::size_t>(n);
  }
  return {};
}

std::expected<std::uint64_t, Error> File::size() const {
  struct stat st;
  if (::fstat(m_fd, &st) == -1) {
    return std::unexpected(Error::from_errno(m_path.string()));
  }
  return static_cast<std::uint64_t>(st.st_size);
}

std::expected<void, Error> File::resize(std::uint64_t new_size) {
  if (::ftruncate(m_fd, static_cast<off_t>(new_size)) == -1) {
    return std::unexpected(Error::from_errno(m_path.string()));
  }
  return {};
}

std::expected<void, Error> File::sync() {
  if (::fsync(m_fd) == -1) {
    return std::unexpected(Error::from_errno(m_path.string()));
  }
  return {};
}

}
