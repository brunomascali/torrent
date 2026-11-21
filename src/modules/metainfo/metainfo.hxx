#pragma once


#include <bencode/bencode.hxx>
#include <cstdint>
#include <expected>
#include <metainfo/error.hxx>
#include <string>
#include <metainfo/file.hxx>

namespace metainfo {

class Metainfo {
public:
  static std::expected<Metainfo, Error>
  parse(std::span<const std::byte> torrent_bytes);

  uint64_t length() const { return m_length; }
  uint64_t piece_length() const { return m_piece_length; }
  uint64_t num_pieces() const { return m_pieces.size(); }
  std::array<std::byte, 20> info_hash() const { return m_info_hash; }
  const std::vector<File>& files() const { return m_files; };
  std::span<const std::byte, 20> pieces() const;
  std::span<const std::byte, 20> piece_hash(uint32_t index) const;
  const std::vector<std::string> &announce_urls() const { return m_announce_urls; };

  std::expected<bencode::ByteString, metainfo::Error> name() const;

  std::expected<int64_t, metainfo::Error> total_size() const;

private:
  Metainfo() = default;

  uint64_t m_length{};
  uint64_t m_piece_length{};
  uint64_t m_piece_count{};
  std::vector<std::array<std::byte, 20>> m_pieces;
  std::array<std::byte, 20> m_info_hash;
  std::vector<File> m_files;
  std::vector<std::string> m_announce_urls;
};


}
