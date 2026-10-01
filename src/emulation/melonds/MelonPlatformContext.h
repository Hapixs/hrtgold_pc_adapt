#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

struct MelonPlatformContext {
    std::filesystem::path dataDirectory;
    std::vector<std::filesystem::path> sensitiveDirectories;
    std::mutex logMutex;
    std::function<void(std::span<const std::uint8_t>)> save;
    std::function<void(std::span<const std::uint8_t>)> firmware;
    std::function<void(int)> stop;
};

// The core's file/log/thread APIs have no userdata. Scope those APIs to this
// worker; spawned core threads inherit the context and must join before it dies.
class MelonPlatformScope {
public:
    explicit MelonPlatformScope(MelonPlatformContext& context);
    ~MelonPlatformScope();
    MelonPlatformScope(const MelonPlatformScope&) = delete;
    MelonPlatformScope& operator=(const MelonPlatformScope&) = delete;
private:
    MelonPlatformContext* previous_;
};
