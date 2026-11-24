#include "bencode/accessors.hxx"
#include "bencode/decode.hxx"
#include <algorithm>
#include <crypto/sha1.hxx>
#include "logger/logger.hxx"
#include "metainfo/file.hxx"
#include <bencode/bencode.hxx>
#include <bencode/encode.hxx>
#include <boost/uuid/detail/sha1.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <metainfo/error.hxx>
#include <metainfo/metainfo.hxx>
#include <span>
#include <variant>

namespace metainfo {

namespace {
Error invalid(std::string detail) {
  return Error(MetainfoErrorCode::invalid_value, std::move(detail));
}

std::expected<std::vector<metainfo::File>, Error>
extract_files(const bencode::Dict &info) {
  auto name = bencode::get<bencode::ByteString>(info, "name");
  if (!name) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key, "info.name"));
  }
  const std::filesystem::path root = std::string(*name);

  std::vector<metainfo::File> files;

  if (auto length = bencode::get<bencode::Integer>(info, "length")) {
    if (*length < 0) {
      return std::unexpected(invalid("info.length is negative"));
    }
    files.emplace_back(root, static_cast<uint64_t>(*length));
    return files;
  }

  auto list = bencode::get<bencode::List>(info, "files");
  if (!list) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key,
                                 "info.length or info.files"));
  }
  if (list->get().empty()) {
    return std::unexpected(invalid("info.files is empty"));
  }

  for (std::size_t i = 0; i < list->get().size(); ++i) {
    const auto *entry = std::get_if<bencode::Dict>(&list->get()[i].data);
    if (!entry) {
      return std::unexpected(invalid(std::format("info.files[{}] is not a dictionary", i)));
    }

    auto length = bencode::get<bencode::Integer>(*entry, "length");
    if (!length || *length < 0) {
      return std::unexpected(invalid(std::format("info.files[{}].length", i)));
    }

    auto parts = bencode::get<bencode::List>(*entry, "path");
    if (!parts || parts->get().empty()) {
      return std::unexpected(invalid(std::format("info.files[{}].path", i)));
    }

    std::filesystem::path path = root;
    for (const auto &part : parts->get()) {
      const auto *component = std::get_if<bencode::ByteString>(&part.data);
      if (!component) {
        return std::unexpected(
            invalid(std::format("info.files[{}].path has a non-string component", i)));
      }
      path /= *component;
    }

    files.emplace_back(std::move(path), static_cast<uint64_t>(*length));
  }

  return files;
}
}

std::expected<Metainfo, Error> Metainfo::parse(std::span<const std::byte> torrent_bytes) {
  Metainfo m;

  auto bencode = bencode::decode(torrent_bytes);
  if (!bencode) {
    return std::unexpected(invalid(std::format("not valid bencode: {}", bencode.error().str())));
  }

  const auto *root_dict = std::get_if<bencode::Dict>(&bencode->data);
  if (!root_dict) {
    return std::unexpected(invalid("top level is not a dictionary"));
  }

  auto info_it = root_dict->find("info");
  if (info_it == root_dict->end()) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key, "info"));
  }
  const bencode::Value &info_value = info_it->second;
  const auto *info_dict = std::get_if<bencode::Dict>(&info_value.data);
  if (!info_dict) {
    return std::unexpected(invalid("info is not a dictionary"));
  }

  auto piece_length = bencode::get<bencode::Integer>(*info_dict, "piece length");
  if (!piece_length) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key, "info.piece length"));
  }
  if (*piece_length <= 0) {
    return std::unexpected(invalid("info.piece length must be positive"));
  }
  m.m_piece_length = *piece_length;

  auto pieces = bencode::get<bencode::ByteString>(*info_dict, "pieces");
  if (!pieces) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key, "info.pieces"));
  }
  std::string_view pieces_view = pieces.value().get();
  if (pieces_view.empty() || pieces_view.size() % 20 != 0) {
    return std::unexpected(invalid("info.pieces length is not a positive multiple of 20"));
  }
  m.m_pieces.reserve(pieces_view.size() / 20);

  for (std::size_t i = 0; i < pieces_view.size(); i += 20) {
    std::array<std::byte, 20> bytes;
    std::ranges::transform(pieces_view.substr(i, 20), bytes.begin(),
                           [](char c) { return static_cast<std::byte>(c); });
    m.m_pieces.push_back(bytes);
  }

  auto trackers = bencode::get<bencode::ByteString>(*root_dict, "announce");
  if (!trackers) {
    return std::unexpected(Error(MetainfoErrorCode::missing_key, "announce"));
  }
  m.m_announce_urls = {std::string(*trackers)};

  m.m_info_hash = crypto::sha1({torrent_bytes.begin() + info_value.start,
                                torrent_bytes.begin() + info_value.end + 1});

  auto files = extract_files(*info_dict);
  if (!files) {
    return std::unexpected(files.error());
  }
  m.m_files = std::move(*files);
  m.m_length = 0;
  for (const auto &file : m.m_files) {
    m.m_length += file.length;
  }

  return m;
}

std::span<const std::byte, 20> Metainfo::piece_hash(uint32_t index) const {
  return m_pieces[index];
}

}
