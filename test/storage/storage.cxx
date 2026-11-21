#include <download/storage.hxx>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeric>

using download::Storage;
using download::StorageFile;
namespace fs = std::filesystem;

namespace {

class StorageTest : public ::testing::Test {
protected:
  fs::path root;

  void SetUp() override {
    root = fs::temp_directory_path() /
           ("torrent_storage_test_" +
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(root);
    fs::create_directories(root);
  }

  void TearDown() override { fs::remove_all(root); }

  std::string contents(const fs::path &rel) {
    std::ifstream in(root / rel, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
  }
};

std::vector<std::byte> bytes(std::string_view s) {
  auto b = std::as_bytes(std::span(s));
  return {b.begin(), b.end()};
}

std::string str(std::span<const std::byte> b) {
  return {reinterpret_cast<const char *>(b.data()), b.size()};
}

}

TEST_F(StorageTest, SingleFileIsCreatedAndPreSized) {
  auto s = Storage::open(root, {{"a.bin", 10}});
  ASSERT_TRUE(s) << s.error().str();
  EXPECT_EQ(s->total_length(), 10u);
  EXPECT_EQ(fs::file_size(root / "a.bin"), 10u);
}

TEST_F(StorageTest, CreatesNestedDirectories) {
  auto s = Storage::open(root, {{"dir/sub/a.bin", 3}});
  ASSERT_TRUE(s) << s.error().str();
  EXPECT_TRUE(fs::exists(root / "dir/sub/a.bin"));
}

TEST_F(StorageTest, WriteSpanningFilesIsSplit) {
  auto s = Storage::open(root, {{"a", 4}, {"b", 2}, {"c", 4}});
  ASSERT_TRUE(s) << s.error().str();

  ASSERT_TRUE(s->write(2, bytes("XXYYZZ")));
  EXPECT_EQ(contents("a"), std::string("\0\0XX", 4));
  EXPECT_EQ(contents("b"), "YY");
  EXPECT_EQ(contents("c"), std::string("ZZ\0\0", 4));
}

TEST_F(StorageTest, ReadSpanningFilesIsJoined) {
  auto s = Storage::open(root, {{"a", 4}, {"b", 2}, {"c", 4}});
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->write(0, bytes("0123456789")));

  std::vector<std::byte> buf(6);
  ASSERT_TRUE(s->read(3, buf));
  EXPECT_EQ(str(buf), "345678");
}

TEST_F(StorageTest, SkipsZeroLengthFiles) {
  auto s = Storage::open(root, {{"a", 2}, {"empty", 0}, {"b", 2}});
  ASSERT_TRUE(s) << s.error().str();
  EXPECT_TRUE(fs::exists(root / "empty"));

  ASSERT_TRUE(s->write(0, bytes("abcd")));
  EXPECT_EQ(contents("a"), "ab");
  EXPECT_EQ(contents("empty"), "");
  EXPECT_EQ(contents("b"), "cd");
}

TEST_F(StorageTest, WriteAtExactFileBoundary) {
  auto s = Storage::open(root, {{"a", 2}, {"b", 2}});
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->write(2, bytes("cd")));
  EXPECT_EQ(contents("a"), std::string("\0\0", 2));
  EXPECT_EQ(contents("b"), "cd");
}

TEST_F(StorageTest, RejectsRangeOutsideTorrent) {
  auto s = Storage::open(root, {{"a", 4}});
  ASSERT_TRUE(s);
  auto r = s->write(2, bytes("abc"));
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, std::errc::invalid_argument);
  EXPECT_FALSE(s->write(5, bytes("")));
  EXPECT_TRUE(s->write(4, bytes("")));
}

TEST_F(StorageTest, RejectsPathsEscapingRoot) {
  for (const char *bad : {"../evil", "a/../../evil", "/etc/evil", "", "a/./b"}) {
    auto s = Storage::open(root, {{bad, 1}});
    ASSERT_FALSE(s) << bad;
    EXPECT_EQ(s.error().code, std::errc::invalid_argument) << bad;
  }
  EXPECT_FALSE(fs::exists(root.parent_path() / "evil"));
}

TEST_F(StorageTest, ReopeningKeepsExistingData) {
  {
    auto s = Storage::open(root, {{"a", 4}});
    ASSERT_TRUE(s);
    ASSERT_TRUE(s->write(0, bytes("data")));
  }
  auto s = Storage::open(root, {{"a", 4}});
  ASSERT_TRUE(s);
  EXPECT_EQ(contents("a"), "data");
}

TEST_F(StorageTest, PieceLargerThanSeveralSmallFiles) {
  std::vector<StorageFile> files;
  for (int i = 0; i < 50; ++i)
    files.push_back({"f" + std::to_string(i), 7});
  auto s = Storage::open(root, files);
  ASSERT_TRUE(s);

  std::vector<std::byte> data(350);
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<std::byte>(i);
  ASSERT_TRUE(s->write(0, data));

  std::vector<std::byte> back(350);
  ASSERT_TRUE(s->read(0, back));
  EXPECT_EQ(back, data);
  EXPECT_EQ(contents("f1"), str(std::span(data).subspan(7, 7)));
}
