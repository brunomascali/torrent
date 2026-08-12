#pragma once

#include <bencode/bencode.hxx>
#include <bencode/error.hxx>
#include <cstddef>
#include <expected>
#include <span>

namespace bencode {

std::expected<bencode::Value, bencode::Error> decode(std::span<const std::byte>);

} // namespace bencode
