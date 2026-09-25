#include "platform/sdl/SdlRuntime.h"

#include <algorithm>
#include <cmath>
#include <QMetaObject>
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
    api_.shutdown();
}

void SdlRuntime::pump() {
    if (!initialized_) return;
    while (const auto event = api_.pollEvent()) handle(*event);
}

void SdlRuntime::emitAction(Gamepad& pad, InputAction action, bool pressed) {
    const auto index = actionIndex(action);
    if (pad.emitted[index] == pressed) return;
    pad.emitted[index] = pressed;
    if (pressed && activeDevice_ != pad.kind) {
        activeDevice_ = pad.kind;
        emit activeDeviceChanged(activeDevice_);
    }
    emit inputEvent({action, pressed ? InputPhase::Pressed : InputPhase::Released, pad.kind});
}

void SdlRuntime::updateDirection(Gamepad& pad, InputAction action) {
    const auto index = actionIndex(action);
    emitAction(pad, action, pad.buttons[index] || pad.axisDirections[index]);
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
        for (std::size_t i = 0; i < it->second.emitted.size(); ++i) {
            if (it->second.emitted[i]) emitAction(it->second, i < 4 ? directionFor(i) :
                (i == 4 ? InputAction::Confirm : i == 5 ? InputAction::Back : InputAction::OpenMenu), false);
        }
        api_.closeGamepad(event.id);
        gamepads_.erase(it);
        if (activeId_ == event.id) {
            activeId_ = -1;
            const auto next = gamepads_.empty() ? InputDeviceKind::Keyboard : gamepads_.begin()->second.kind;
            if (next != activeDevice_) { activeDevice_ = next; emit activeDeviceChanged(next); }
        }
        return;
    }
    auto it = gamepads_.find(event.id);
    if (it == gamepads_.end()) return;
    auto& pad = it->second;
    if (event.type == SdlApi::EventType::AxisMotion) {
        const auto base = event.axis == SdlApi::Axis::LeftX ? 2 : 0;
        if (event.axis == SdlApi::Axis::Unknown) return;
        const bool negative = event.value <= -16000;
        const bool positive = event.value >= 16000;
        pad.axisDirections[base] = negative;
        pad.axisDirections[base + 1] = positive;
        updateDirection(pad, directionFor(base));
        updateDirection(pad, directionFor(base + 1));
        if (negative || positive) activeId_ = event.id;
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
    pad.buttons[index] = pressed;
    if (index < 4) updateDirection(pad, action);
    else emitAction(pad, action, pressed);
    if (pressed) activeId_ = event.id;
}

void SdlRuntime::setVolume(float volume) noexcept {
    volume_ = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
}

void SdlRuntime::submitAudio(std::span<const std::int16_t> samples) {
    if (!audioAvailable_ || samples.empty()) return;
    const auto completeFrames = samples.first(samples.size() / 2 * 2);
    const auto count = std::min(completeFrames.size(), maxAudioSamples);
    if (count == 0) return;
    const auto tail = completeFrames.last(count);
    std::vector<std::int16_t> scaled;
    scaled.reserve(count);
    for (const auto sample : tail) {
        const auto value = static_cast<int>(std::lround(static_cast<float>(sample) * volume_));
        scaled.push_back(static_cast<std::int16_t>(std::clamp(value, -32768, 32767)));
    }
    if (!api_.putAudio(scaled)) {
        audioAvailable_ = false;
        emit audioError(QStringLiteral("SDL audio submission failed"));
    }
}
