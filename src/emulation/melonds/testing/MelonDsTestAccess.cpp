#include "emulation/melonds/testing/MelonDsTestAccess.h"
#include "emulation/melonds/MelonDsAdapterInternal.h"
#include "Platform.h"
#include "emulation/melonds/MelonRomLoader.h"
#include "SPI_Firmware.h"
std::vector<std::uint8_t> MelonDsTestAccess::writeGeneratedFirmware(MelonDsAdapter& adapter) {
    melonDS::Firmware firmware(0);
    melonDS::Platform::WriteFirmware(firmware, 0, firmware.Length(), &adapter.impl_->platform);
    return {firmware.Buffer(), firmware.Buffer() + firmware.Length()};
}
bool MelonDsTestAccess::bufferSizesAccepted(std::uint64_t romSize, std::uint64_t saveSize) { return validCoreBufferSizes(romSize, saveSize); }
EmulationResult MelonDsTestAccess::inspectCartridge(const EmulationConfig& config, const SaveRepository& repository, std::vector<std::uint8_t>& data) {
    try {
        auto cart = loadCartridge(config, repository, nullptr);
        if (!cart) return {EmulationErrorCode::CoreFailure, "No cartridge"};
        data.assign(cart->GetSaveMemory(), cart->GetSaveMemory() + cart->GetSaveMemoryLength());
        return {};
    } catch (const EmulationResult& result) { return result; }
}
void MelonDsTestAccess::signalPowerOff(MelonDsAdapter& adapter) {
    melonDS::Platform::SignalStop(melonDS::Platform::PowerOff, &adapter.impl_->platform);
}
void MelonDsTestAccess::writeSave(MelonDsAdapter& adapter, std::span<const std::uint8_t> data, unsigned offset, unsigned length) {
    melonDS::Platform::WriteNDSSave(data.data(), static_cast<unsigned>(data.size()), offset, length, &adapter.impl_->platform);
}
