#pragma once

#include <bencode/bencode.hxx>
#include <string>

namespace bencode {

std::string encode(const bencode::Value& bencode);

} // namespace bencode