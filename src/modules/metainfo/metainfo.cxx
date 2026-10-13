#include "bencode/accessors.hxx"
#include "bencode/decode.hxx"
#include <algorithm>
#include <crypto/sha1.hxx>
#include "download/tracker.hxx"
#include "logger/logger.hxx"
#include <bencode/bencode.hxx>
#include <bencode/encode.hxx>
#include <boost/uuid/detail/sha1.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <metainfo/error.hxx>
#include <metainfo/metainfo.hxx>
#include <span>
#include <variant>
#include <ranges>

namespace metainfo {

namespace {
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

  // todo: get all trackers from metainfo
  auto trackers = bencode::get<bencode::ByteString>(*root_dict, "announce");
  if (!trackers) {
    logger::error("Metainfo file does not have a 'announce' field", bencode.error().str());
    exit(1);
  }
  m.m_trackers = {download::Tracker(*trackers)};

  const bencode::Value v = root_dict->at("info");
  logger::trace("{} -> {}", v.start, v.end);
  m.m_info_hash = crypto::sha1({torrent_bytes.begin() + v.start, torrent_bytes.begin() + v.end + 1});
  m.m_files = extract_files(*info_dict);

  return m;
}

std::span<const std::byte, 20> Metainfo::piece_hash(uint32_t index) const {
  return m_pieces[index];
}

} // namespace metainfo
