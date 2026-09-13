#include <bencode/bencode.hxx>
#include <bencode/error.hxx>
#include <bencode/decode.hxx>

#include <cctype>
#include <cstddef>
#include <expected>
#include <format>
#include <span>

namespace bencode {

std::expected<bencode::Value, bencode::Error> decode_(std::span<const std::byte> buffer, size_t &i);
std::expected<bencode::Integer, bencode::Error> decode_int(std::span<const std::byte> buffer, size_t &i);
std::expected<bencode::ByteString, bencode::Error> decode_bytestring(std::span<const std::byte> buffer, size_t &i);
std::expected<bencode::List, bencode::Error> decode_list(std::span<const std::byte> buffer, size_t &i);
std::expected<bencode::Dict, bencode::Error> decode_dict(std::span<const std::byte> buffer, size_t &i);

bencode::Error bencode_err(BencodeErrorCode ec, size_t index);

std::expected<bencode::Value, bencode::Error> decode(std::span<const std::byte> buffer) {
  size_t i = 0;
  return decode_(buffer, i);
}

std::expected<bencode::Value, bencode::Error> decode_(std::span<const std::byte> buffer, size_t& i) {
  if (buffer[i] == std::byte{'i'})
    return decode_int(buffer, i);
  if (std::isdigit(static_cast<unsigned char>(buffer[i]))) {
    return decode_bytestring(buffer, i);
  }
  if (buffer[i] == std::byte{'l'})
    return decode_list(buffer, i);
  if (buffer[i] == std::byte{'d'})
    return decode_dict(buffer, i);
  else {
    auto missing_starting_char =
        bencode::Error(BencodeErrorCode::missing_starting_character,
                       std::format("Position: {}", i));
    return std::unexpected(missing_starting_char);
  }

  if (i < buffer.size()) {
    auto missing_terminator = bencode::Error(
        BencodeErrorCode::missing_terminator, std::format("Position: {}", i));
    return std::unexpected(missing_terminator);
  }
}

std::expected<bencode::Integer, bencode::Error>
decode_int(std::span<const std::byte> buffer, size_t &i) {
  bencode::Integer number = 0;
  i += 1;

  bool is_negative = buffer[i] == std::byte{'-'};
  if (is_negative)
    i += 1;

  while (i < buffer.size() && buffer[i] != std::byte{'e'}) {
    number = 10 * number + std::to_integer<int>(buffer[i]) - '0';
    i += 1;
  }

  if (i >= buffer.size()) {
    auto missing_terminator = bencode::Error(
        BencodeErrorCode::missing_terminator, std::format("Position: {}", i));
    return std::unexpected(missing_terminator);
  }

  if (buffer[i] == std::byte{'e'}) {
    i += 1;
  }

  if (is_negative)
    number *= -1;

  return number;
}

std::expected<bencode::ByteString, bencode::Error>
decode_bytestring(std::span<const std::byte> buffer, size_t &i) {
  size_t length = 0;
  while (i < buffer.size() && buffer[i] != std::byte{':'}) {
    length = 10 * length + std::to_integer<int>(buffer[i]) - '0';
    i += 1;
  }

  if (i >= buffer.size()) {
    return std::unexpected(bencode_err(BencodeErrorCode::missing_terminator, i));
  }

  i += 1;

  if (i + length > buffer.size()) {
    return std::unexpected(bencode_err(BencodeErrorCode::malformed_bytestring, i));
  }


  auto str = std::string(reinterpret_cast<const char*>(buffer.data() + i), length);
  i += length;
  return str;
}

std::expected<bencode::List, bencode::Error> decode_list(std::span<const std::byte> buffer, size_t &i) {
  bencode::List list;

  i += 1;

  while (i < buffer.size() && buffer[i] != std::byte{'e'}) {
    auto bencode = decode_(buffer, i);
    if (!bencode.has_value())
      return std::unexpected(bencode.error());

    list.emplace_back(std::move(bencode.value()));
  }

  if (i >= buffer.size())
    return std::unexpected(bencode_err(BencodeErrorCode::malformed_list, i));

  if (buffer[i] != std::byte{'e'})
    return std::unexpected(bencode_err(BencodeErrorCode::missing_terminator, i));

  i += 1;
  return list;
}

std::expected<bencode::Dict, bencode::Error> decode_dict(std::span<const std::byte> buffer, size_t &i) {
  bencode::Dict dict;

  i += 1;

  while (i < buffer.size() && buffer[i] != std::byte{'e'}) {
    if (!std::isdigit(static_cast<unsigned char>(buffer[i]))) {
      return std::unexpected(bencode_err(BencodeErrorCode::missing_starting_character, i));
    }

    auto key = decode_bytestring(buffer, i);
    if (!key.has_value())
      return std::unexpected(key.error());

    if (i >= buffer.size()) {
      return std::unexpected(bencode_err(BencodeErrorCode::missing_terminator, i));
    }

    auto value = decode_(buffer, i);
    if (!value.has_value())
      return std::unexpected(value.error());

    dict.emplace(std::move(key.value()), std::move(value.value()));
  }

  if (i >= buffer.size() || buffer[i] != std::byte{'e'}) {
    return std::unexpected(bencode_err(BencodeErrorCode::missing_terminator, i));
  }

  i += 1;
  return dict;
}

bencode::Error bencode_err(BencodeErrorCode ec, size_t index) {
  auto err = bencode::Error(ec, std::format("Position: {}", index));
  return err;
}

} // namespace bencode
