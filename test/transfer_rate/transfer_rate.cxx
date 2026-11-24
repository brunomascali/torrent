#include <download/download_session.hxx>
#include <download/transfer_rate.hxx>
#include <gtest/gtest.h>

using download::TransferRate;
using namespace std::chrono_literals;

namespace {

const TransferRate::Clock::time_point t0{};

}

TEST(TransferRate, IsZeroBeforeAnyTransfer) {
  TransferRate rate(t0);
  EXPECT_EQ(rate.bytes_per_second(t0), 0.0);
  EXPECT_EQ(rate.bytes_per_second(t0 + 3s), 0.0);
}

TEST(TransferRate, AveragesOverElapsedTimeDuringStartup) {
  TransferRate rate(t0);
  rate.add(1000, t0 + 100ms);
  rate.add(1000, t0 + 1500ms);
  EXPECT_DOUBLE_EQ(rate.bytes_per_second(t0 + 2s), 1000.0);
}

TEST(TransferRate, FirstSecondDoesNotInflateTheRate) {
  TransferRate rate(t0);
  rate.add(500, t0 + 10ms);
  EXPECT_DOUBLE_EQ(rate.bytes_per_second(t0 + 20ms), 500.0);
}

TEST(TransferRate, SteadyTransferGivesItsRate) {
  TransferRate rate(t0);
  for (int s = 0; s < 20; ++s) {
    rate.add(2048, t0 + std::chrono::seconds{s});
  }
  EXPECT_NEAR(rate.bytes_per_second(t0 + 20s), 2048.0, 2048.0 * 0.21);
  EXPECT_NEAR(rate.bytes_per_second(t0 + 19s + 999ms), 2048.0, 2048.0 * 0.21);
}

TEST(TransferRate, ForgetsTransfersOlderThanTheWindow) {
  TransferRate rate(t0);
  rate.add(1'000'000, t0 + 1s);
  EXPECT_GT(rate.bytes_per_second(t0 + 2s), 0.0);
  EXPECT_EQ(rate.bytes_per_second(t0 + 1s + std::chrono::seconds{TransferRate::window_seconds + 1}), 0.0);
}

TEST(TransferRate, ReflectsASpeedChangeWithinTheWindow) {
  TransferRate rate(t0);
  for (int s = 0; s < 10; ++s) {
    rate.add(10'000, t0 + std::chrono::seconds{s});
  }
  for (int s = 10; s < 20; ++s) {
    rate.add(1'000, t0 + std::chrono::seconds{s});
  }
  EXPECT_NEAR(rate.bytes_per_second(t0 + 20s), 1000.0, 250.0);
}

TEST(TransferRate, ReusesBucketsAfterALongPause) {
  TransferRate rate(t0);
  rate.add(9999, t0);
  rate.add(600, t0 + 100s);
  EXPECT_DOUBLE_EQ(rate.bytes_per_second(t0 + 101s), 600.0 / 5.0);
}

TEST(TransferRate, FormatsHumanReadableRates) {
  EXPECT_EQ(download::format_rate(0), "0 B/s");
  EXPECT_EQ(download::format_rate(512), "512 B/s");
  EXPECT_EQ(download::format_rate(1536), "1.5 KiB/s");
  EXPECT_EQ(download::format_rate(5.0 * 1024 * 1024), "5.0 MiB/s");
}

TEST(TransferStats, ProgressAndEta) {
  download::TransferStats s{.downloaded = 0, .uploaded = 0, .download_rate = 1024,
                            .upload_rate = 0, .completed_pieces = 25, .total_pieces = 100,
                            .bytes_left = 10 * 1024, .peers = 3};
  EXPECT_DOUBLE_EQ(s.progress(), 0.25);
  ASSERT_TRUE(s.eta());
  EXPECT_EQ(*s.eta(), 10s);

  s.download_rate = 0;
  EXPECT_FALSE(s.eta());

  s.bytes_left = 0;
  EXPECT_EQ(s.eta(), 0s);
}
