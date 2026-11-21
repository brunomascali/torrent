#include <download/peer/wire/bytes.hxx>
#include <download/peer/wire/stream.hxx>

#include <array>
#include <format>
#include <vector>

namespace download::peer::wire {

boost::asio::awaitable<std::expected<void, Error>>
write_handshake(io::TcpConnection &conn, const Handshake &handshake) {
  auto bytes = handshake.encode();
  if (auto sent = co_await conn.async_write(bytes); !sent) {
    co_return std::unexpected(Error::from_io(sent.error()));
  }
  co_return std::expected<void, Error>{};
}

boost::asio::awaitable<std::expected<Handshake, Error>>
read_handshake(io::TcpConnection &conn) {
  std::array<std::byte, Handshake::size> bytes;
  if (auto got = co_await conn.async_read_exact(bytes); !got) {
    co_return std::unexpected(Error::from_io(got.error()));
  }
  co_return Handshake::decode(bytes);
}

boost::asio::awaitable<std::expected<void, Error>>
write_message(io::TcpConnection &conn, const PeerMessage &message) {
  auto bytes = encode(message);
  if (auto sent = co_await conn.async_write(bytes); !sent) {
    co_return std::unexpected(Error::from_io(sent.error()));
  }
  co_return std::expected<void, Error>{};
}

boost::asio::awaitable<std::expected<PeerMessage, Error>>
read_message(io::TcpConnection &conn, std::size_t num_pieces) {
  std::array<std::byte, 4> prefix;
  if (auto got = co_await conn.async_read_exact(prefix); !got) {
    co_return std::unexpected(Error::from_io(got.error()));
  }

  std::uint32_t length = load_u32_be(prefix);
  if (length == 0) {
    co_return KeepAlive{};
  }
  if (length > max_message_length) {
    co_return std::unexpected(Error::make(
        Errc::message_too_large,
        std::format("{} bytes (max {})", length, max_message_length)));
  }

  std::vector<std::byte> body(length);
  if (auto got = co_await conn.async_read_exact(std::span(body)); !got) {
    co_return std::unexpected(Error::from_io(got.error()));
  }
  co_return decode(body, num_pieces);
}

}
