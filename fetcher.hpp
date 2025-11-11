#pragma once

#include <string>
#include <string_view>
#include <optional>

class Fetcher
{
public:
    [[nodiscard]] static std::optional<std::string> fetch_json(std::string_view url);
};