#pragma once
#include "emulation/melonds/MelonDsAdapter.h"

// Test utilities are compiled only into the adapter test executable.
struct MelonDsTestAccess {
    static EmulationResult startFreeBios(MelonDsAdapter& adapter) {
        return adapter.startWorker(true);
    }
    static void signalPowerOff(MelonDsAdapter&);
    static void writeSave(MelonDsAdapter&, std::span<const std::uint8_t>, unsigned offset, unsigned length);
    static bool bufferSizesAccepted(std::uint64_t romSize, std::uint64_t saveSize);
    static EmulationResult inspectCartridge(const EmulationConfig&, const SaveRepository&, std::vector<std::uint8_t>&);
    static std::vector<std::uint8_t> writeGeneratedFirmware(MelonDsAdapter&);
};
