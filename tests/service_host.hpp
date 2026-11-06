#pragma once

// A FakeHost that also offers the optional kit services (POST, Discord, icon menu, MIDI, remote control, output
// files, library file) and records every call, plus a fixture that runs an element against it. Shared by the module tests of
// those services.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/image_host.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/natives/discord.hpp"
#include "../src/core/natives/fs.hpp"
#include "../src/core/natives/http.hpp"
#include "../src/core/natives/library.hpp"
#include "../src/core/natives/midi.hpp"
#include "../src/core/natives/remote.hpp"
#include "../src/core/natives/tray.hpp"
#include "../src/core/output_files.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"

namespace ampify::test {

struct ServiceHost : FakeHost, HttpPostHost, BrowserHost, DiscordHost, TrayHost, MidiHost, RemoteHost, OutputFilesHost,
                     ArtworkHost, LibraryHost, GifHost {
    struct GifCall {
        std::string video;
        GifHost::Options options;
    };
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
    std::vector<Post> headedGets;  // GETs that carried headers, with an empty body
    std::vector<Post> sent;        // PUT, PATCH and DELETE: url is "<method> <url>"
    std::vector<std::string> openedPages;
    HttpResponse postResponse{201, "sent"};
    std::vector<ShownActivity> activities;
    std::vector<std::string> clearedActivities;
    std::map<std::string, std::vector<TrayItem>> trayMenus;
    std::vector<std::string> trayRemoved;
    std::vector<std::string> midiSubscribers;
    std::vector<std::string> midiUnsubscribed;
    std::map<std::string, std::string> remoteStates;
    std::vector<std::string> remoteRemoved;
    std::filesystem::path outputRoot;
    std::filesystem::path libraryFile;
    std::vector<GifCall> gifCalls;
    std::optional<ArtworkHost::File> artwork;  // read from a worker thread, set by the test between reads

    std::optional<ArtworkHost::File> CurrentArtwork() override {
        std::lock_guard<std::mutex> lock(httpMutex);
        return artwork;
    }

    HttpResponse HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) override {
        std::lock_guard<std::mutex> lock(httpMutex);  // called from a worker thread
        posts.push_back({url, body, headers});
        return postResponse;
    }
    HttpResponse HttpSend(const std::string& method, const std::string& url, const std::string& body,
                          const HttpHeaders& headers) override {
        std::lock_guard<std::mutex> lock(httpMutex);  // called from a worker thread
        sent.push_back({method + " " + url, body, headers});
        return postResponse;
    }
    HttpResponse HttpGetWithHeaders(const std::string& url, const HttpHeaders& headers) override {
        std::lock_guard<std::mutex> lock(httpMutex);  // called from a worker thread
        headedGets.push_back({url, "", headers});
        return postResponse;
    }
    bool OpenInBrowser(const std::string& url) override {
        openedPages.push_back(url);
        return true;
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
    std::vector<std::string> MidiDevices() override { return {"Test keyboard"}; }
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
    std::filesystem::path LibraryFile() override { return libraryFile; }
    // An empty video stands for one the host cannot read.
    std::optional<std::string> GifFromVideo(const std::string& video, const GifHost::Options& options,
                                            std::string* error) override {
        std::lock_guard<std::mutex> lock(httpMutex);  // called from a worker thread
        gifCalls.push_back({video, options});
        if (video.empty()) {
            *error = "no video stream";
            return std::nullopt;
        }
        return std::string("GIF89a");
    }

    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<HttpPostHost>(name, this)) return found;
        if (void* found = Offer<BrowserHost>(name, this)) return found;
        if (void* found = Offer<DiscordHost>(name, this)) return found;
        if (void* found = Offer<TrayHost>(name, this)) return found;
        if (void* found = Offer<MidiHost>(name, this)) return found;
        if (void* found = Offer<RemoteHost>(name, this)) return found;
        if (void* found = Offer<OutputFilesHost>(name, this)) return found;
        if (void* found = Offer<ArtworkHost>(name, this)) return found;
        if (void* found = Offer<LibraryHost>(name, this)) return found;
        if (void* found = Offer<GifHost>(name, this)) return found;
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
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("service-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ManifestResult result = ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        if (!result.manifest) {
            std::string reasons;
            for (const std::string& error : result.errors) reasons += error + "; ";
            throw std::runtime_error("test manifest refused: " + reasons);
        }
        return std::make_unique<ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace ampify::test
