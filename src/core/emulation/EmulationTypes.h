#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

constexpr int kDsScreenWidth = 256;
constexpr int kDsScreenHeight = 192;

struct VideoFrame {
    std::uint64_t sequence{};
    int width{kDsScreenWidth};
    int height{kDsScreenHeight};
    std::array<std::uint32_t, kDsScreenWidth * kDsScreenHeight> rgba{};
};

using AudioChunk = std::vector<std::int16_t>;

enum class DsButton : std::uint16_t {
    A = 1 << 0,
    B = 1 << 1,
    Select = 1 << 2,
    Start = 1 << 3,
    Right = 1 << 4,
    Left = 1 << 5,
    Up = 1 << 6,
    Down = 1 << 7,
    R = 1 << 8,
    L = 1 << 9,
    X = 1 << 10,
    Y = 1 << 11,
};

using DsButtonMask = std::uint16_t;

enum class EmulationErrorCode {
    None,
    InvalidRom,
    MissingPrivateFile,
    SaveIo,
    AudioUnavailable,
    CoreFailure,
};

struct EmulationResult {
    EmulationErrorCode code{EmulationErrorCode::None};
    std::string message;

    bool ok() const noexcept { return code == EmulationErrorCode::None; }
};

struct EmulationConfig {
    std::filesystem::path romPath;
    std::filesystem::path savePath;
    std::string romSha256;
    int sampleRate{48000};
    bool useDirectBoot{true};
};
