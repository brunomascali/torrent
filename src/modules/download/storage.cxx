#include <download/storage.hxx>

#include <algorithm>
#include <format>
#include <system_error>

namespace download {

namespace {

io::Error make_error(std::error_code code, std::string context) {
  io::Error e;
  e.code = code;
  e.context = std::move(context);
  return e;
}

bool is_safe_relative(const std::filesystem::path &p) {
  if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) {
    return false;
  }
  return std::ranges::none_of(p, [](const auto &part) {
    return part == ".." || part == "." || part.empty();
  });
}

}

std::expected<Storage, io::Error>
Storage::open(const std::filesystem::path &root, std::vector<StorageFile> files) {
  std::vector<Entry> entries;
  entries.reserve(files.size());
  std::uint64_t offset = 0;

  for (auto &f : files) {
    if (!is_safe_relative(f.path)) {
      return std::unexpected(make_error(
          std::make_error_code(std::errc::invalid_argument),
          std::format("unsafe path in torrent: {}", f.path.string())));
    }

    const auto full = root / f.path;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    if (ec) {
      return std::unexpected(make_error(ec, full.parent_path().string()));
    }

    auto file = io::File::open(full, io::OpenMode::create);
    if (!file) {
      return std::unexpected(std::move(file.error()));
    }

    if (auto sized = file->resize(f.length); !sized) {
      return std::unexpected(std::move(sized.error()));
    }

    entries.push_back(Entry{std::move(*file), offset, f.length});
    offset += f.length;
  }

  return Storage(std::move(entries), offset);
}

template <class Self, class Fn>
std::expected<void, io::Error>
Storage::for_each_segment(Self &self, std::uint64_t offset, std::size_t size,
                          Fn &&fn) {
  const std::uint64_t total = self.m_total_length;
  if (offset > total || size > total - offset) {
    return std::unexpected(make_error(
        std::make_error_code(std::errc::invalid_argument),
        std::format("range [{}, {}) outside torrent of {} bytes", offset,
                    offset + size, total)));
  }

  auto it = std::ranges::upper_bound(
      self.m_entries, offset, {},
      [](const Entry &e) { return e.offset + e.length; });

  std::size_t done = 0;
  for (; done < size && it != self.m_entries.end(); ++it) {
    if (it->length == 0) {
      continue;
    }
    const std::uint64_t in_file = offset + done - it->offset;
    const std::size_t len = static_cast<std::size_t>(
        std::min<std::uint64_t>(size - done, it->length - in_file));
    if (auto r = fn(*it, in_file, done, len); !r) {
      return r;
    }
    done += len;
  }
  return {};
}

std::expected<void, io::Error> Storage::write(std::uint64_t offset,
                                              std::span<const std::byte> bytes) {
  return for_each_segment(
      *this, offset, bytes.size(),
      [&](Entry &e, std::uint64_t in_file, std::size_t start, std::size_t len) {
        return e.file.write_all_at(in_file, bytes.subspan(start, len));
      });
}

std::expected<void, io::Error> Storage::read(std::uint64_t offset,
                                             std::span<std::byte> buffer) const {
  return for_each_segment(
      *this, offset, buffer.size(),
      [&](const Entry &e, std::uint64_t in_file, std::size_t start, std::size_t len) {
        return e.file.read_exact_at(in_file, buffer.subspan(start, len));
      });
}

}
