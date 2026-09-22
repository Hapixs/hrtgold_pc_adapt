#include "core/rom/RomValidator.h"

#include <QCryptographicHash>
#include <QFile>

#include <cstddef>
#include <cstdint>

namespace {
constexpr std::uint64_t minimumHeaderSize = 0x200;

std::uint32_t readLittleEndian32(const QByteArray& bytes, std::size_t offset) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

std::string readTitle(const QByteArray& bytes) {
    std::string title(bytes.constData(), 12);
    if (const auto nul = title.find('\0'); nul != std::string::npos) {
        title.resize(nul);
    }
    while (!title.empty() && title.back() == ' ') {
        title.pop_back();
    }
    return title;
}

RomValidation resultWithStatus(RomStatus status) {
    return RomValidation{status, {}, {}, {}, 0};
}
} // namespace

RomValidation RomValidator::validate(const std::filesystem::path& path) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        return resultWithStatus(RomStatus::IoError);
    }
    if (!exists) {
        return resultWithStatus(RomStatus::FileNotFound);
    }

    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        return resultWithStatus(RomStatus::IoError);
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return resultWithStatus(RomStatus::IoError);
    }

    RomValidation result{
        RomStatus::TooSmall,
        {},
        {},
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
            .toHex()
            .toStdString(),
        static_cast<std::uint64_t>(bytes.size())};
    if (result.size < minimumHeaderSize) {
        return result;
    }

    result.title = readTitle(bytes);
    result.gameCode.assign(bytes.constData() + 0x0c, 4);

    const auto arm9Offset = static_cast<std::uint64_t>(readLittleEndian32(bytes, 0x20));
    const auto arm9Size = static_cast<std::uint64_t>(readLittleEndian32(bytes, 0x2c));
    if (arm9Offset > result.size || arm9Size > result.size - arm9Offset) {
        result.status = RomStatus::MalformedHeader;
        return result;
    }

    if (!result.gameCode.starts_with("IPK")) {
        result.status = RomStatus::UnsupportedGame;
        return result;
    }

    result.status = RomStatus::ValidHeartGold;
    return result;
}
