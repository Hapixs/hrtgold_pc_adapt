#pragma once

#include <filesystem>
#include <string_view>

class AppPaths {
public:
    explicit AppPaths(std::filesystem::path localAppData);
    std::filesystem::path root() const;
    std::filesystem::path saveDirectory(std::string_view sha256) const;

private:
    std::filesystem::path root_;
};
