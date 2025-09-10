#pragma once


#include <string>
#include <optional>
#include <any>
#include <unordered_map>

namespace facebook::velox::config {

class IConfig {
public:
  IConfig() = default;
  virtual std::optional<std::string> get(const std::string& key) const = 0;
  virtual std::unordered_map<std::string, std::string> rawConfigsCopy() const = 0;
  virtual ~IConfig() = default;
};

} // namespace facebook::velox::config
