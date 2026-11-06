#pragma once

// A Host for the tests: everything in memory, and every call is recorded so that a test can check it.

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../src/core/natives/keyboard.hpp"
#include "../src/core/natives/kit.hpp"

#include "../src/core/audio/audio_host.hpp"
#include "../src/core/host.hpp"
#include "../src/core/image_host.hpp"

namespace ampify::test {

struct FakeHost : Host, KeyUnbindHost {
    std::string process = "ui";  // elements run in the interface unless their manifest asks for the engine
    std::string locale = "en-US";
    std::map<std::string, std::string> settings;
    std::map<std::string, std::string> storage;  // element id -> JSON object text
    PlayerSnapshot player;
    std::vector<PlayerCommand> commands;
    std::vector<std::pair<std::string, double>> gains;  // (element, dB) as set
    std::vector<std::string> notifications;             // "title|text"
    std::map<std::string, std::string> keys;            // combo -> element id
    std::vector<std::string> unregistered;              // element ids
    std::vector<std::string> requestedUrls;
    HttpResponse httpResponse{200, "ok"};
    UiModel ui;

    std::string Process() const override { return process; }
    std::string Locale() const override { return locale; }

    std::optional<std::string> ReadSetting(const std::string& key) override {
        auto found = settings.find(key);
        if (found == settings.end()) return std::nullopt;
        return found->second;
    }
    void WriteSetting(const std::string& key, const std::string& value) override { settings[key] = value; }

    std::optional<std::string> ReadStorage(const std::string& elementId) override {
        auto found = storage.find(elementId);
        if (found == storage.end()) return std::nullopt;
        return found->second;
    }
    bool WriteStorage(const std::string& elementId, const std::string& json) override {
        storage[elementId] = json;
        return true;
    }

    PlayerSnapshot Player() override { return player; }
    bool Control(PlayerCommand command, int64_t seekMs) override {
        (void)seekMs;
        commands.push_back(command);
        return true;
    }

    void SetGainDb(const std::string& elementId, double db) override { gains.emplace_back(elementId, db); }
    UiModel& Ui() override { return ui; }

    void Notify(const std::string& elementId, const std::string& title, const std::string& text) override {
        (void)elementId;
        notifications.push_back(title + "|" + text);
    }

    bool RegisterKey(const std::string& elementId, const std::string& combo) override {
        if (keys.count(combo)) return false;
        keys[combo] = elementId;
        return true;
    }
    bool UnregisterKey(const std::string& elementId, const std::string& combo) override {
        auto found = keys.find(combo);
        if (found == keys.end() || found->second != elementId) return false;
        keys.erase(found);
        return true;
    }
    void UnregisterKeys(const std::string& elementId) override {
        unregistered.push_back(elementId);
        for (auto it = keys.begin(); it != keys.end();) {
            it = it->second == elementId ? keys.erase(it) : std::next(it);
        }
    }

    // Requests come from a worker thread. A test that holds the gate shut makes them wait, as a slow network would
    // (at most kHttpHoldLimit, so a test that forgets to release does not hang).
    static constexpr std::chrono::seconds kHttpHoldLimit{5};
    void HoldHttp() {
        std::lock_guard<std::mutex> lock(httpMutex);
        httpHeld = true;
    }
    void ReleaseHttp() {
        {
            std::lock_guard<std::mutex> lock(httpMutex);
            httpHeld = false;
        }
        httpChanged.notify_all();
    }
    // True once `count` requests have reached the host, or false after the hold limit.
    bool WaitForRequests(size_t count) {
        std::unique_lock<std::mutex> lock(httpMutex);
        return httpChanged.wait_for(lock, kHttpHoldLimit, [&] { return requestedUrls.size() >= count; });
    }
    size_t RequestCount() {
        std::lock_guard<std::mutex> lock(httpMutex);
        return requestedUrls.size();
    }

    HttpResponse HttpGet(const std::string& url) override {
        std::unique_lock<std::mutex> lock(httpMutex);
        requestedUrls.push_back(url);
        httpChanged.notify_all();
        httpChanged.wait_for(lock, kHttpHoldLimit, [this] { return !httpHeld; });
        return httpResponse;
    }

    std::mutex httpMutex;  // guards requestedUrls and httpHeld (and the posts of ServiceHost)
    std::condition_variable httpChanged;
    bool httpHeld = false;

    // Optional audio: a test that sets it (usually to an ampify::audio::AudioEngine it feeds) offers AudioHost.
    AudioHost* audio = nullptr;
    // Optional image decoding, for Ampify.color.fromArtwork.
    ImageHost* images = nullptr;
    // Optional cover of the song playing, for Ampify.color.playing.
    ArtworkHost* artwork = nullptr;
    // Optional runner of the kit's commands, for Ampify.kit.run (the Marketplace).
    KitCommandHost* kit = nullptr;

    void* QueryCapability(std::string_view name) override {
        if (audio && name == AudioHost::kCapabilityName) return audio;
        if (images && name == ImageHost::kCapabilityName) return images;
        if (artwork && name == ArtworkHost::kCapabilityName) return artwork;
        if (kit && name == KitCommandHost::kCapabilityName) return kit;
        if (void* found = Offer<KeyUnbindHost>(name, this)) return found;
        return Host::QueryCapability(name);
    }
};

}  // namespace ampify::test
