#include <download/download_session.hxx>
#include <io/file.hxx>
#include <metainfo/metainfo.hxx>
#include <download/peer/peer_connection.hxx>
#include <logger/logger.hxx>
#include <logger/file_sink.hxx>
#include <memory>
#include <cstdio>
#include <print>
#include <unistd.h>

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

  auto metainfo = metainfo::Metainfo::parse(buffer);
  if (!metainfo) {
    logger::error("Invalid torrent file: {}", metainfo.error().str());
    std::println(stderr, "Invalid torrent file: {}", metainfo.error().str());
    exit(1);
  }

  auto session = download::DownloadSession::create(
      std::make_unique<metainfo::Metainfo>(std::move(*metainfo)));
  if (!session) {
    logger::error("Cannot start download: {}", session.error());
    std::println(stderr, "Cannot start download: {}", session.error());
    exit(1);
  }

  const bool live = isatty(fileno(stdout));
  (*session)->set_progress_handler([live](const download::TransferStats &stats) {
    if (live) {
      std::print("\r\x1b[2K{}", download::describe(stats));
      std::fflush(stdout);
    } else {
      std::println("{}", download::describe(stats));
    }
  });

  const bool ok = (*session)->run();
  if (live) {
    std::println("");
  }
  if (!ok) {
    std::println(stderr, "Download failed, see log.txt for details");
    exit(1);
  }

  const auto final_stats = (*session)->stats();
  if (final_stats.bytes_left == 0) {
    std::println("Download complete: {} downloaded, {} uploaded",
                 download::format_bytes(final_stats.downloaded),
                 download::format_bytes(final_stats.uploaded));
  } else {
    std::println("Stopped with {}/{} pieces", final_stats.completed_pieces,
                 final_stats.total_pieces);
  }

}
