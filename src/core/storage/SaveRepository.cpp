#include "core/storage/SaveRepository.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
bool isValidSha256(std::string_view value) {
    return value.size() == 64 &&
           std::ranges::all_of(value, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool hasWritePermission(const std::filesystem::path& directory,
                        std::error_code& error) {
    const auto permissions = std::filesystem::status(directory, error).permissions();
    if (error) {
        return false;
    }
    constexpr auto writePermissions = std::filesystem::perms::owner_write |
                                      std::filesystem::perms::group_write |
                                      std::filesystem::perms::others_write;
    return (permissions & writePermissions) != std::filesystem::perms::none;
}

std::string timestamp(
    const std::chrono::system_clock::time_point& timePoint) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  timePoint.time_since_epoch())
                                  .count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::UTC)
        .toString(QStringLiteral("yyyyMMdd-HHmmss"))
        .toStdString();
}

bool replaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& active,
                 std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), active.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, active, error);
    return !error;
#endif
}
} // namespace

SaveRepository::SaveRepository(
    std::filesystem::path directory,
    std::function<std::chrono::system_clock::time_point()> clock)
    : directory_(std::move(directory)), clock_(std::move(clock)) {}

SaveWriteError SaveRepository::replace(std::span<const std::uint8_t> bytes) {
    if (!isValidSha256(directory_.filename().string())) {
        return SaveWriteError::InvalidHash;
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error || !hasWritePermission(directory_, error)) {
        return SaveWriteError::NotWritable;
    }

    const auto temporary = directory_ / "game.sav.tmp";
    const auto active = directory_ / "game.sav";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return SaveWriteError::NotWritable;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return SaveWriteError::NotWritable;
        }
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return SaveWriteError::NotWritable;
        }
    }

    if (std::filesystem::is_regular_file(active, error)) {
        const auto backup = directory_ /
                            ("game-" + timestamp(clock_()) + ".sav.bak");
        std::filesystem::copy_file(active, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return SaveWriteError::NotWritable;
        }
    } else if (error) {
        std::filesystem::remove(temporary, error);
        return SaveWriteError::NotWritable;
    }

    if (!replaceFile(temporary, active, error)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return SaveWriteError::RenameFailed;
    }

    return SaveWriteError::None;
}

std::vector<std::uint8_t> SaveRepository::load() const {
    std::ifstream input(directory_ / "game.sav", std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}
