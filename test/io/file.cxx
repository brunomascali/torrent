#include <io/file.hxx>

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

class FileTest : public ::testing::Test {
protected:
    std::filesystem::path test_file;

    void SetUp() override {
        test_file = std::filesystem::temp_directory_path() / "torrent_io_test_file.txt";
        std::filesystem::remove(test_file);
    }

    void TearDown() override {
        std::filesystem::remove(test_file);
    }

    static std::span<const std::byte> as_bytes(const std::string &s) {
        return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
    }
};

TEST_F(FileTest, CreateMakesMissingFile) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value()) << file.error().str();
    EXPECT_TRUE(std::filesystem::exists(test_file));
    EXPECT_EQ(file->path(), test_file);
}

TEST_F(FileTest, FailsToOpenMissingFileWithoutCreate) {
    auto file = io::File::open(test_file, io::OpenMode::read_write);
    ASSERT_FALSE(file.has_value());
    EXPECT_EQ(file.error().code, std::errc::no_such_file_or_directory);
    EXPECT_FALSE(std::filesystem::exists(test_file));
}

TEST_F(FileTest, CanWriteAndReadData) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());

    std::string text = "bittorrent_piece_data";
    ASSERT_TRUE(file->write_all_at(0, as_bytes(text)).has_value());

    std::vector<std::byte> buffer(text.size());
    ASSERT_TRUE(file->read_exact_at(0, buffer).has_value());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(buffer.data()), buffer.size()), text);
}

TEST_F(FileTest, WritesAtOffset) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());

    ASSERT_TRUE(file->write_all_at(4, as_bytes("abcd")).has_value());
    EXPECT_EQ(file->size().value(), 8);

    std::vector<std::byte> buffer(4);
    ASSERT_TRUE(file->read_exact_at(4, buffer).has_value());
    EXPECT_EQ(buffer[0], std::byte{'a'});
}

TEST_F(FileTest, ReadAtStopsAtEndOfFile) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(file->write_all_at(0, as_bytes("abc")).has_value());

    std::vector<std::byte> buffer(10);
    auto n = file->read_at(0, buffer);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(n.value(), 3);
}

TEST_F(FileTest, ReadExactFailsPastEndOfFile) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(file->write_all_at(0, as_bytes("abc")).has_value());

    std::vector<std::byte> buffer(10);
    auto res = file->read_exact_at(0, buffer);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, io::ErrC::unexpected_eof);
}

TEST_F(FileTest, CorrectlyReportsFileSize) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size().value(), 0);

    std::vector<std::byte> data(42, std::byte{0xFF});
    ASSERT_TRUE(file->write_all_at(0, data).has_value());
    EXPECT_EQ(file->size().value(), 42);
}

TEST_F(FileTest, ResizeChangesSize) {
    auto file = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(file.has_value());

    ASSERT_TRUE(file->resize(1024).has_value());
    EXPECT_EQ(file->size().value(), 1024);
}

TEST_F(FileTest, ReadOnlyRejectsWrites) {
    ASSERT_TRUE(io::File::open(test_file, io::OpenMode::create).has_value());

    auto file = io::File::open(test_file, io::OpenMode::read_only);
    ASSERT_TRUE(file.has_value());
    auto res = file->write_all_at(0, as_bytes("x"));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, std::errc::bad_file_descriptor);
}

TEST_F(FileTest, MoveTransfersOwnership) {
    auto opened = io::File::open(test_file, io::OpenMode::create);
    ASSERT_TRUE(opened.has_value());

    io::File moved = std::move(*opened);
    ASSERT_TRUE(moved.write_all_at(0, as_bytes("ok")).has_value());
    EXPECT_EQ(moved.size().value(), 2);
    EXPECT_EQ(moved.path(), test_file);
}
