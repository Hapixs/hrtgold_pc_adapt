#pragma once

#include "core/emulation/EmulationTypes.h"

#include <memory>

class EmulatorCore {
public:
    virtual ~EmulatorCore() = default;

    virtual EmulationResult load(const EmulationConfig& config) = 0;
    virtual EmulationResult start() = 0;
    virtual EmulationResult pause() = 0;
    virtual EmulationResult resume() = 0;
    virtual EmulationResult stop() = 0;
    virtual void setButtons(DsButtonMask pressed) = 0;
    virtual std::shared_ptr<const VideoFrame> latestFrame() const = 0;
    virtual bool isRunning() const noexcept = 0;
};
