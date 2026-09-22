#pragma once

#include <download/tracker.hxx>
#include <bencode/bencode.hxx>
#include <filesystem>
#include <metainfo/error.hxx>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace metainfo {

struct File {
  std::filesystem::path path;
  int64_t length;

  bool operator==(const File& other) const {
    return path == other.path && length == other.length;
  }
};

class Metainfo {
public:
  explicit Metainfo(const bencode::Value &bencode);

  std::expected<download::Tracker, metainfo::Error> announce() const;

  std::expected<bencode::ByteString, metainfo::Error> name() const;

  std::expected<bencode::Integer, metainfo::Error> length() const;

  std::expected<bencode::Integer, metainfo::Error> piece_length() const;

  std::expected<bencode::ByteString, metainfo::Error> pieces() const;

  std::expected<std::vector<File>, metainfo::Error> files() const;

  std::expected<int64_t, metainfo::Error> total_size() const;

  std::expected<std::string, metainfo::Error> info_hash() const;

private:
  const bencode::Value &m_bencode;
  std::optional<bencode::ByteString> m_info_hash;

  // Todo: return references
  template <typename BencodeType>
  std::expected<BencodeType, metainfo::Error>
  get_field(const std::string &key) const {
    auto bencode = std::get<bencode::Dict>(m_bencode.data);

    if (!bencode.contains(key)) {
      return std::unexpected(
          metainfo::Error(MetainfoErrorCode::missing_key, key));
    }

    return std::get<BencodeType>(bencode.at(key).data);
  }

  template <typename BencodeType>
  std::expected<BencodeType, metainfo::Error>
  get_info_field(const std::string &key) const {
    auto info_dict_expected = get_field<bencode::Dict>("info");

    if (!info_dict_expected.has_value()) {
      return std::unexpected(info_dict_expected.error());
    }

    const auto &info_dict = info_dict_expected.value();

    if (!info_dict.contains(key)) {
      return std::unexpected(
          metainfo::Error(MetainfoErrorCode::missing_key, "info." + key));
    }

    return std::get<BencodeType>(info_dict.at(key).data);
  }
};
} // namespace metainfo
