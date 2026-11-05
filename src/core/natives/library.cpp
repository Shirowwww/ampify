// Natives of the module library (src/core/prelude/library.cpp). Permission: library.read. The library file is read
// off the JavaScript thread (src/core/library/musicdb.*), and its answer is kept for every element of the process
// until the file's date or size changes, since Apple Music rewrites it after each play.

#include "library.hpp"

#include <cinttypes>
#include <cstdio>
#include <mutex>
#include <system_error>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "../library/musicdb.hpp"

namespace ampify::natives {

namespace {

struct LibraryCache {
    std::mutex mutex;
    std::filesystem::path file;
    std::filesystem::file_time_type modified;
    uintmax_t size = 0;
    std::string json;  // empty until a read succeeds
};

LibraryCache& Cache() {
    static LibraryCache cache;
    return cache;
}

// Apple writes persistent ids as 16 hexadecimal digits; a JavaScript number cannot hold 64 bits.
std::string PersistentIdText(uint64_t id) {
    char text[17];
    std::snprintf(text, sizeof text, "%016" PRIX64, id);
    return text;
}

std::string TracksJson(const std::vector<LibraryTrack>& tracks) {
    Json::Array list;
    list.reserve(tracks.size());
    for (const LibraryTrack& t : tracks) {
        list.emplace_back(Json::Object{{"persistentId", Json(PersistentIdText(t.persistentId))},
                                       {"title", Json(t.title)},
                                       {"artist", Json(t.artist)},
                                       {"album", Json(t.album)},
                                       {"albumArtist", Json(t.albumArtist)},
                                       {"genre", Json(t.genre)},
                                       {"composer", Json(t.composer)},
                                       {"year", Json(static_cast<double>(t.year))},
                                       {"playCount", Json(static_cast<double>(t.playCount))},
                                       {"skipCount", Json(static_cast<double>(t.skipCount))},
                                       {"lastPlayed", Json(static_cast<double>(t.lastPlayedMs))},
                                       {"dateAdded", Json(static_cast<double>(t.dateAddedMs))},
                                       {"durationMs", Json(static_cast<double>(t.durationMs))}});
    }
    return Json(std::move(list)).Dump();
}

// Worker thread: status 200 and the tracks as JSON, or 0 and why.
HttpResponse ReadTracks(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::file_time_type modified = std::filesystem::last_write_time(file, ec);
    uintmax_t size = ec ? 0 : std::filesystem::file_size(file, ec);
    if (ec) return {0, "no Apple Music library was found"};
    LibraryCache& cache = Cache();
    std::lock_guard<std::mutex> lock(cache.mutex);  // held while reading, so two elements do not read it twice
    if (!cache.json.empty() && cache.file == file && cache.modified == modified && cache.size == size) {
        return {200, cache.json};
    }
    MusicDbResult result = ReadMusicDb(file);
    if (!result.error.empty()) return {0, result.error};
    cache.file = file;
    cache.modified = modified;
    cache.size = size;
    cache.json = TracksJson(result.tracks);
    return {200, cache.json};
}

struct LibraryNatives : ScriptBridge {
    // native.libraryTracks(): starts reading the library and returns the request's id. The answer comes back as
    // dispatch.http.
    AMPIFY_NATIVE(Tracks) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "library.read", &failure)) return failure;
        LibraryHost* library = HostOf(ext).Capability<LibraryHost>();
        std::filesystem::path file = library ? library->LibraryFile() : std::filesystem::path();
        uint32_t id = SubmitHttp(ext, [file]() -> HttpResponse {
            if (file.empty()) return {0, "the library is not read by this host"};
            return ReadTracks(file);
        });
        if (id == 0) {
            return Throw(ctx, "RangeError",
                         "too many requests in flight (" + std::to_string(kMaxHttpInFlight) + " at most)");
        }
        return JS_NewUint32(ctx, id);
    }
};

}  // namespace

void DefineLibrary(api::NativeScope& scope) {
    scope.Function("libraryTracks", LibraryNatives::Tracks, 0);
}

}  // namespace ampify::natives
