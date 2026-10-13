#include "logger/logger.hxx"
#include <boost/uuid/detail/sha1.hpp>
#include <crypto/sha1.hxx>

namespace crypto {

std::array<std::byte, 20> sha1(std::span<const std::byte> bytes) {
  logger::trace("Generating sha1 of {} bytes", bytes.size());
  boost::uuids::detail::sha1 hasher;
  hasher.process_bytes(bytes.data(), bytes.size());

  boost::uuids::detail::sha1::digest_type digest;
  hasher.get_digest(digest);

  std::array<std::byte, 20> out{};

  using Elem = std::remove_extent_t<decltype(digest)>;

  if constexpr (sizeof(Elem) == 1) {
    for (std::size_t i = 0; i < 20; ++i) {
      out[i] = static_cast<std::byte>(digest[i]);
    }
  } else {
    for (std::size_t i = 0; i < 5; ++i) {
      out[4 * i + 0] = static_cast<std::byte>((digest[i] >> 24) & 0xFF);
      out[4 * i + 1] = static_cast<std::byte>((digest[i] >> 16) & 0xFF);
      out[4 * i + 2] = static_cast<std::byte>((digest[i] >> 8) & 0xFF);
      out[4 * i + 3] = static_cast<std::byte>(digest[i] & 0xFF);
    }
  }

  return out;
}

} // namespace crypto
