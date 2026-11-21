#include <download/peer_id.hxx>

#include <algorithm>
#include <random>
#include <string_view>

namespace download {

std::array<std::byte, 20> make_peer_id() {
  constexpr std::string_view prefix = "-XX0001-";
  constexpr std::string_view alphabet =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::array<std::byte, 20> id{};
  auto it = std::ranges::transform(prefix, id.begin(), [](char c) {
              return static_cast<std::byte>(c);
            }).out;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);
  for (; it != id.end(); ++it)
    *it = static_cast<std::byte>(alphabet[dist(gen)]);

  return id;
}

}
