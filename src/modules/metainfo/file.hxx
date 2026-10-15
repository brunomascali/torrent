#pragma once

#include <cstdint>
#include <io/file_handle.hxx>
#include <filesystem>
#include <optional>

namespace metainfo {

struct File {
  std::filesystem::path path;
  uint64_t length;
  mutable io::FileHandle handle;

  File(std::filesystem::path path, uint64_t length)
  : path(path), length(length), handle(path) {}

  bool operator==(const File &other) const {
    return path == other.path && length == other.length;
  }
};

} // namespace metainfo
