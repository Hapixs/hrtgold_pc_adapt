#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

enum class SaveWriteError { None, InvalidHash, NotWritable, RenameFailed };

class SaveRepository {
public:
    SaveRepository(std::filesystem::path directory,
                   std::function<std::chrono::system_clock::time_point()> clock);
    SaveWriteError replace(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> load() const;
    std::filesystem::path activeSavePath() const { return directory_ / "game.sav"; }

private:
    std::filesystem::path directory_;
    std::function<std::chrono::system_clock::time_point()> clock_;
};
