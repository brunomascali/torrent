#include "bencode/accessors.hxx"
#include "bencode/decode.hxx"
#include "download/tracker.hxx"
#include "logger/logger.hxx"
#include <bencode/bencode.hxx>
#include <bencode/encode.hxx>
#include <boost/uuid/detail/sha1.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <metainfo/error.hxx>
#include <metainfo/metainfo.hxx>
#include <print>
#include <variant>

namespace metainfo {

namespace {
std::string calculate_info_hash(const bencode::Dict &info) {
  std::string info_str = bencode::encode(info);

  boost::uuids::detail::sha1 sha1;
  sha1.process_bytes(info_str.data(), info_str.size());

  boost::uuids::detail::sha1::digest_type digest;
  sha1.get_digest(digest);

  bencode::ByteString hash_bytes;
  if constexpr (sizeof(digest) == 20 && sizeof(digest[0]) == 1) {
    hash_bytes.assign(reinterpret_cast<const char *>(digest), 20);
  } else {
    hash_bytes.reserve(20);
    for (unsigned int word : digest) {
      hash_bytes.push_back(static_cast<char>((word >> 24) & 0xFF));
      hash_bytes.push_back(static_cast<char>((word >> 16) & 0xFF));
      hash_bytes.push_back(static_cast<char>((word >> 8) & 0xFF));
      hash_bytes.push_back(static_cast<char>(word & 0xFF));
    }
  }

  return hash_bytes;
}

// Todo: handle multiple file download
std::vector<metainfo::File> extract_files(const bencode::Dict& info) {
  auto name = bencode::get<bencode::ByteString>(info, "name");
  if (!name) {
    logger::error("Multiple files torrent not supported yet");
    exit(1);
  }

  auto length = bencode::get<bencode::Integer>(info, "length");
  if (!length) {
    logger::error("Multiple files torrent not supported yet");
    exit(1);
  }

  std::filesystem::path path = std::string(*name);
  uint64_t file_length = static_cast<uint64_t>(*length);

  return {
    metainfo::File{path, file_length}
  };

}
} // namespace

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

  auto length = bencode::get<bencode::Integer>(*info_dict, "length");
  if (!length) {
    logger::error("Metainfo file does not have a 'info.length' integer (could be a multiple files download)", bencode.error().str());
    exit(1);
  }
  m.m_length = *length;

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
  m.m_pieces = std::move(*pieces);

  // todo: get all trackers from metainfo
  auto trackers = bencode::get<bencode::ByteString>(*root_dict, "announce");
  if (!trackers) {
    logger::error("Metainfo file does not have a 'announce' field", bencode.error().str());
    exit(1);
  }
  m.m_trackers = {download::Tracker(*trackers)};

  m.m_info_hash = calculate_info_hash(*info_dict);
  m.m_files = extract_files(*info_dict);

  return m;
}

} // namespace metainfo
