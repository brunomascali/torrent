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
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <metainfo/error.hxx>
#include <metainfo/metainfo.hxx>
#include <span>
#include <variant>
#include <optional>

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
    logger::error("Error decoding metainfo", bencode.error().str());
    exit(1);
  }

  bencode::Dict* root_dict = std::get_if<bencode::Dict>(&bencode.value().data);
  if (!root_dict) {
    logger::error("Metainfo file is not a dictionary", bencode.error().str());
    exit(1);
  }

  bencode::Dict* info_dict = std::get_if<bencode::Dict>(&(root_dict->at("info").data));
  if (!info_dict) {
    logger::error("Metainfo file does not have a 'info' dictionary", bencode.error().str());
    exit(1);
  }

  auto piece_length = bencode::get<bencode::Integer>(*info_dict, "piece length");
  if (!piece_length) {
    logger::error("Metainfo file does not have a 'info.piece length' integer (could be a multiple files download)", bencode.error().str());
    exit(1);
  }
  m.m_piece_length = *piece_length;

  auto pieces = bencode::get<bencode::ByteString>(*info_dict, "pieces");
  if (!pieces) {
    logger::error("Metainfo file does not have a 'info.pieces' integer", bencode.error().str());
    exit(1);
  }
  std::string_view pieces_view = pieces.value().get();
  if (pieces_view.size() % 20 != 0) {
    logger::error("Pieces length is not a multiple of 20", bencode.error().str());
    exit(1);
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
    logger::error("Metainfo file does not have a 'announce' field", bencode.error().str());
    exit(1);
  }
  m.m_announce_urls = {std::string(*trackers)};

  const bencode::Value v = root_dict->at("info");
  logger::trace("{} -> {}", v.start, v.end);
  m.m_info_hash = crypto::sha1({torrent_bytes.begin() + v.start, torrent_bytes.begin() + v.end + 1});

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
