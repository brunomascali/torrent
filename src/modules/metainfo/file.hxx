#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace metainfo {

struct File {
  std::filesystem::path path;
  uint64_t length;

  File(std::filesystem::path path, uint64_t length)
  : path(path), length(length) {}

  bool operator==(const File &other) const {
    return path == other.path && length == other.length;
  }
};

} // namespace metainfo
