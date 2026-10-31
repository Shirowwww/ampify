#pragma once

// A Host for the tests: everything in memory, and every call is recorded so that a test can check it.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../core/audio/audio_host.hpp"
#include "../core/host.hpp"

namespace ampify::test {

struct FakeHost : Host {
    std::string process = "ui";  // elements run in the interface unless their manifest asks for the engine
    std::string locale = "fr-FR";
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
    void UnregisterKeys(const std::string& elementId) override {
        unregistered.push_back(elementId);
        for (auto it = keys.begin(); it != keys.end();) {
            it = it->second == elementId ? keys.erase(it) : std::next(it);
        }
    }

    HttpResponse HttpGet(const std::string& url) override {
        requestedUrls.push_back(url);
        return httpResponse;
    }

    // Optional audio: a test that sets it (usually to an ampify::audio::AudioEngine it feeds) offers AudioHost.
    AudioHost* audio = nullptr;

    void* QueryCapability(std::string_view name) override {
        if (audio && name == AudioHost::kCapabilityName) return audio;
        return Host::QueryCapability(name);
    }
};

}  // namespace ampify::test
