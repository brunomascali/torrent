#include "download/tracker.hxx"
#include <bencode/bencode.hxx>
#include <bencode/decode.hxx>
#include <download/download_manager.hxx>
#include <io/file_handle.hxx>
#include <metainfo/metainfo.hxx>
#include <gtest/gtest.h>
#include <boost/asio/ip/address.hpp>
#include <download/http_announce.hxx>

class HttpAnnounceTest : public ::testing::Test {
protected:
  std::unique_ptr<bencode::Value> m_bencode;
  std::unique_ptr<metainfo::Metainfo> m_metainfo;
  std::unique_ptr<download::Tracker> m_ubuntu_tracker;

  void SetUp() override {
    const char *project_root = std::getenv("TEST_TORRENTS_DIR");
    if (project_root == nullptr) {
      throw std::runtime_error(
          "TORRENT_ROOT_DIR environment variable is not set");
    }

    std::filesystem::path torrent_path =
        std::filesystem::path(project_root) / "ubuntu-26.04-desktop-amd64.iso.torrent";
    auto handle = io::FileHandle(torrent_path);

    ASSERT_TRUE(handle.open().has_value())
        << "Could not find ubuntu-26.04-desktop-amd64.iso.torrent file in the working directory.";

    auto file_size = handle.size().value();
    std::vector<std::byte> buffer(file_size, std::byte{0});
    auto bytes_read = handle.read_at(0, buffer);
    ASSERT_EQ(file_size, bytes_read) << "File was not completely read";

    auto decode_res = bencode::decode(buffer);
    ASSERT_TRUE(decode_res.has_value())
        << "Failed to parse bencode from the torrent file.";

    m_bencode = std::make_unique<bencode::Value>(decode_res.value());
    m_metainfo = std::make_unique<metainfo::Metainfo>(metainfo::Metainfo(*m_bencode));
    m_ubuntu_tracker = std::make_unique<download::Tracker>(m_metainfo->announce().value());
  }

  void TearDown() override {}
};

TEST_F(HttpAnnounceTest, HttpAnnounce) {
  download::HttpAnnounceRequest req{
    .info_hash = std::string(
        "\xDA" "\xFC" "\x8C" "\x07" "l" "\xA2" "\xF3" "\xED" "7" "n"
        "\xEA" "\xE7" "\xC7" "j" "\x0D" "k" "\xE2" "A" "\\" "E",
        20),
    .peer_id = "-TR2940-123456789012",
    .ip = std::nullopt,
    .port = 6881,
    .uploaded = 0,
    .downloaded = 0,
    .left = 1000,
  };

  std::expected<download::HttpAnnounceResponse, std::string> response = m_ubuntu_tracker->announce(req);

  ASSERT_TRUE(response.has_value());
  ASSERT_TRUE(response->is_success());

  for (const auto& peer : response->as_success()->peers) {
    auto addr = boost::asio::ip::make_address(peer.ip);
    ASSERT_TRUE(addr.is_v4() || addr.is_v6());
  }
}
