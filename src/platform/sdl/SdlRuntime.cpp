#include "platform/sdl/SdlRuntime.h"

#include <algorithm>
#include <cmath>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <vector>

namespace {
constexpr std::size_t maxAudioSamples = 48000 * 2 / 4;

std::size_t actionIndex(InputAction action) {
    switch (action) {
    case InputAction::MoveUp: return 0;
    case InputAction::MoveDown: return 1;
    case InputAction::MoveLeft: return 2;
    case InputAction::MoveRight: return 3;
    case InputAction::Confirm: return 4;
    case InputAction::Back: return 5;
    case InputAction::OpenMenu: return 6;
    default: return 6;
    }
}

InputAction directionFor(std::size_t index) {
    constexpr std::array directions{InputAction::MoveUp, InputAction::MoveDown,
                                    InputAction::MoveLeft, InputAction::MoveRight};
    return directions[index];
}

InputAction actionFor(std::size_t index) {
    if (index < 4) return directionFor(index);
    return index == 4 ? InputAction::Confirm :
           index == 5 ? InputAction::Back : InputAction::OpenMenu;
}

std::size_t kindIndex(InputDeviceKind kind) {
    switch (kind) {
    case InputDeviceKind::Keyboard: return 0;
    case InputDeviceKind::Xbox: return 1;
    case InputDeviceKind::PlayStation: return 2;
    case InputDeviceKind::GenericGamepad: return 3;
    }
    return 0;
}
}

SdlRuntime::SdlRuntime(SdlApi& api, QObject* parent) : QObject(parent), api_(api) {
    qRegisterMetaType<InputEvent>();
    qRegisterMetaType<InputDeviceKind>();
    initialized_ = api_.init();
    if (initialized_) audioAvailable_ = api_.openAudioStream();
    if (!initialized_ || !audioAvailable_) {
        const auto message = initialized_ ? QStringLiteral("SDL audio is unavailable") :
                                            QStringLiteral("SDL input initialization failed");
        QMetaObject::invokeMethod(this, [this, message] { emit audioError(message); }, Qt::QueuedConnection);
    }
}

SdlRuntime::~SdlRuntime() {
    for (const auto& [id, unused] : gamepads_) api_.closeGamepad(id);
    std::lock_guard lock(audioMutex_);
    audioShuttingDown_ = true;
    audioAvailable_.store(false);
    api_.shutdown();
}

void SdlRuntime::pump() {
    if (!initialized_ || pumping_) return;
    QScopedValueRollback guard(pumping_, true);
    while (const auto event = api_.pollEvent()) handle(*event);
}

void SdlRuntime::setHeld(int id, InputAction action, bool pressed) {
    const auto index = actionIndex(action);
    InputDeviceKind kind;
    bool aggregateChanged = false;
    {
        auto it = gamepads_.find(id);
        if (it == gamepads_.end() || it->second.held[index] == pressed) return;
        kind = it->second.kind;
        it->second.held[index] = pressed;
        auto& count = heldCounts_[kindIndex(kind)][index];
        if (pressed) aggregateChanged = count++ == 0;
        else aggregateChanged = --count == 0;
    }
    if (pressed) activeId_ = id;
    if (pressed && activeDevice_ != kind) {
        activeDevice_ = kind;
        emit activeDeviceChanged(activeDevice_);
    }
    if (aggregateChanged) emit inputEvent({action, pressed ? InputPhase::Pressed : InputPhase::Released, kind});
}

void SdlRuntime::updateDirection(int id, InputAction action) {
    const auto index = actionIndex(action);
    const auto it = gamepads_.find(id);
    if (it == gamepads_.end()) return;
    const bool pressed = it->second.buttons[index] || it->second.axisDirections[index];
    setHeld(id, action, pressed);
}

