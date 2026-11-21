#include "download/tracker.hxx"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

#include <bencode/bencode.hxx>
#include <bencode/decode.hxx>
#include <io/file.hxx>
#include <metainfo/metainfo.hxx>

class MultipleFileMetainfoTest : public ::testing::Test {
protected:
  bencode::Value m_bencode;
  std::unique_ptr<metainfo::Metainfo> m_metainfo;

  void SetUp() override {
    const char *project_root = std::getenv("TEST_TORRENTS_DIR");
    if (project_root == nullptr) {
      throw std::runtime_error(
          "TORRENT_ROOT_DIR environment variable is not set");
    }

    std::filesystem::path torrent_path =
        std::filesystem::path(project_root) / "multi_file.torrent";
    auto handle = io::File::open(torrent_path, io::OpenMode::read_only);

    ASSERT_TRUE(handle.has_value())
        << "Could not find multi_file.torrent file in the working directory.";

    auto file_size = handle->size().value();
    std::vector<std::byte> buffer(file_size, std::byte{0});
    ASSERT_TRUE(handle->read_exact_at(0, buffer).has_value())
        << "File was not completely read";

    auto decode_res = bencode::decode(buffer);
    ASSERT_TRUE(decode_res.has_value())
        << "Failed to parse bencode from the torrent file.";

    m_bencode = decode_res.value();
    m_metainfo = std::make_unique<metainfo::Metainfo>(m_bencode);
  }

  void TearDown() override { m_metainfo.reset(); }
};

TEST_F(MultipleFileMetainfoTest, SuccessfullyReadsAnnounceUrl) {
  auto result = m_metainfo->announce();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), download::Tracker("http://bruno.com"));
}

TEST_F(MultipleFileMetainfoTest, SuccessfullyReadsFiles) {
  auto result = m_metainfo->files();
  ASSERT_TRUE(result.has_value());

  namespace fs = std::filesystem;

  std::vector<metainfo::File> expected_files = {
      metainfo::File{fs::path("dir2") / fs::path("nested_file.txt"), 13},
      metainfo::File{fs::path("file.txt"), 7}
  };

  EXPECT_EQ(result.value(), expected_files);
}

TEST_F(MultipleFileMetainfoTest, SuccessfullyReadsTotalSize) {
  auto result = m_metainfo->total_size();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 20);
}
