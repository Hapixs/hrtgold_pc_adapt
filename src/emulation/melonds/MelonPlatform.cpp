/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/
// Platform contract and stdio/dynamic-library wrappers adapted from
// src/frontend/qt_sdl/Platform.cpp at the pin documented in THIRD_PARTY.md.
// HeartGold supplies its own context, std synchronization and inactive devices.
#include "emulation/melonds/MelonPlatformContext.h"
#include "Platform.h"
#include "SPI_Firmware.h"
#include <SDL3/SDL_loadso.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <limits>
#include <thread>

namespace {
thread_local MelonPlatformContext* currentContext = nullptr;
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
std::filesystem::path ownedPath(const std::string& path) {
    if (!currentContext || path.empty()) return {};
    std::error_code error;
    const auto input = std::filesystem::path(std::u8string(path.begin(), path.end()));
    if (!input.is_absolute() || !currentContext->dataDirectory.is_absolute()) return {};
    const auto root = std::filesystem::weakly_canonical(currentContext->dataDirectory, error);
    if (error) return {};
    const auto candidate = std::filesystem::weakly_canonical(input, error);
    if (error) return {};
    auto part = candidate.begin();
    for (const auto& component : root) {
        if (part == candidate.end()) return {};
        auto left = component.wstring(), right = part->wstring();
#ifdef _WIN32
        std::ranges::transform(left, left.begin(), [](wchar_t c){ return std::towlower(c); });
        std::ranges::transform(right, right.begin(), [](wchar_t c){ return std::towlower(c); });
#endif
        if (left != right) return {};
        ++part;
    }
    return candidate;
}
FILE* stdFile(melonDS::Platform::FileHandle* file) { return reinterpret_cast<FILE*>(file); }
bool validTransfer(melonDS::u64 size, melonDS::u64 count) {
    return size && count <= std::numeric_limits<std::size_t>::max()/size;
}
}

MelonPlatformScope::MelonPlatformScope(MelonPlatformContext& context) : previous_(currentContext) {
    currentContext = &context;
}
MelonPlatformScope::~MelonPlatformScope() { currentContext = previous_; }

