#pragma once

#include <filesystem>

namespace metainfo {

struct File {
  std::filesystem::path path;
  uint64_t length;

  bool operator==(const File &other) const {
    return path == other.path && length == other.length;
  }
};

} // namespace metainfo
