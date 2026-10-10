#include "download/download_manager.hxx"
#include <bencode/decode.hxx>
#include <download/http_announce.hxx>
#include <io/file_handle.hxx>
#include <metainfo/metainfo.hxx>
#include <download/peer/peer_connection.hxx>
#include <logger/logger.hxx>
#include <logger/file_sink.hxx>
#include <memory>
#include <print>

int main(int argc, char** argv) {
  logger::Logger::instance().set_sink(std::make_unique<logger::FileSink>("log.txt"));

  if (argc == 1) {
    logger::error("missing .torrent file");
    exit(1);
  }

  auto file = argv[1];
  auto handle = io::FileHandle(file);
  handle.open();

  auto file_size = handle.size().value();
  std::vector<std::byte> buffer(file_size, std::byte{0});
  auto bytes_read = handle.read_at(0, buffer);

  auto decode_res = bencode::decode(buffer);
  auto m_bencode = std::make_unique<bencode::Value>(decode_res.value());
  auto m_metainfo = std::make_unique<metainfo::Metainfo>(metainfo::Metainfo::parse(buffer).value());

  auto manager = download::DownloadManager(std::move(m_metainfo));
  manager.start_download();

//   auto m_ubuntu_tracker = std::make_unique<download::Tracker>(m_metainfo->announce().value());
//
//   download::HttpAnnounceRequest req{
//     .info_hash = std::string(
//         "\xDA" "\xFC" "\x8C" "\x07" "l" "\xA2" "\xF3" "\xED" "7" "n"
//         "\xEA" "\xE7" "\xC7" "j" "\x0D" "k" "\xE2" "A" "\\" "E",
//         20),
//     .peer_id = "-TR2940-123456789012",
//     .ip = std::nullopt,
//     .port = 6881,
//     .uploaded = 0,
//     .downloaded = 0,
//     .left = 1000,
//   };
//
//   std::expected<download::HttpAnnounceResponse, std::string> response = m_ubuntu_tracker->announce(req);
//   if (response->is_failure()) {
//     std::println("Failed to announce: ", response.value().as_failure()->failure_reason);
//     return 1;
//   }
//   auto peers = response->as_success()->peers;
//   if (peers.size() == 0) {
//     std::println("empty peers list");
//     return 0;
//   }
//   auto first_peer = response->as_success()->peers.front();
//   std::println("CONNECTING TO PEER");
//
//   auto connection = download::peer::PeerConnection(first_peer, req.info_hash);
//
//   connection.start_communication();
//
//   auto msg = connection.receive_message();
//   auto bitfield = std::get<download::peer::Bitfield>(msg);
//   std::println("Got a bitfield of size: {}", bitfield.mask.size());
}
