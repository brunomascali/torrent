#pragma once

#include <string>
namespace error {

class BaseError {
public:
  virtual std::string str() const = 0;
  virtual constexpr std::string module() const = 0;
  virtual ~BaseError() = default;
};

} // namespace error
