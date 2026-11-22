#include <download/piece_picker.hxx>

#include <gtest/gtest.h>

#include <set>
#include <thread>

using download::PieceLayout;
using download::PiecePicker;

namespace {

PieceLayout small_layout() { return PieceLayout::make(10, 4, 3).value(); }

std::vector<bool> all(std::size_t n) { return std::vector<bool>(n, true); }

}

TEST(PieceLayout, LastPieceHoldsTheRemainder) {
  auto layout = small_layout();
  EXPECT_EQ(layout.num_pieces(), 3u);
  EXPECT_EQ(layout.piece_size(0), 4u);
  EXPECT_EQ(layout.piece_size(1), 4u);
  EXPECT_EQ(layout.piece_size(2), 2u);
  EXPECT_EQ(layout.piece_offset(2), 8u);
}

TEST(PieceLayout, ExactMultipleHasFullLastPiece) {
  auto layout = PieceLayout::make(12, 4, 3).value();
  EXPECT_EQ(layout.piece_size(2), 4u);
}

TEST(PieceLayout, SinglePieceSmallerThanPieceLength) {
  auto layout = PieceLayout::make(100, 262144, 1).value();
  EXPECT_EQ(layout.piece_size(0), 100u);
}

TEST(PieceLayout, HandlesTorrentsLargerThan4GiB) {
  const std::uint64_t piece = 1 << 20;
  const std::uint64_t total = 5ull * 1024 * piece + 123;
  auto layout = PieceLayout::make(total, piece, 5 * 1024 + 1).value();
  EXPECT_EQ(layout.piece_offset(5 * 1024), 5ull * 1024 * piece);
  EXPECT_EQ(layout.piece_size(5 * 1024), 123u);
}

TEST(PieceLayout, RejectsMismatchedPieceCount) {
  EXPECT_FALSE(PieceLayout::make(10, 4, 2));
  EXPECT_FALSE(PieceLayout::make(10, 4, 4));
}

TEST(PieceLayout, RejectsZeroLengths) {
  EXPECT_FALSE(PieceLayout::make(0, 4, 0));
  EXPECT_FALSE(PieceLayout::make(10, 0, 3));
}

TEST(PiecePicker, ReservesOnlyPiecesThePeerHas) {
  PiecePicker picker(small_layout());
  EXPECT_EQ(picker.reserve({false, false, true}), 2u);
  EXPECT_EQ(picker.reserve({false, false, true}), std::nullopt);
  EXPECT_EQ(picker.reserve({false, false, false}), std::nullopt);
}

TEST(PiecePicker, NeverHandsOutTheSamePieceTwice) {
  PiecePicker picker(small_layout());
  std::set<std::uint32_t> got;
  while (auto p = picker.reserve(all(3)))
    got.insert(*p);
  EXPECT_EQ(got, (std::set<std::uint32_t>{0, 1, 2}));
}

TEST(PiecePicker, ReleasedPieceCanBeReservedAgain) {
  PiecePicker picker(small_layout());
  auto p = picker.reserve(all(3));
  ASSERT_TRUE(p);
  picker.release(*p);
  EXPECT_EQ(picker.reserve(all(3)), p);
}

TEST(PiecePicker, CompletedPieceIsNotReservedAgain) {
  PiecePicker picker(small_layout());
  picker.complete(*picker.reserve({true, false, false}));
  picker.release(0);
  EXPECT_EQ(picker.reserve({true, false, false}), std::nullopt);
}

TEST(PiecePicker, TracksCompletionAndBytesLeft) {
  PiecePicker picker(small_layout());
  EXPECT_FALSE(picker.is_complete());
  EXPECT_EQ(picker.bytes_left(), 10u);

  for (int i = 0; i < 3; ++i)
    picker.complete(*picker.reserve(all(3)));

  EXPECT_TRUE(picker.is_complete());
  EXPECT_EQ(picker.completed_count(), 3u);
  EXPECT_EQ(picker.bytes_left(), 0u);
}

TEST(PiecePicker, CompletingTwiceCountsOnce) {
  PiecePicker picker(small_layout());
  picker.complete(*picker.reserve({false, false, true}));
  picker.complete(2);
  EXPECT_EQ(picker.completed_count(), 1u);
  EXPECT_EQ(picker.bytes_left(), 8u);
}

TEST(PiecePicker, RejectsBitfieldOfWrongSize) {
  PiecePicker picker(small_layout());
  EXPECT_EQ(picker.reserve(all(2)), std::nullopt);
}

TEST(PiecePicker, ConcurrentReservesAreDisjoint) {
  constexpr std::size_t n = 10'000;
  PiecePicker picker(PieceLayout::make(n * 16, 16, n).value());

  std::vector<std::vector<std::uint32_t>> per_thread(8);
  std::vector<std::thread> threads;
  for (auto &mine : per_thread) {
    threads.emplace_back([&] {
      while (auto p = picker.reserve(all(n)))
        mine.push_back(*p);
    });
  }
  for (auto &t : threads)
    t.join();

  std::set<std::uint32_t> seen;
  std::size_t total = 0;
  for (const auto &mine : per_thread) {
    total += mine.size();
    seen.insert(mine.begin(), mine.end());
  }
  EXPECT_EQ(total, n);
  EXPECT_EQ(seen.size(), n);
}

TEST(PiecePicker, InterestingWhenPeerHasAPieceNotDone) {
  PiecePicker picker(small_layout());
  EXPECT_FALSE(picker.is_interesting({false, false, false}));
  EXPECT_TRUE(picker.is_interesting({false, true, false}));

  auto p = picker.reserve({false, true, false});
  EXPECT_TRUE(picker.is_interesting({false, true, false}));

  picker.complete(*p);
  EXPECT_FALSE(picker.is_interesting({false, true, false}));
  EXPECT_FALSE(picker.is_interesting(all(2)));
}

TEST(PiecePicker, HasReportsOnlyDonePieces) {
  PiecePicker picker(small_layout());
  auto p = picker.reserve({true, false, false});
  EXPECT_FALSE(picker.has(*p));
  picker.complete(*p);
  EXPECT_TRUE(picker.has(*p));
  EXPECT_FALSE(picker.has(1));
  EXPECT_FALSE(picker.has(99));
}

TEST(PiecePicker, SnapshotAndCompletionLogNeverMissOrRepeatAPiece) {
  PiecePicker picker(small_layout());
  picker.complete(2);

  std::size_t cursor = 0;
  EXPECT_EQ(picker.have_snapshot(cursor), (std::vector<bool>{false, false, true}));
  EXPECT_TRUE(picker.completed_since(cursor).empty());

  picker.complete(0);
  picker.complete(1);
  EXPECT_EQ(picker.completed_since(cursor), (std::vector<std::uint32_t>{0, 1}));
  EXPECT_TRUE(picker.completed_since(cursor).empty());

  std::size_t other = 0;
  EXPECT_EQ(picker.completed_since(other), (std::vector<std::uint32_t>{2, 0, 1}));
}
