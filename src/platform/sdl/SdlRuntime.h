#pragma once

#include "platform/sdl/SdlApi.h"

#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <map>
#include <span>

Q_DECLARE_METATYPE(InputEvent)
Q_DECLARE_METATYPE(InputDeviceKind)

class SdlRuntime final : public QObject {
    Q_OBJECT
public:
    explicit SdlRuntime(SdlApi& api, QObject* parent = nullptr);
    ~SdlRuntime() override;

    void pump();
    void submitAudio(std::span<const std::int16_t> samples);
    void setVolume(float volume) noexcept;
    bool audioAvailable() const noexcept { return audioAvailable_; }
    InputDeviceKind activeDevice() const noexcept { return activeDevice_; }

signals:
    void inputEvent(InputEvent event);
    void activeDeviceChanged(InputDeviceKind kind);
    void audioError(QString message);

private:
    struct Gamepad {
        InputDeviceKind kind;
        std::array<bool, 7> buttons{};
        std::array<bool, 4> axisDirections{};
        std::array<bool, 7> emitted{};
    };

    void handle(const SdlApi::Event& event);
    void emitAction(Gamepad& pad, InputAction action, bool pressed);
    void updateDirection(Gamepad& pad, InputAction action);

    SdlApi& api_;
    std::map<int, Gamepad> gamepads_;
    InputDeviceKind activeDevice_{InputDeviceKind::Keyboard};
    int activeId_{-1};
    float volume_{1.0f};
    bool initialized_{false};
    bool audioAvailable_{false};
};
