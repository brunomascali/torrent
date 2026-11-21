#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <io/error.hxx>
#include <io/file.hxx>
#include <span>
#include <vector>

namespace download {

struct StorageFile {
  std::filesystem::path path;
  std::uint64_t length;
};

class Storage {
public:
  [[nodiscard]] static std::expected<Storage, io::Error>
  open(const std::filesystem::path &root, std::vector<StorageFile> files);

  [[nodiscard]] std::expected<void, io::Error>
  write(std::uint64_t offset, std::span<const std::byte> bytes);

  [[nodiscard]] std::expected<void, io::Error>
  read(std::uint64_t offset, std::span<std::byte> buffer) const;

  std::uint64_t total_length() const noexcept { return m_total_length; }

private:
  struct Entry {
    io::File file;
    std::uint64_t offset;
    std::uint64_t length;
  };

  explicit Storage(std::vector<Entry> entries, std::uint64_t total_length)
      : m_entries(std::move(entries)), m_total_length(total_length) {}

  template <class Self, class Fn>
  static std::expected<void, io::Error>
  for_each_segment(Self &self, std::uint64_t offset, std::size_t size, Fn &&fn);

  std::vector<Entry> m_entries;
  std::uint64_t m_total_length;
};

}
