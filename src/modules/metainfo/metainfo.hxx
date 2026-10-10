#pragma once


#include <bencode/bencode.hxx>
#include <cstdint>
#include <download/tracker.hxx>
#include <expected>
#include <filesystem>
#include <metainfo/error.hxx>
#include <string>

namespace metainfo {

struct File {
  std::filesystem::path path;
  uint64_t length;

  bool operator==(const File &other) const {
    return path == other.path && length == other.length;
  }
};

class Metainfo {
public:
  static std::expected<Metainfo, Error>
  parse(std::span<const std::byte> torrent_bytes);

  uint64_t length() const { return m_length; }
  uint64_t piece_length() const { return m_piece_length; }
  uint64_t num_pieces() const { return m_pieces.size() / 20; }
  const std::string &info_hash() const { return m_info_hash; }
  const std::vector<File>& files() const { return m_files; };
  std::span<const std::byte, 20> piece_hash(uint32_t index) const;
  const std::vector<download::Tracker> &trackers() const { return m_trackers; };

  std::expected<bencode::ByteString, metainfo::Error> name() const;

  std::expected<bencode::ByteString, metainfo::Error> pieces() const;

  std::expected<int64_t, metainfo::Error> total_size() const;

private:
  Metainfo() = default;

  uint64_t m_length{};
  uint64_t m_piece_length{};
  uint64_t m_piece_count{};
  std::string m_pieces;
  std::string m_info_hash;
  std::vector<File> m_files;
  std::vector<download::Tracker> m_trackers;
};


} // namespace metainfo
