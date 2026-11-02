#pragma once

// Discord Rich Presence over the local pipe of the Discord client (\\.\pipe\discord-ipc-0 to -9). One worker thread
// owns the pipe: Set() only stores what to show and wakes it, so the JavaScript thread never waits on Discord. When
// Discord is closed, the worker tries again every 15 s while something is waiting to be shown. The bytes come from
// src/core/discord_frame.*.

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "../core/discord_frame.hpp"
#include "../core/i18n.hpp"
#include "../core/journal.hpp"

namespace ampify::win {

class DiscordIpc {
public:
    // `pipeBase` is followed by 0 to 9. The tests keep the default name and skip when Discord holds it. Discord
    // accepts 5 updates in 20 s: changes closer than `minGap` wait, and only the latest one is sent.
    DiscordIpc(Journal& journal, std::wstring pipeBase = L"\\\\.\\pipe\\discord-ipc-",
               std::chrono::milliseconds minGap = std::chrono::milliseconds(4000));
    ~DiscordIpc();

    DiscordIpc(const DiscordIpc&) = delete;
    DiscordIpc& operator=(const DiscordIpc&) = delete;

    // Shows `activity` under the application `clientId`, or clears the presence when it is empty. Returns at once.
    void Set(const std::string& clientId, std::optional<discord::Activity> activity);

    // For the tests: activities that Discord answered without an error, since the start.
    int accepted() const;

private:
    void Run();
    bool Connect(const std::string& clientId);
    void Disconnect();
    bool Write(discord::Opcode opcode, const std::string& json);
    // The next frame from Discord, answering pings on the way. nullopt after `timeoutMs` or on a broken pipe.
    std::optional<discord::Frame> Read(DWORD timeoutMs);

    Journal& m_journal;
    std::wstring m_pipeBase;
    std::chrono::milliseconds m_minGap;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::string m_clientId;
    std::optional<discord::Activity> m_activity;
    bool m_dirty = false;
    bool m_stop = false;
    int m_accepted = 0;

    // Worker thread only.
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    std::string m_connectedClient;
    discord::FrameReader m_reader;
    uint64_t m_nonce = 0;
    bool m_warnedUnavailable = false;
    std::optional<std::chrono::steady_clock::time_point> m_lastSent;

    std::thread m_thread;
};

}  // namespace ampify::win
