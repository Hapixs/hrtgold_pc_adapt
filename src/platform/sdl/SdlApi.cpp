#include "platform/sdl/SdlApi.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>

struct Sdl3Api::Impl {
    std::map<int, SDL_Gamepad*> gamepads;
    std::deque<Event> initialEvents;
    SDL_AudioStream* audio{nullptr};
    bool inputInitialized{false};
    bool audioInitialized{false};
};

namespace {
SdlApi::Button buttonFromSdl(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return SdlApi::Button::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return SdlApi::Button::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return SdlApi::Button::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return SdlApi::Button::DpadRight;
    case SDL_GAMEPAD_BUTTON_SOUTH: return SdlApi::Button::South;
    case SDL_GAMEPAD_BUTTON_EAST: return SdlApi::Button::East;
    case SDL_GAMEPAD_BUTTON_START: return SdlApi::Button::Start;
    default: return SdlApi::Button::Unknown;
    }
}

InputDeviceKind kindFromSdl(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return InputDeviceKind::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return InputDeviceKind::PlayStation;
    default: return InputDeviceKind::GenericGamepad;
    }
}
}

Sdl3Api::Sdl3Api() : impl_(std::make_unique<Impl>()) {}
Sdl3Api::~Sdl3Api() { shutdown(); }

bool Sdl3Api::init() {
    if (impl_->inputInitialized) return true;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) return false;
    impl_->inputInitialized = true;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        impl_->initialEvents.push_back({EventType::GamepadAdded, static_cast<int>(ids[i])});
    }
    SDL_free(ids);
    return true;
}

std::optional<SdlApi::Event> Sdl3Api::pollEvent() {
    if (!impl_->initialEvents.empty()) {
        const auto event = impl_->initialEvents.front();
        impl_->initialEvents.pop_front();
        return event;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED: return Event{EventType::GamepadAdded, static_cast<int>(event.gdevice.which)};
        case SDL_EVENT_GAMEPAD_REMOVED: return Event{EventType::GamepadRemoved, static_cast<int>(event.gdevice.which)};
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            return Event{event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? EventType::ButtonDown : EventType::ButtonUp,
                         static_cast<int>(event.gbutton.which), buttonFromSdl(event.gbutton.button)};
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            return Event{EventType::AxisMotion, static_cast<int>(event.gaxis.which), Button::Unknown,
                         event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX ? Axis::LeftX :
                         event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY ? Axis::LeftY : Axis::Unknown,
                         event.gaxis.value};
        default: break;
        }
    }
    return std::nullopt;
}

std::optional<InputDeviceKind> Sdl3Api::openGamepad(int id) {
    if (impl_->gamepads.contains(id)) return kindFromSdl(SDL_GetGamepadType(impl_->gamepads.at(id)));
    auto* pad = SDL_OpenGamepad(static_cast<SDL_JoystickID>(id));
    if (!pad) return std::nullopt;
    impl_->gamepads[id] = pad;
    return kindFromSdl(SDL_GetGamepadType(pad));
}

void Sdl3Api::closeGamepad(int id) {
    const auto it = impl_->gamepads.find(id);
    if (it == impl_->gamepads.end()) return;
    SDL_CloseGamepad(it->second);
    impl_->gamepads.erase(it);
}

bool Sdl3Api::openAudioStream() {
    if (impl_->audio) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    impl_->audioInitialized = true;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    impl_->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!impl_->audio) return false;
    return SDL_ResumeAudioStreamDevice(impl_->audio);
}

bool Sdl3Api::putAudio(std::span<const std::int16_t> samples) {
    if (!impl_->audio) return false;
    constexpr int maxBytes = 48000 * 2 * sizeof(std::int16_t) / 4;
    const auto bytes = samples.size_bytes();
    if (samples.size() % 2 != 0 || bytes > static_cast<std::size_t>(maxBytes)) return false;
    const int headroom = maxBytes - static_cast<int>(bytes);
    int queued = SDL_GetAudioStreamQueued(impl_->audio);
    if (queued < 0) return false;
    if (queued > headroom) {
        SDL_AudioSpec output{};
        if (!SDL_GetAudioStreamFormat(impl_->audio, nullptr, &output) ||
            output.freq <= 0 || output.channels <= 0 || SDL_AUDIO_BYTESIZE(output.format) == 0) return false;
        const int outputFrameBytes = SDL_AUDIO_BYTESIZE(output.format) * output.channels;
        std::array<std::byte, 4096> discarded{};
        if (outputFrameBytes > static_cast<int>(discarded.size())) return false;
        int stalledReads = 0;
        while (queued > headroom) {
            const int excessInputFrames = (queued - headroom + 3) / 4;
            const auto outputFrames = (static_cast<std::int64_t>(excessInputFrames) * output.freq + 47999) / 48000;
            const int requestFrames = static_cast<int>(std::min<std::int64_t>(
                std::max<std::int64_t>(outputFrames, 1), discarded.size() / outputFrameBytes));
            const int drained = SDL_GetAudioStreamData(impl_->audio, discarded.data(), requestFrames * outputFrameBytes);
            if (drained <= 0) {
                // A stalled or failed drain must not leave an unbounded backlog.
                SDL_ClearAudioStream(impl_->audio);
                return false;
            }
            const int nextQueued = SDL_GetAudioStreamQueued(impl_->audio);
            if (nextQueued < 0 || nextQueued > queued ||
                (nextQueued == queued && ++stalledReads >= 16)) {
                SDL_ClearAudioStream(impl_->audio);
                return false;
            }
            if (nextQueued < queued) stalledReads = 0;
            queued = nextQueued;
        }
    }
    return SDL_PutAudioStreamData(impl_->audio, samples.data(), static_cast<int>(bytes));
}

void Sdl3Api::shutdown() {
    for (const auto& [id, pad] : impl_->gamepads) SDL_CloseGamepad(pad);
    impl_->gamepads.clear();
    impl_->initialEvents.clear();
    if (impl_->audio) { SDL_DestroyAudioStream(impl_->audio); impl_->audio = nullptr; }
    if (impl_->audioInitialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); impl_->audioInitialized = false; }
    if (impl_->inputInitialized) { SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS); impl_->inputInitialized = false; }
}
