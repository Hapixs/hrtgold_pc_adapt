#pragma once

#include "platform/sdl/SdlAudioSink.h"

#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <map>
#include <memory>

Q_DECLARE_METATYPE(InputEvent)
Q_DECLARE_METATYPE(InputDeviceKind)

class SdlRuntime final : public QObject {
    Q_OBJECT
public:
    explicit SdlRuntime(SdlApi& api, QObject* parent = nullptr);
    ~SdlRuntime() override;

    void pump();
    std::shared_ptr<SdlAudioSink> audioSink() const noexcept { return audioSink_; }
    void setVolume(float volume) noexcept;
    bool audioAvailable() const noexcept { return audioSink_->audioAvailable(); }
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
        std::array<bool, 7> held{};
    };

    void handle(const SdlApi::Event& event);
    void setHeld(int id, InputAction action, bool pressed);
    void updateDirection(int id, InputAction action);

    SdlApi& api_;
    std::map<int, Gamepad> gamepads_;
    std::array<std::array<unsigned, 7>, 4> heldCounts_{};
    InputDeviceKind activeDevice_{InputDeviceKind::Keyboard};
    int activeId_{-1};
    bool pumping_{false};
    std::shared_ptr<SdlAudioSink> audioSink_;
    bool initialized_{false};
};
