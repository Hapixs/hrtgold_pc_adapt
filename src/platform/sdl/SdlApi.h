#pragma once

#include "core/input/InputTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

class SdlApi {
public:
    enum class EventType { GamepadAdded, GamepadRemoved, ButtonDown, ButtonUp, AxisMotion };
    enum class Button { DpadUp, DpadDown, DpadLeft, DpadRight, South, East, Start, Unknown };
    enum class Axis { LeftX, LeftY, Unknown };
    struct Event {
        EventType type;
        int id;
        Button button{Button::Unknown};
        Axis axis{Axis::Unknown};
        int value{0};
    };

    virtual ~SdlApi() = default;
    virtual bool init() = 0;
    virtual std::optional<Event> pollEvent() = 0;
    virtual std::optional<InputDeviceKind> openGamepad(int id) = 0;
    virtual void closeGamepad(int id) = 0;
    virtual bool openAudioStream() = 0;
    // Implementations keep the SDL queue within 250 ms, discarding stale queued audio.
    virtual bool putAudio(std::span<const std::int16_t> samples) = 0;
    virtual void shutdown() = 0;
};

class Sdl3Api final : public SdlApi {
public:
    Sdl3Api();
    ~Sdl3Api() override;
    bool init() override;
    std::optional<Event> pollEvent() override;
    std::optional<InputDeviceKind> openGamepad(int id) override;
    void closeGamepad(int id) override;
    bool openAudioStream() override;
    bool putAudio(std::span<const std::int16_t> samples) override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