namespace melonDS::Platform {
void SignalStop(StopReason reason, void* userdata) {
    if (auto* context = static_cast<MelonPlatformContext*>(userdata); context && context->stop)
        context->stop(static_cast<int>(reason));
}
std::string GetLocalFilePath(const std::string& filename) {
    if (!currentContext) return {};
    auto path = std::filesystem::path(std::u8string(filename.begin(), filename.end()));
    if (!path.is_absolute()) path = currentContext->dataDirectory / path;
    return utf8(ownedPath(utf8(path)));
}
FileHandle* OpenFile(const std::string& path, FileMode mode) {
    const auto safe = ownedPath(path);
    const bool write = mode & (Write | Append);
    if (safe.empty() || !(mode & (ReadWrite | Append))) return nullptr;
    std::error_code error;
    const bool exists = std::filesystem::exists(safe, error);
    if (error || (write && (mode & NoCreate) && !exists)) return nullptr;
    std::string access;
    if (mode & Append) access = (mode & Read) ? "a+" : "a";
    else if (!write) access = "r";
    else if ((mode & Preserve) && exists) access = "r+";
    else access = (mode & Read) ? "w+" : "w";
    if (!(mode & Text)) access += 'b';
#ifdef _WIN32
    const std::wstring wideMode(access.begin(), access.end());
    auto* file = _wfopen(safe.c_str(), wideMode.c_str());
#else
    auto* file = std::fopen(safe.c_str(), access.c_str());
#endif
    return reinterpret_cast<FileHandle*>(file);
}
FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(GetLocalFilePath(path), mode); }
bool CloseFile(FileHandle* file) { return file && std::fclose(stdFile(file)) == 0; }
bool IsEndOfFile(FileHandle* file) { return file && std::feof(stdFile(file)); }
bool FileReadLine(char* text, int count, FileHandle* file) {
    return file && text && count > 0 && std::fgets(text, count, stdFile(file));
}
bool FileExists(const std::string& path) { auto* file = OpenFile(path, Read); return file && CloseFile(file); }
bool LocalFileExists(const std::string& path) { return FileExists(GetLocalFilePath(path)); }
bool CheckFileWritable(const std::string& path) { auto* file = OpenFile(path, Append); return file && CloseFile(file); }
bool CheckLocalFileWritable(const std::string& path) { return CheckFileWritable(GetLocalFilePath(path)); }
bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) {
    if (!file) return false;
    int from;
    switch (origin) {
    case FileSeekOrigin::Start: from = SEEK_SET; break;
    case FileSeekOrigin::Current: from = SEEK_CUR; break;
    case FileSeekOrigin::End: from = SEEK_END; break;
    default: return false;
    }
#ifdef _WIN32
    return _fseeki64(stdFile(file), offset, from) == 0;
#else
    return fseeko(stdFile(file), offset, from) == 0;
#endif
}
u64 FilePosition(FileHandle* file) {
    if (!file) return 0;
#ifdef _WIN32
    const auto pos = _ftelli64(stdFile(file));
#else
    const auto pos = ftello(stdFile(file));
#endif
    return pos < 0 ? 0 : static_cast<u64>(pos);
}
void FileRewind(FileHandle* file) { if (file) std::rewind(stdFile(file)); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) {
    return file && data && validTransfer(size, count) ? std::fread(data, size, count, stdFile(file)) : 0;
}
bool FileFlush(FileHandle* file) { return file && std::fflush(stdFile(file)) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) {
    return file && data && validTransfer(size, count) ? std::fwrite(data, size, count, stdFile(file)) : 0;
}
u64 FileWriteFormatted(FileHandle* file, const char* format, ...) {
    if (!file || !format) return 0;
    va_list args;
    va_start(args, format);
    const auto count = std::vfprintf(stdFile(file), format, args);
    va_end(args);
    return count < 0 ? 0 : static_cast<u64>(count);
}
u64 FileLength(FileHandle* file) {
    const auto position = FilePosition(file);
    if (!FileSeek(file, 0, FileSeekOrigin::End)) return 0;
    const auto length = FilePosition(file);
    return FileSeek(file, static_cast<s64>(position), FileSeekOrigin::Start) ? length : 0;
}
void Log(LogLevel, const char* format, ...) {
    if (!currentContext || !format) return;
    std::array<char, 2048> buffer{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    try {
        std::string message(buffer.data());
        // Normalize separators/case for comparison, preserving the log message.
        auto normalized = [](std::string value) {
            for (auto& c : value) {
                if (c == '\\') c = '/';
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            }
            return value;
        };
        for (const auto& path : currentContext->sensitiveDirectories) {
            const auto prefix = normalized(utf8(path));
            if (prefix.empty()) continue;
            std::size_t position = 0;
            while ((position = normalized(message).find(prefix, position)) != std::string::npos) {
                message.replace(position, prefix.size(), "[redacted]");
                position += 10;
            }
        }
        std::lock_guard lock(currentContext->logMutex);
        std::error_code error;
        std::filesystem::create_directories(currentContext->dataDirectory, error);
        const auto path = ownedPath(utf8(currentContext->dataDirectory / "melonds.log"));
        const auto old = ownedPath(utf8(currentContext->dataDirectory / "melonds.log.1"));
        if (error || path.empty() || old.empty()) return;
        const auto size = std::filesystem::exists(path, error) ? std::filesystem::file_size(path, error) : 0;
        if (error) return;
        if (size + message.size() > 65536) {
            std::filesystem::remove(old, error);
            if (error) return;
            std::filesystem::rename(path, old, error);
            if (error) return;
        }
        std::ofstream output(path, std::ios::app | std::ios::binary);
        output << message;
    } catch (...) { /* Logging must never unwind through the emulated CPU. */ }
}
struct Thread { std::thread worker; };
Thread* Thread_Create(std::function<void()> func) {
    auto* context = currentContext;
    return new Thread{std::thread([context, func = std::move(func)] {
        if (context) { MelonPlatformScope scope(*context); func(); }
        else func();
    })};
}
void Thread_Wait(Thread* thread) { if (thread && thread->worker.joinable()) thread->worker.join(); }
void Thread_Free(Thread* thread) { Thread_Wait(thread); delete thread; }
struct Semaphore { std::mutex mutex; std::condition_variable changed; std::uint64_t count{}; };
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* sem) { delete sem; }
void Semaphore_Reset(Semaphore* sem) { std::lock_guard lock(sem->mutex); sem->count = 0; }
void Semaphore_Wait(Semaphore* sem) {
    std::unique_lock lock(sem->mutex);
    sem->changed.wait(lock, [sem]{ return sem->count > 0; });
    --sem->count;
}
bool Semaphore_TryWait(Semaphore* sem, int timeout) {
    std::unique_lock lock(sem->mutex);
    if (!sem->changed.wait_for(lock, std::chrono::milliseconds(std::max(0, timeout)), [sem]{ return sem->count > 0; })) return false;
    --sem->count;
    return true;
}
void Semaphore_Post(Semaphore* sem, int count) {
    if (count <= 0) return;
    { std::lock_guard lock(sem->mutex); sem->count += static_cast<unsigned>(count); }
    sem->changed.notify_all();
}
struct Mutex { std::mutex mutex; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* mutex) { delete mutex; }
void Mutex_Lock(Mutex* mutex) { mutex->mutex.lock(); }
void Mutex_Unlock(Mutex* mutex) { mutex->mutex.unlock(); }
bool Mutex_TryLock(Mutex* mutex) { return mutex->mutex.try_lock(); }
void Sleep(u64 microseconds) { std::this_thread::sleep_for(std::chrono::microseconds(microseconds)); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
u64 GetMSCount() { return GetUSCount()/1000; }
void WriteNDSSave(const u8* data, u32 length, u32, u32, void* userdata) {
    if (auto* context = static_cast<MelonPlatformContext*>(userdata); context && context->save && data && length)
        context->save({data, length});
}
void WriteGBASave(const u8*, u32, u32, u32, void*) {} // No GBA cartridge is installed.
void WriteFirmware(const Firmware& firmware, u32, u32, void* userdata) {
    if (auto* context = static_cast<MelonPlatformContext*>(userdata); context && context->firmware)
        context->firmware({firmware.Buffer(), firmware.Length()});
}
void WriteDateTime(int, int, int, int, int, int, void*) {} // RTC stays session-local.
void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }
void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32* frame, int width, int height, bool yuv, void*) {
    if (frame && width > 0 && height > 0 && width <= 640 && height <= 480)
        std::fill_n(frame, static_cast<std::size_t>(width)*height, yuv ? 0x80008000U : 0U);
}
void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16* data, int maximum, void*) {
    if (!data || maximum <= 0) return 0;
    std::fill_n(data, maximum, 0);
    return maximum;
}
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }
bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0.0f; }
DynamicLibrary* DynamicLibrary_Load(const char* name) { return reinterpret_cast<DynamicLibrary*>(SDL_LoadObject(name)); }
void DynamicLibrary_Unload(DynamicLibrary* lib) { SDL_UnloadObject(reinterpret_cast<SDL_SharedObject*>(lib)); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) {
    return reinterpret_cast<void*>(SDL_LoadFunction(reinterpret_cast<SDL_SharedObject*>(lib), name));
}
}
