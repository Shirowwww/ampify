#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace amk {

struct TrackInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string variantId;
    bool atmos = false;
    bool lossless = false;
};

struct PlayerSnapshot {
    bool playing = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    std::optional<TrackInfo> track;  // empty when nothing is loaded
};

enum class PlayerCommand { Play, Pause, Next, Previous, Seek };

struct HttpResponse {
    int status = 0;
    std::string text;
};

// What the kernel offers to the extensions. The Windows kernel implements it with COM, the Win32 API and the
// settings file. The tests use a fake. Every method runs on the thread that owns the extensions, except
// the Deliver* calls of the loader, which may come from any thread.
class Host {
public:
    virtual ~Host() = default;

    // "ui" in AppleMusic.exe, "engine" in AMPLibraryAgent.exe.
    virtual std::string Process() const = 0;
    // The app language, such as "fr-FR".
    virtual std::string Locale() const = 0;

    // Settings, as `key = value` in settings.txt. Extension keys look like `extensions.<id>.<key>`.
    virtual std::optional<std::string> ReadSetting(const std::string& key) = 0;
    virtual void WriteSetting(const std::string& key, const std::string& value) = 0;

    // One JSON object per element, in its own file. Returns nullopt when the file does not exist yet.
    virtual std::optional<std::string> ReadStorage(const std::string& elementId) = 0;
    virtual bool WriteStorage(const std::string& elementId, const std::string& json) = 0;

    virtual PlayerSnapshot Player() = 0;
    // False when the app cannot run the command now. Never changes the output volume.
    virtual bool Control(PlayerCommand command, int64_t seekMs) = 0;

    // Gain in dB applied to the decoded samples of the current track, for one element. The kernel adds the
    // gains of all elements and clamps the total to the ceiling. 0 removes the element's gain.
    virtual void SetGainDb(const std::string& elementId, double db) = 0;

    virtual void Notify(const std::string& elementId, const std::string& title, const std::string& text) = 0;

    // Registers a global shortcut in its normalized form, such as "Ctrl+Alt+Right". False when it is taken
    // or unsupported. The kernel calls the loader's DeliverKey when it is pressed.
    virtual bool RegisterKey(const std::string& elementId, const std::string& combo) = 0;
    virtual void UnregisterKeys(const std::string& elementId) = 0;

    // Only https hosts that the manifest declares reach this call. The kernel does not follow other hosts.
    virtual HttpResponse HttpGet(const std::string& url) = 0;
};

}  // namespace amk
