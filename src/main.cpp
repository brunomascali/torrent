#include <download/download_session.hxx>
#include <bencode/decode.hxx>
#include <io/file.hxx>
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
  auto handle = io::File::open(file, io::OpenMode::read_only);
  if (!handle) {
    logger::error("Error opening torrent file: {}", handle.error().str());
    exit(1);
  }

  auto file_size = handle->size().value();
  std::vector<std::byte> buffer(file_size, std::byte{0});
  if (auto read = handle->read_exact_at(0, buffer); !read) {
    logger::error("Error reading torrent file: {}", read.error().str());
    exit(1);
  }

  auto decode_res = bencode::decode(buffer);
  auto m_bencode = std::make_unique<bencode::Value>(decode_res.value());
  auto m_metainfo = std::make_unique<metainfo::Metainfo>(metainfo::Metainfo::parse(buffer).value());

  auto session = download::DownloadSession::create(std::move(m_metainfo));
  if (!session) {
    logger::error("Cannot start download: {}", session.error());
    exit(1);
  }
  if (!(*session)->run()) {
    exit(1);
  }

}
