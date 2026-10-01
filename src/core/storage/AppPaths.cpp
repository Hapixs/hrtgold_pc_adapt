#include "core/storage/AppPaths.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {
bool isValidSha256(std::string_view value) {
    return value.size() == 64 &&
           std::ranges::all_of(value, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}
} // namespace

AppPaths::AppPaths(std::filesystem::path localAppData)
    : root_(std::move(localAppData) / "HeartGoldPCRemastered") {}

std::filesystem::path AppPaths::root() const {
    return root_;
}

std::filesystem::path AppPaths::saveDirectory(std::string_view sha256) const {
    if (!isValidSha256(sha256)) {
        return {};
    }
    return root_ / "saves" / std::string(sha256);
}