void SdlRuntime::handle(const SdlApi::Event& event) {
    if (event.type == SdlApi::EventType::GamepadAdded) {
        if (!gamepads_.contains(event.id)) {
            if (const auto kind = api_.openGamepad(event.id)) gamepads_.emplace(event.id, Gamepad{*kind});
        }
        return;
    }
    if (event.type == SdlApi::EventType::GamepadRemoved) {
        auto it = gamepads_.find(event.id);
        if (it == gamepads_.end()) return;
        const auto removed = it->second;
        gamepads_.erase(it);
        std::array<bool, 7> released{};
        for (std::size_t i = 0; i < removed.held.size(); ++i) {
            if (removed.held[i]) released[i] = --heldCounts_[kindIndex(removed.kind)][i] == 0;
        }
        api_.closeGamepad(event.id);
        bool deviceChanged = false;
        if (activeId_ == event.id) {
            activeId_ = -1;
            const auto next = gamepads_.empty() ? InputDeviceKind::Keyboard : gamepads_.begin()->second.kind;
            deviceChanged = next != activeDevice_;
            activeDevice_ = next;
        }
        for (std::size_t i = 0; i < released.size(); ++i) {
            if (released[i]) emit inputEvent({actionFor(i), InputPhase::Released, removed.kind});
        }
        if (deviceChanged) emit activeDeviceChanged(activeDevice_);
        return;
    }
    if (event.type == SdlApi::EventType::AxisMotion) {
        const auto base = event.axis == SdlApi::Axis::LeftX ? 2 : 0;
        if (event.axis == SdlApi::Axis::Unknown) return;
        const bool negative = event.value <= -16000;
        const bool positive = event.value >= 16000;
        {
            auto it = gamepads_.find(event.id);
            if (it == gamepads_.end()) return;
            it->second.axisDirections[base] = negative;
            it->second.axisDirections[base + 1] = positive;
        }
        updateDirection(event.id, directionFor(base));
        updateDirection(event.id, directionFor(base + 1));
        return;
    }
    const bool pressed = event.type == SdlApi::EventType::ButtonDown;
    InputAction action;
    switch (event.button) {
    case SdlApi::Button::DpadUp: action = InputAction::MoveUp; break;
    case SdlApi::Button::DpadDown: action = InputAction::MoveDown; break;
    case SdlApi::Button::DpadLeft: action = InputAction::MoveLeft; break;
    case SdlApi::Button::DpadRight: action = InputAction::MoveRight; break;
    case SdlApi::Button::South: action = InputAction::Confirm; break;
    case SdlApi::Button::East: action = InputAction::Back; break;
    case SdlApi::Button::Start: action = InputAction::OpenMenu; break;
    default: return;
    }
    const auto index = actionIndex(action);
    {
        auto it = gamepads_.find(event.id);
        if (it == gamepads_.end()) return;
        it->second.buttons[index] = pressed;
    }
    if (index < 4) updateDirection(event.id, action);
    else setHeld(event.id, action, pressed);
}

void SdlRuntime::setVolume(float volume) noexcept {
    volume_.store(std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f);
}

void SdlRuntime::submitAudio(std::span<const std::int16_t> samples) {
    if (!audioAvailable_.load() || samples.empty()) return;
    const auto completeFrames = samples.first(samples.size() / 2 * 2);
    const auto count = std::min(completeFrames.size(), maxAudioSamples);
    if (count == 0) return;
    const auto tail = completeFrames.last(count);
    const auto volume = volume_.load();
    std::vector<std::int16_t> scaled;
    scaled.reserve(count);
    for (const auto sample : tail) {
        const auto value = static_cast<int>(std::lround(static_cast<float>(sample) * volume));
        scaled.push_back(static_cast<std::int16_t>(std::clamp(value, -32768, 32767)));
    }
    {
        std::lock_guard lock(audioMutex_);
        if (audioShuttingDown_ || !audioAvailable_.load()) return;
        if (!api_.putAudio(scaled)) {
            audioAvailable_.store(false);
            QMetaObject::invokeMethod(this, [this] {
                emit audioError(QStringLiteral("SDL audio submission failed"));
            }, Qt::QueuedConnection);
        }
    }
}
