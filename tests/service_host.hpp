#pragma once

// A FakeHost that also offers the optional kit services (POST, Discord, icon menu, MIDI, remote control, output
// files) and records every call, plus a fixture that runs an element against it. Shared by the module tests of
// those services.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../core/manifest.hpp"
#include "../core/natives/discord.hpp"
#include "../core/natives/fs.hpp"
#include "../core/natives/http.hpp"
#include "../core/natives/midi.hpp"
#include "../core/natives/remote.hpp"
#include "../core/natives/tray.hpp"
#include "../core/output_files.hpp"
#include "../core/script_extension.hpp"
#include "fake_host.hpp"

namespace amk::test {

struct ServiceHost : FakeHost, HttpPostHost, DiscordHost, TrayHost, MidiHost, RemoteHost, OutputFilesHost {
    struct Post {
        std::string url;
        std::string body;
        HttpHeaders headers;
    };
    struct ShownActivity {
        std::string elementId;
        std::string clientId;
        discord::Activity activity;
    };

    std::vector<Post> posts;
    HttpResponse postResponse{201, "envoyé"};
    std::vector<ShownActivity> activities;
    std::vector<std::string> clearedActivities;
    std::map<std::string, std::vector<TrayItem>> trayMenus;
    std::vector<std::string> trayRemoved;
    std::vector<std::string> midiSubscribers;
    std::vector<std::string> midiUnsubscribed;
    std::map<std::string, std::string> remoteStates;
    std::vector<std::string> remoteRemoved;
    std::filesystem::path outputRoot;

    HttpResponse HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) override {
        posts.push_back({url, body, headers});
        return postResponse;
    }
    bool SetActivity(const std::string& elementId, const std::string& clientId,
                     const discord::Activity& activity) override {
        activities.push_back({elementId, clientId, activity});
        return true;
    }
    void ClearActivity(const std::string& elementId) override { clearedActivities.push_back(elementId); }
    bool SetTrayMenu(const std::string& elementId, const std::vector<TrayItem>& items) override {
        trayMenus[elementId] = items;
        return true;
    }
    void RemoveTrayMenu(const std::string& elementId) override {
        trayRemoved.push_back(elementId);
        trayMenus.erase(elementId);
    }
    bool SubscribeMidi(const std::string& elementId) override {
        midiSubscribers.push_back(elementId);
        return true;
    }
    void UnsubscribeMidi(const std::string& elementId) override { midiUnsubscribed.push_back(elementId); }
    std::vector<std::string> MidiDevices() override { return {"Clavier de test"}; }
    bool PublishRemoteState(const std::string& elementId, const std::string& json) override {
        remoteStates[elementId] = json;
        return true;
    }
    void RemoveRemoteState(const std::string& elementId) override {
        remoteRemoved.push_back(elementId);
        remoteStates.erase(elementId);
    }
    bool WriteOutputText(const std::string& elementId, const std::string& name, const std::string& text,
                         std::string* error) override {
        return WriteOutputFile(outputRoot, elementId, name, text, error);
    }

    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<HttpPostHost>(name, this)) return found;
        if (void* found = Offer<DiscordHost>(name, this)) return found;
        if (void* found = Offer<TrayHost>(name, this)) return found;
        if (void* found = Offer<MidiHost>(name, this)) return found;
        if (void* found = Offer<RemoteHost>(name, this)) return found;
        if (void* found = Offer<OutputFilesHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

// Runs elements against a host of type H (ServiceHost, or a plain FakeHost to check the hosts without a service).
template <class H = ServiceHost>
struct ServiceFixture {
    H host;
    std::ostringstream out;
    Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ScriptExtension> Make(const std::string& id, const std::string& main,
                                          const std::string& permissions = "") {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "amk-tests" / ("service-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ManifestResult result = ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        if (!result.manifest) {
            std::string reasons;
            for (const std::string& error : result.errors) reasons += error + "; ";
            throw std::runtime_error("manifeste de test refusé : " + reasons);
        }
        return std::make_unique<ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace amk::test
