#include <logger/file_sink.hxx>
#include <logger/logger.hxx>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <thread>
#include <vector>

using namespace logger;

namespace {
int format_calls = 0;
struct FormatProbe {};
}

template <> struct std::formatter<FormatProbe> : std::formatter<std::string_view> {
  auto format(FormatProbe, std::format_context &ctx) const {
    ++format_calls;
    return std::formatter<std::string_view>::format("probe", ctx);
  }
};

namespace {

struct Captured {
  std::mutex mutex;
  std::vector<std::string> lines;
  std::vector<LogLevel> levels;
};

class CaptureSink : public LogSink {
public:
  explicit CaptureSink(std::shared_ptr<Captured> out) : m_out(std::move(out)) {}

  void write(const LogRecord &record) override {
    std::lock_guard lock(m_out->mutex);
    m_out->lines.emplace_back(record.message);
    m_out->levels.push_back(record.level);
  }

private:
  std::shared_ptr<Captured> m_out;
};

class LoggerTest : public ::testing::Test {
protected:
  std::shared_ptr<Captured> captured = std::make_shared<Captured>();

  void SetUp() override {
    Logger::instance().set_min_level(LogLevel::Trace);
    Logger::instance().set_sink(std::make_unique<CaptureSink>(captured));
  }

  void TearDown() override {
    Logger::instance().set_sink(nullptr);
    Logger::instance().set_min_level(LogLevel::Debug);
  }
};

const std::regex line_format(
    R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} T\d+ +(Trace|Debug|Info|Warn|Error) +.*\n)");

}

TEST_F(LoggerTest, FormatsMessageAndLevel) {
  info("hello {} {}", 42, "world");
  ASSERT_EQ(captured->lines.size(), 1u);
  EXPECT_EQ(captured->lines[0], "hello 42 world");
  EXPECT_EQ(captured->levels[0], LogLevel::Info);
}

TEST_F(LoggerTest, DropsMessagesBelowMinLevel) {
  Logger::instance().set_min_level(LogLevel::Warn);
  trace("t");
  debug("d");
  info("i");
  warn("w");
  error("e");
  EXPECT_EQ(captured->lines, (std::vector<std::string>{"w", "e"}));
}

TEST_F(LoggerTest, DisabledLevelSkipsFormatting) {
  Logger::instance().set_min_level(LogLevel::Info);
  format_calls = 0;
  debug("{}", FormatProbe{});
  trace("{}", FormatProbe{});
  EXPECT_EQ(format_calls, 0);
  info("{}", FormatProbe{});
  EXPECT_EQ(format_calls, 1);
}

TEST_F(LoggerTest, NoSinkIsANoOp) {
  Logger::instance().set_sink(nullptr);
  EXPECT_FALSE(Logger::instance().enabled(LogLevel::Error));
  error("nobody listens");
}

TEST_F(LoggerTest, ConcurrentLoggingKeepsEveryMessageIntact) {
  constexpr int threads = 8;
  constexpr int per_thread = 2000;
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([t] {
      for (int i = 0; i < per_thread; ++i) {
        warn("thread {} message {} padding padding padding", t, i);
      }
    });
  }
  for (auto &th : pool) {
    th.join();
  }

  ASSERT_EQ(captured->lines.size(), static_cast<std::size_t>(threads * per_thread));
  std::set<std::string> unique(captured->lines.begin(), captured->lines.end());
  EXPECT_EQ(unique.size(), captured->lines.size());
}

TEST(FormatRecord, ProducesTimestampThreadLevelMessage) {
  const LogRecord record{.level = LogLevel::Warn,
                         .time = std::chrono::system_clock::now(),
                         .thread = 3,
                         .message = "peer gone"};
  const auto line = format_record(record);
  EXPECT_TRUE(std::regex_match(line, line_format)) << line;
  EXPECT_NE(line.find(" T3 "), std::string::npos) << line;
  EXPECT_TRUE(line.ends_with("Warn  peer gone\n")) << line;
}

TEST(FormatRecord, MillisecondsComeFromTheRecordTime) {
  using namespace std::chrono;
  const auto base = floor<seconds>(system_clock::now());
  const LogRecord record{.level = LogLevel::Info,
                         .time = base + milliseconds(7),
                         .thread = 1,
                         .message = "x"};
  EXPECT_NE(format_record(record).find(".007 "), std::string::npos) << format_record(record);
}

TEST(ThreadNumber, IsStablePerThreadAndDistinctAcrossThreads) {
  const unsigned mine = current_thread_number();
  EXPECT_EQ(current_thread_number(), mine);
  unsigned other = 0;
  std::thread([&] { other = current_thread_number(); }).join();
  EXPECT_NE(other, mine);
}

TEST(FileSinkConcurrency, EveryLineIsWellFormedUnderContention) {
  const auto path = std::filesystem::temp_directory_path() / "torrent_logger_test.log";
  {
    auto sink = std::make_unique<FileSink>(path);
    ASSERT_TRUE(sink->is_open());
    Logger::instance().set_min_level(LogLevel::Trace);
    Logger::instance().set_sink(std::move(sink));

    std::vector<std::thread> pool;
    for (int t = 0; t < 8; ++t) {
      pool.emplace_back([t] {
        for (int i = 0; i < 500; ++i) {
          warn("Connect failed: [io] 10.0.{}.{}:6881: Connection timed out", t, i % 250);
        }
      });
    }
    for (auto &th : pool) {
      th.join();
    }
    Logger::instance().set_sink(nullptr);
    Logger::instance().set_min_level(LogLevel::Debug);
  }

  std::ifstream in(path);
  std::string line;
  std::size_t count = 0;
  while (std::getline(in, line)) {
    ++count;
    ASSERT_TRUE(std::regex_match(line + "\n", line_format)) << "line " << count << ": " << line;
    ASSERT_TRUE(line.ends_with(":6881: Connection timed out")) << line;
  }
  EXPECT_EQ(count, 8u * 500u);
  std::filesystem::remove(path);
}
