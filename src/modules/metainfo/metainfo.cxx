#include <bencode/bencode.hxx>
#include <bencode/encode.hxx>
#include <boost/uuid/detail/sha1.hpp>
#include <cstdint>
#include <expected>
#include <metainfo/error.hxx>
#include <metainfo/metainfo.hxx>
#include <print>

namespace metainfo {
Metainfo::Metainfo(const bencode::Value &bencode) : m_bencode(bencode) {}

std::expected<std::string, metainfo::Error> Metainfo::announce() const {
  return get_field<bencode::ByteString>("announce");
}

std::expected<std::string, metainfo::Error> Metainfo::name() const {
  return get_info_field<bencode::ByteString>("name");
}

std::expected<bencode::Integer, metainfo::Error> Metainfo::length() const {
  return get_info_field<bencode::Integer>("length");
}

std::expected<int64_t, metainfo::Error> Metainfo::piece_length() const {
  return get_info_field<bencode::Integer>("piece length");
}

std::expected<bencode::ByteString, metainfo::Error> Metainfo::pieces() const {
  return get_info_field<bencode::ByteString>("pieces");
}

std::expected<std::vector<File>, metainfo::Error> Metainfo::files() const {
  std::vector<File> files;

  auto length = this->length();
  if (length.has_value()) {
    auto name_opt = this->name();
    if (!name_opt.has_value()) {
      return std::unexpected(
          metainfo::Error(MetainfoErrorCode::missing_key, "info.name"));
    }
    files.emplace_back(name_opt.value(), length.value());
    return files;
  }

  auto file_list = get_info_field<bencode::List>("files");
  if (!file_list) {
    return std::unexpected(
        metainfo::Error(MetainfoErrorCode::missing_key, "info.files"));
  }

  for (const bencode::Value &file_value : file_list.value()) {
    auto file_dict = std::get<bencode::Dict>(file_value.data);

    bencode::Integer file_length =
        std::get<bencode::Integer>(file_dict["length"].data);
    std::filesystem::path path;
    auto path_list = std::get<bencode::List>(file_dict["path"].data);
    for (const bencode::Value &path_item : path_list) {
      auto path_piece = std::get<bencode::ByteString>(path_item.data);
      path.append(path_piece);
    }
    files.emplace_back(path, file_length);

    std::println("{}", files.back().path.string());
  }
  return files;
}

std::expected<bencode::ByteString, metainfo::Error>
Metainfo::info_hash() const {
  if (!m_info_hash.has_value()) {
    auto info_result = get_field<bencode::Dict>("info");
    if (!info_result.has_value()) {
      return std::unexpected(info_result.error());
    }

    std::string info_str = bencode::encode(info_result.value());

    boost::uuids::detail::sha1 sha1;
    sha1.process_bytes(info_str.data(), info_str.size());

    unsigned char digest[20];
    sha1.get_digest(digest);

    bencode::ByteString hash_bytes;
    hash_bytes.reserve(20);
    for (unsigned int word : digest) {
      hash_bytes.push_back(static_cast<char>((word >> 24) & 0xFF));
      hash_bytes.push_back(static_cast<char>((word >> 16) & 0xFF));
      hash_bytes.push_back(static_cast<char>((word >> 8) & 0xFF));
      hash_bytes.push_back(static_cast<char>(word & 0xFF));
    }

    return hash_bytes;
  }

  return m_info_hash.value();
}

} // namespace metainfo
