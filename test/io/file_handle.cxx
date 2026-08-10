#include <io/file_handle.hxx>

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

class FileHandleTest : public ::testing::Test {
protected:
    std::filesystem::path test_file;

    void SetUp() override {
        test_file = std::filesystem::temp_directory_path() / "torrent_io_test_file.txt";
    }

    void TearDown() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }
};

TEST_F(FileHandleTest, CanOpenAndCloseFile) {
    io::FileHandle fh(test_file);

    EXPECT_FALSE(fh.is_open());

    auto open_res = fh.open();
    ASSERT_TRUE(open_res.has_value()) << "Failed to open file";
    EXPECT_TRUE(fh.is_open());

    fh.close();
    EXPECT_FALSE(fh.is_open());
}

TEST_F(FileHandleTest, CanWriteAndReadData) {
    io::FileHandle fh(test_file);
    ASSERT_TRUE(fh.open().has_value());

    std::string text_data = "bittorrent_piece_data";
    std::span<const std::byte> write_span(
        reinterpret_cast<const std::byte*>(text_data.data()),
        text_data.size()
    );

    auto write_res = fh.write_at(0, write_span);
    ASSERT_TRUE(write_res.has_value());
    EXPECT_EQ(write_res.value(), text_data.size());

    std::vector<std::byte> read_buffer(text_data.size());

    auto read_res = fh.read_at(0, read_buffer);
    ASSERT_TRUE(read_res.has_value());
    EXPECT_EQ(read_res.value(), text_data.size());

    std::string read_text(
        reinterpret_cast<const char*>(read_buffer.data()),
        read_buffer.size()
    );
    EXPECT_EQ(read_text, text_data);
}

TEST_F(FileHandleTest, CorrectlyReportsFileSize) {
    io::FileHandle fh(test_file);
    ASSERT_TRUE(fh.open().has_value());

    auto size_res = fh.size();
    ASSERT_TRUE(size_res.has_value());
    EXPECT_EQ(size_res.value(), 0);

    std::vector<std::byte> dummy_data(42, std::byte{0xFF});
    ASSERT_TRUE(fh.write_at(0, dummy_data).has_value());

    size_res = fh.size();
    ASSERT_TRUE(size_res.has_value());
    EXPECT_EQ(size_res.value(), 42);
}

TEST_F(FileHandleTest, FailsToOpenMissingFileIfCreateIsFalse) {
    std::filesystem::remove(test_file);

    io::FileHandle fh(test_file);
    auto res = fh.open(false);

    EXPECT_FALSE(res.has_value());
    EXPECT_FALSE(fh.is_open());
}
