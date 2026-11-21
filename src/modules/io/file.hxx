#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <io/error.hxx>
#include <span>

namespace io {

enum class OpenMode {
  read_only,
  read_write,
  create,
};

class File {
public:
  [[nodiscard]] static std::expected<File, Error>
  open(const std::filesystem::path &path, OpenMode mode);

  ~File();

  File(const File &) = delete;
  File &operator=(const File &) = delete;
  File(File &&other) noexcept;
  File &operator=(File &&other) noexcept;

  [[nodiscard]] std::expected<std::size_t, Error>
  read_at(std::uint64_t offset, std::span<std::byte> buffer) const;

  [[nodiscard]] std::expected<void, Error>
  read_exact_at(std::uint64_t offset, std::span<std::byte> buffer) const;

  [[nodiscard]] std::expected<void, Error>
  write_all_at(std::uint64_t offset, std::span<const std::byte> buffer);

  [[nodiscard]] std::expected<std::uint64_t, Error> size() const;
  [[nodiscard]] std::expected<void, Error> resize(std::uint64_t new_size);
  [[nodiscard]] std::expected<void, Error> sync();

  const std::filesystem::path &path() const noexcept { return m_path; }

private:
  File(int fd, std::filesystem::path path) noexcept;
  void close() noexcept;

  int m_fd;
  std::filesystem::path m_path;
};

}
