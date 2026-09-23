#pragma once

#include "core/emulation/EmulationTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

class FrameMailbox {
public:
    FrameMailbox() : storage_(std::make_shared<Storage>()) {}

    void publish(std::shared_ptr<const VideoFrame> frame) noexcept {
        if (!frame || frame->width != kDsScreenWidth || frame->height != kDsScreenHeight) {
            return;
        }

        const auto current = storage_->latest.load(std::memory_order_acquire);
        for (std::size_t offset = 1; offset <= kSlotCount; ++offset) {
            const auto index = current < 0
                                   ? offset - 1
                                   : (static_cast<std::size_t>(current) + offset) % kSlotCount;
            if (static_cast<int>(index) == current) {
                continue;
            }

            auto expected = std::uint32_t{0};
            auto& slot = storage_->slots[index];
            if (!slot.state.compare_exchange_strong(expected, kWriterClaim,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                continue;
            }

            slot.frame = *frame;
            slot.state.store(0, std::memory_order_release);
            storage_->latest.store(static_cast<int>(index), std::memory_order_release);
            return;
        }
    }

    std::shared_ptr<const VideoFrame> latest() const noexcept {
        auto storage = storage_;
        for (std::size_t attempt = 0; attempt < kSlotCount; ++attempt) {
            const auto index = storage->latest.load(std::memory_order_acquire);
            if (index < 0) {
                return {};
            }

            auto& slot = storage->slots[static_cast<std::size_t>(index)];
            auto state = slot.state.load(std::memory_order_acquire);
            if ((state & kWriterClaim) != 0 || state == kReaderCountMask) {
                continue;
            }
            if (!slot.state.compare_exchange_strong(state, state + 1,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                continue;
            }

            try {
                auto lease = std::make_shared<ReadLease>(storage,
                                                         static_cast<std::size_t>(index));
                return std::shared_ptr<const VideoFrame>(std::move(lease), &slot.frame);
            } catch (...) {
                slot.state.fetch_sub(1, std::memory_order_release);
                return {};
            }
        }
        return {};
    }

private:
    static constexpr std::size_t kSlotCount = 3;
    static constexpr std::uint32_t kWriterClaim = std::uint32_t{1} << 31;
    static constexpr std::uint32_t kReaderCountMask = kWriterClaim - 1;

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<int>::is_always_lock_free);

    struct Slot {
        std::atomic<std::uint32_t> state{0};
        VideoFrame frame;
    };

    struct Storage {
        std::array<Slot, kSlotCount> slots;
        std::atomic<int> latest{-1};
    };

    struct ReadLease {
        ReadLease(std::shared_ptr<Storage> storage, std::size_t index)
            : storage(std::move(storage)), index(index) {}

        ~ReadLease() {
            storage->slots[index].state.fetch_sub(1, std::memory_order_release);
        }

        std::shared_ptr<Storage> storage;
        std::size_t index;
    };

    std::shared_ptr<Storage> storage_;
};
