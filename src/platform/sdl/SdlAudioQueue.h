#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_audio {

constexpr int maxQueuedBytes = 48000 * 2 * sizeof(std::int16_t) / 4;

// Queue must provide queuedBytes, drainOldestFrames, clearQueued, and putAudioSamples.
// A converter may temporarily produce no output despite having queued input;
// in that case discard the undrainable backlog and keep the newest submission.
template <typename Queue>
bool submitBoundedAudio(Queue& queue, std::span<const std::int16_t> samples) {
    const auto bytes = samples.size_bytes();
    if (samples.size() % 2 != 0 || bytes > static_cast<std::size_t>(maxQueuedBytes)) return false;
    const int headroom = maxQueuedBytes - static_cast<int>(bytes);
    int queued = queue.queuedBytes();
    if (queued < 0) return false;
    int stalledReads = 0;
    while (queued > headroom) {
        const int excessInputFrames = (queued - headroom + 3) / 4;
        const int drained = queue.drainOldestFrames(excessInputFrames);
        if (drained < 0) return false;
        if (drained == 0) {
            if (!queue.clearQueued()) return false;
            break;
        }
        const int nextQueued = queue.queuedBytes();
        if (nextQueued < 0 || nextQueued > queued) return false;
        if (nextQueued == queued && ++stalledReads >= 16) {
            if (!queue.clearQueued()) return false;
            break;
        }
        if (nextQueued < queued) stalledReads = 0;
        queued = nextQueued;
    }
    return queue.putAudioSamples(samples);
}

} // namespace sdl_audio
