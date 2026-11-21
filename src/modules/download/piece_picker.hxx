#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace download {

class PieceLayout {
public:
  static std::expected<PieceLayout, std::string>
  make(std::uint64_t total_length, std::uint64_t piece_length,
       std::size_t num_pieces);

  std::size_t num_pieces() const noexcept { return m_num_pieces; }
  std::uint64_t total_length() const noexcept { return m_total_length; }
  std::uint64_t piece_length() const noexcept { return m_piece_length; }

  std::uint32_t piece_size(std::uint32_t index) const noexcept;
  std::uint64_t piece_offset(std::uint32_t index) const noexcept;

private:
  PieceLayout(std::uint64_t total_length, std::uint64_t piece_length,
              std::size_t num_pieces) noexcept
      : m_total_length(total_length), m_piece_length(piece_length),
        m_num_pieces(num_pieces) {}

  std::uint64_t m_total_length;
  std::uint64_t m_piece_length;
  std::size_t m_num_pieces;
};

class PiecePicker {
public:
  explicit PiecePicker(PieceLayout layout);

  const PieceLayout &layout() const noexcept { return m_layout; }

  std::optional<std::uint32_t> reserve(const std::vector<bool> &peer_has);

  bool is_interesting(const std::vector<bool> &peer_has) const;

  void release(std::uint32_t index);

  void complete(std::uint32_t index);

  bool is_complete() const;
  std::size_t completed_count() const;
  std::uint64_t bytes_left() const;

private:
  enum class State : std::uint8_t { missing, in_progress, done };

  PieceLayout m_layout;
  mutable std::mutex m_mutex;
  std::vector<State> m_states;
  std::size_t m_completed{0};
  std::uint64_t m_bytes_left;
};

}
