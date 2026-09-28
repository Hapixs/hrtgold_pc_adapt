#pragma once
#include "core/emulation/EmulationTypes.h"
#include "core/storage/SaveRepository.h"
#include "NDSCart.h"
bool validCoreBufferSizes(std::uint64_t romSize, std::uint64_t saveSize) noexcept;
std::unique_ptr<melonDS::NDSCart::CartCommon> loadCartridge(
    const EmulationConfig& config, const SaveRepository& saves, void* userdata);
