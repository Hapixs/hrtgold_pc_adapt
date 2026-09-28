// SPDX-License-Identifier: GPL-3.0-or-later
#include "emulation/melonds/MelonRomLoader.h"
#include <fstream>
#include <limits>
#include <cstring>
#include <QCryptographicHash>

namespace {
bool validSegment(std::uint32_t offset, std::uint32_t size, std::uint32_t ram,
                  std::uint32_t entry, std::uint32_t romSize, bool arm7) {
    if (!size || (size & 3) || (offset & 3) || (ram & 3) || offset < 0x4000 ||
        offset > romSize || size > romSize - offset) return false;
    const auto ramEnd = static_cast<std::uint64_t>(ram) + size;
    const bool mainRam = ram >= 0x02000000 && ramEnd <= 0x02400000;
    const bool arm7Ram = arm7 && ram >= 0x03800000 && ramEnd <= 0x03810000;
    const auto instruction = entry & ~1U;
    return (mainRam || arm7Ram) && instruction >= ram && instruction < ramEnd;
}
}

bool validCoreBufferSizes(std::uint64_t romSize, std::uint64_t saveSize) noexcept {
    // PadToPowerOf2 at the pinned revision loops forever above 2^31.
    return romSize >= sizeof(melonDS::NDSHeader) && romSize <= 0x80000000ULL &&
           saveSize <= std::numeric_limits<std::uint32_t>::max();
}
std::unique_ptr<melonDS::NDSCart::CartCommon> loadCartridge(
    const EmulationConfig& config, const SaveRepository& saves, void* userdata) {
    std::ifstream stream(config.romPath, std::ios::binary | std::ios::ate);
    const auto end = stream.tellg();
    if (!stream || end < 0 || !validCoreBufferSizes(static_cast<std::uint64_t>(end), 0))
        throw EmulationResult{EmulationErrorCode::InvalidRom, "The ROM size is invalid."};
    const auto length = static_cast<std::uint32_t>(end);
    auto bytes = std::make_unique<std::uint8_t[]>(length);
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.get()), length))
        throw EmulationResult{EmulationErrorCode::InvalidRom, "The ROM could not be read."};
    const auto hash = QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(bytes.get()), length), QCryptographicHash::Sha256).toHex().toStdString();
    melonDS::NDSHeader header{};
    std::memcpy(&header, bytes.get(), sizeof(header));
    if (hash != config.romSha256 || std::memcmp(header.GameCode, "IPK", 3) != 0 || header.UnitCode != 0 ||
        !validSegment(header.ARM9ROMOffset, header.ARM9Size, header.ARM9RAMAddress, header.ARM9EntryAddress, length, false) ||
        !validSegment(header.ARM7ROMOffset, header.ARM7Size, header.ARM7RAMAddress, header.ARM7EntryAddress, length, true) ||
        (header.ARM9ROMOffset < 0x8000 && (header.ARM9Size < 0x800 || length - header.ARM9ROMOffset < 0x800)))
        throw EmulationResult{EmulationErrorCode::InvalidRom, "The ROM hash or direct-boot header is invalid."};
    std::error_code error;
    auto expectedSaveSize = std::filesystem::file_size(config.savePath, error);
    if (error == std::errc::no_such_file_or_directory) { expectedSaveSize = 0; error.clear(); }
    // 64 MiB is the largest SRAM capacity supported by this pinned core.
    if (error || !validCoreBufferSizes(length, expectedSaveSize) || expectedSaveSize > 64ULL*1024*1024)
        throw EmulationResult{EmulationErrorCode::SaveIo, "The save could not be read or exceeds the supported size."};
    auto save = saves.load();
    if (save.size() != expectedSaveSize)
        throw EmulationResult{EmulationErrorCode::SaveIo, "The save could not be read completely."};
    if (!validCoreBufferSizes(length, save.size()))
        throw EmulationResult{EmulationErrorCode::SaveIo, "The save size is invalid."};
    auto sram = std::make_unique<std::uint8_t[]>(save.size());
    std::copy(save.begin(), save.end(), sram.get());
    auto cart = melonDS::NDSCart::ParseROM(std::move(bytes), length, userdata,
        melonDS::NDSCart::NDSCartArgs{std::nullopt, std::move(sram), static_cast<std::uint32_t>(save.size())});
    if (!cart) throw EmulationResult{EmulationErrorCode::InvalidRom, "The cartridge could not be parsed."};
    // The core silently truncates oversized SRAM. Reject it before installing
    // the cartridge so the original file is never overwritten with a truncation.
    if (save.size() > cart->GetSaveMemoryLength())
        throw EmulationResult{EmulationErrorCode::SaveIo, "The save is larger than the cartridge save memory."};
    return cart;
}
