#include <download/piece_picker.hxx>

#include <format>
#include <limits>
#include <logger/logger.hxx>

namespace download {

std::expected<PieceLayout, std::string>
PieceLayout::make(std::uint64_t total_length, std::uint64_t piece_length,
                  std::size_t num_pieces) {
  if (total_length == 0) {
    return std::unexpected("torrent length is 0");
  }
  if (piece_length == 0) {
    return std::unexpected("piece length is 0");
  }

  if (piece_length > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(std::format("piece length {} too large", piece_length));
  }

  const std::uint64_t expected = (total_length + piece_length - 1) / piece_length;
  if (expected != num_pieces) {
    return std::unexpected(std::format(
        "{} bytes in {}-byte pieces needs {} pieces, but there are {} hashes",
        total_length, piece_length, expected, num_pieces));
  }
  if (num_pieces > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(std::format("too many pieces ({})", num_pieces));
  }

  return PieceLayout(total_length, piece_length, num_pieces);
}

std::uint32_t PieceLayout::piece_size(std::uint32_t index) const noexcept {
  if (index + 1 < m_num_pieces) {
    return static_cast<std::uint32_t>(m_piece_length);
  }
  return static_cast<std::uint32_t>(m_total_length - piece_offset(index));
}

std::uint64_t PieceLayout::piece_offset(std::uint32_t index) const noexcept {
  return std::uint64_t{index} * m_piece_length;
}

PiecePicker::PiecePicker(PieceLayout layout)
    : m_layout(layout), m_states(layout.num_pieces(), State::missing),
      m_bytes_left(layout.total_length()) {}

std::optional<std::uint32_t>
PiecePicker::reserve(const std::vector<bool> &peer_has) {
  if (peer_has.size() != m_states.size()) {
    logger::warn("reserve: peer bitfield has {} entries, expected {}",
                 peer_has.size(), m_states.size());
    return std::nullopt;
  }

  std::lock_guard lock(m_mutex);
  for (std::uint32_t i = 0; i < m_states.size(); ++i) {
    if (m_states[i] == State::missing && peer_has[i]) {
      m_states[i] = State::in_progress;
      return i;
    }
  }
  return std::nullopt;
}

bool PiecePicker::is_interesting(const std::vector<bool> &peer_has) const {
  if (peer_has.size() != m_states.size()) {
    return false;
  }
  std::lock_guard lock(m_mutex);
  for (std::size_t i = 0; i < m_states.size(); ++i) {
    if (peer_has[i] && m_states[i] != State::done) {
      return true;
    }
  }
  return false;
}

void PiecePicker::release(std::uint32_t index) {
  std::lock_guard lock(m_mutex);
  if (index < m_states.size() && m_states[index] == State::in_progress) {
    m_states[index] = State::missing;
  }
}

void PiecePicker::complete(std::uint32_t index) {
  std::lock_guard lock(m_mutex);
  if (index >= m_states.size() || m_states[index] == State::done) {
    return;
  }
  m_states[index] = State::done;
  ++m_completed;
  m_bytes_left -= m_layout.piece_size(index);
}

bool PiecePicker::is_complete() const {
  std::lock_guard lock(m_mutex);
  return m_completed == m_states.size();
}

std::size_t PiecePicker::completed_count() const {
  std::lock_guard lock(m_mutex);
  return m_completed;
}

std::uint64_t PiecePicker::bytes_left() const {
  std::lock_guard lock(m_mutex);
  return m_bytes_left;
}

}
