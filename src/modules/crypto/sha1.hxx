#pragma once

#include <array>
#include <span>

namespace crypto {

std::array<std::byte, 20> sha1(std::span<const std::byte> bytes);

}
