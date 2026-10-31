#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ui_model.hpp"

namespace ampify {

struct TrackInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string variantId;
    bool atmos = false;
    bool lossless = false;
    // The entry of the play queue that holds this track, as Apple numbers it. The same song played twice has two
    // entries. Empty when the source does not tell it (SMTC).
    std::string queueItemId;
    // The cover as an https address of a 1000 px image, and its dominant colour as "#RRGGBB". Empty when the source
    // does not give them (SMTC) or Apple has none.
    std::string artwork;
    std::string artworkColor;
};

struct PlayerSnapshot {
    bool playing = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    std::optional<TrackInfo> track;  // empty when nothing is loaded
    // Where the state was read: "apple" (AMP.Services.Player, inside Apple Music), "smtc" (the Windows media
    // controls), or empty when no source answered. An empty source means "unknown", not "stopped".
    std::string source;
    // Apple has lyrics for the current track and the kernel holds them. Not shown to elements: it drives the
    // "lyricschange" event (core/player_watch.*), and Ampify.player.lyrics() gives the lyrics themselves.
    bool lyricsAvailable = false;
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

    // The interface that the elements add to. Thread-safe: the host renders it, and the tests read it.
    virtual UiModel& Ui() = 0;

    // ---- Optional capabilities --------------------------------------------------------------------------------
    // The pure virtuals above are what every host has. A new service is a capability instead, so that adding one
    // does not touch every host. A module declares it in its own header:
    //
    //     struct AudioAnalysisHost {
    //         static constexpr std::string_view kCapabilityName = "audio.analysis";
    //         virtual ~AudioAnalysisHost() = default;
    //         virtual bool Subscribe(const std::string& elementId) = 0;
    //     };
    //
    // A host that supports it derives from that struct and answers QueryCapability for its name (see Offer). The
    // module's natives call host.Capability<AudioAnalysisHost>(), which is null on the hosts that do not have it.

    // The capability T of this host, or nullptr when it does not offer it.
    template <class T>
    T* Capability() {
        return static_cast<T*>(QueryCapability(T::kCapabilityName));
    }

    // The capability named `name`, as a T* converted to void*, or nullptr. An override ends with
    // `return Base::QueryCapability(name);`, so that the classes it derives from can still offer theirs.
    virtual void* QueryCapability(std::string_view name) {
        (void)name;
        return nullptr;
    }

protected:
    // For QueryCapability overrides: `if (void* found = Offer<AudioAnalysisHost>(name, this)) return found;`.
    // The cast goes through T*, so that Capability<T> gets the right address under multiple inheritance.
    template <class T, class Self>
    static void* Offer(std::string_view name, Self* self) {
        return name == T::kCapabilityName ? static_cast<void*>(static_cast<T*>(self)) : nullptr;
    }
};

}  // namespace ampify
