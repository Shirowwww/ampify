// A stand-in for AVFoundationCF.dll and CoreFoundation.dll, for the Wine harness only. The harness copies this
// file under other names (CoreFoundation.dll, CoreFP.dll) to test the module guard.
//
// It describes one item: a player whose current item has one track, a Dolby Atmos format and a stable variant
// id "abc123". The harness can withhold the variant, as Apple Music does while an item loads. Objects are tokens that describe themselves (a magic value and a kind), so any copy of this
// file, loaded under any name, recognises the tokens another copy handed out, as real CoreFoundation does.

#include <windows.h>

#include <cstring>

namespace {

constexpr unsigned kMagic = 0x414d4b54;  // "AMKT"

enum Kind : int { kItem = 1, kTracks, kTrack, kAsset, kFormats, kFormat, kVariant };

struct Token {
    unsigned magic;
    int kind;
    const char* text;  // the description, for the kinds that have one
};

const char kFormatText[] = "mediaType:'soun' mediaSubType:'qec3' channels:16";
const char kVariantText[] = "<AVCFVariantID: contents = \"abc123\", kind = 2>";

Token g_item{kMagic, kItem, nullptr};
Token g_tracks{kMagic, kTracks, nullptr};
Token g_track{kMagic, kTrack, nullptr};
Token g_asset{kMagic, kAsset, nullptr};
Token g_formats{kMagic, kFormats, nullptr};
Token g_format{kMagic, kFormat, kFormatText};
Token g_variant{kMagic, kVariant, kVariantText};

int g_volumeCalls = 0;
int g_trackReads = 0;
bool g_variantKnown = true;

bool IsKind(const void* object, int kind) {
    const Token* token = static_cast<const Token*>(object);
    return token && token->magic == kMagic && token->kind == kind;
}

}  // namespace

extern "C" {

__declspec(dllexport) void __cdecl AVCFPlayerSetVolume(void* player, float volume) {
    (void)player;
    (void)volume;
    g_volumeCalls++;
}

__declspec(dllexport) int __cdecl amk_fake_volume_calls() {
    return g_volumeCalls;
}

__declspec(dllexport) int __cdecl amk_fake_protected_method(void* self, int value) {
    (void)self;
    return value + 1;
}

// While `known` is 0, the item has no stable variant yet.
__declspec(dllexport) void __cdecl amk_fake_set_variant_known(int known) {
    g_variantKnown = known != 0;
}

// How many times the tracks of the item were asked for.
__declspec(dllexport) int __cdecl amk_fake_track_reads() {
    return g_trackReads;
}

__declspec(dllexport) void* __cdecl AVCFPlayerCopyCurrentItem(void* player) {
    return player ? &g_item : nullptr;
}

__declspec(dllexport) void* __cdecl AVCFPlayerItemCopyTracks(const void* item) {
    g_trackReads++;
    return IsKind(item, kItem) ? &g_tracks : nullptr;
}

__declspec(dllexport) void* __cdecl AVCFPlayerItemTrackCopyAssetTrack(const void* track) {
    return IsKind(track, kTrack) ? &g_asset : nullptr;
}

__declspec(dllexport) void* __cdecl AVCFAssetTrackCopyFormatDescriptions(const void* asset) {
    return IsKind(asset, kAsset) ? &g_formats : nullptr;
}

__declspec(dllexport) void* __cdecl AVCFPlayerItemCopyCurrentStableVariantID(const void* item) {
    return IsKind(item, kItem) && g_variantKnown ? &g_variant : nullptr;
}

__declspec(dllexport) void* __cdecl CFRetain(const void* object) {
    return const_cast<void*>(object);
}

// A description is the text itself, as a C string.
__declspec(dllexport) void* __cdecl CFCopyDescription(const void* object) {
    if (IsKind(object, kFormat) || IsKind(object, kVariant)) {
        return const_cast<char*>(static_cast<const Token*>(object)->text);
    }
    return nullptr;
}

__declspec(dllexport) void __cdecl CFRelease(const void* object) {
    (void)object;
}

__declspec(dllexport) long long __cdecl CFArrayGetCount(const void* array) {
    return (IsKind(array, kTracks) || IsKind(array, kFormats)) ? 1 : 0;
}

__declspec(dllexport) const void* __cdecl CFArrayGetValueAtIndex(const void* array, long long index) {
    if (index != 0) return nullptr;
    if (IsKind(array, kTracks)) return &g_track;
    if (IsKind(array, kFormats)) return &g_format;
    return nullptr;
}

__declspec(dllexport) long long __cdecl CFStringGetLength(const void* string) {
    return static_cast<long long>(std::strlen(static_cast<const char*>(string)));
}

// The range is two 64-bit values: passed by reference on Windows x64, as the caller's own CFRange is.
struct FakeRange {
    long long location;
    long long length;
};

__declspec(dllexport) long long __cdecl CFStringGetBytes(const void* string, FakeRange range, unsigned encoding,
                                                          unsigned char lossByte, unsigned char externalRepresentation,
                                                          unsigned char* buffer, long long bufferSize,
                                                          long long* usedBufferLength) {
    (void)range;
    (void)encoding;
    (void)lossByte;
    (void)externalRepresentation;
    const char* text = static_cast<const char*>(string);
    long long used = static_cast<long long>(std::strlen(text));
    if (used > bufferSize) used = bufferSize;
    std::memcpy(buffer, text, static_cast<size_t>(used));
    if (usedBufferLength) *usedBufferLength = used;
    return used;
}

}  // extern "C"
