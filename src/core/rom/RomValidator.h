#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

enum class RomStatus {
    ValidHeartGold,
    FileNotFound,
    IoError,
    TooSmall,
    MalformedHeader,
    UnsupportedGame
};

struct RomValidation {
    RomStatus status;
    std::string title;
    std::string gameCode;
    std::string sha256;
    std::uint64_t size;
};

class RomValidator {
public:
    RomValidation validate(const std::filesystem::path& path) const;
};
