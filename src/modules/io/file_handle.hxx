#pragma once

#include <cstddef>
#include <cstdint>
#include <error/error.hxx>
#include <expected>
#include <filesystem>
#include <io/error.hxx>
#include <span>

namespace io {
class FileHandle {
public:
  explicit FileHandle(const std::filesystem::path &path);
  ~FileHandle();

  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;
  FileHandle(FileHandle &&) noexcept;
  FileHandle &operator=(FileHandle &&) noexcept;

  [[nodiscard]] std::expected<void, io::Error>
  open(bool create_if_missing = true);
  void close();

  [[nodiscard]] std::expected<std::size_t, io::Error>
  read_at(std::uint64_t offset, std::span<std::byte> buffer);
  [[nodiscard]] std::expected<std::size_t, io::Error>
  write_at(std::uint64_t offset, std::span<const std::byte> buffer);

  [[nodiscard]] std::expected<std::uint64_t, io::Error> size() const;

  [[nodiscard]] std::expected<void, io::Error> flush();
  bool is_open();

private:
  int m_fd = -1;
  std::filesystem::path m_path;
};
} // namespace io
