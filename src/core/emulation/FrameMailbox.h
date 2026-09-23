#pragma once

#include "core/emulation/EmulationTypes.h"

#include <atomic>
#include <memory>

class FrameMailbox {
public:
    void publish(std::shared_ptr<const VideoFrame> frame) noexcept {
        if (!frame || frame->width != kDsScreenWidth || frame->height != kDsScreenHeight) {
            return;
        }
        latest_.store(std::move(frame), std::memory_order_release);
    }

    std::shared_ptr<const VideoFrame> latest() const noexcept {
        return latest_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::shared_ptr<const VideoFrame>> latest_{};
};
