#include "larger_cover.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Web.Http.h>

#include <chrono>
#include <cmath>
#include <map>
#include <utility>

#include "../core/player_model.hpp"
#include "player_controls.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

namespace imaging = winrt::Microsoft::UI::Xaml::Media::Imaging;

constexpr auto kLookupTimeout = std::chrono::seconds(10);

// Catalog id -> the lookup's artworkUrl100, "" when the catalog has no cover for it. Interface thread only; never
// destroyed (a static destructor would run after XAML shut down).
std::map<std::string, std::string>& Covers() {
    static auto* covers = new std::map<std::string, std::string>();
    return *covers;
}

std::string AddressOf(const imaging::BitmapImage& bitmap) {
    auto uri = bitmap ? bitmap.UriSource() : nullptr;
    return uri ? winrt::to_string(uri.AbsoluteUri()) : std::string();
}

void FromCatalog(const std::string& thumb, double pixels, const CoverFound& found) {
    std::string address = CatalogCoverAddress(thumb, pixels);
    found(address, address.empty() ? "the catalog has no cover for the song" : "");
}

// Asks the catalog for the song's cover, then answers on the interface thread if the same song still plays.
winrt::fire_and_forget Lookup(std::string id, double pixels, CoverFound found) {
    auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    std::string thumb;
    bool answered = false;  // only an answer is kept: offline now, the next song asks again
    try {
        std::string address = CatalogLookupAddress(id, winrt::to_string(CatalogStorefront()));
        if (!address.empty()) {
            winrt::Windows::Web::Http::HttpClient client;
            auto request = client.GetStringAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(address)));
            auto timeout = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
                [request](auto&&) {
                    try {
                        request.Cancel();
                    } catch (...) {
                    }
                },
                kLookupTimeout);
            winrt::hstring reply = co_await request;
            timeout.Cancel();
            auto results = winrt::Windows::Data::Json::JsonObject::Parse(reply).GetNamedArray(L"results", nullptr);
            if (results && results.Size() > 0) {
                thumb = winrt::to_string(results.GetObjectAt(0).GetNamedString(L"artworkUrl100", L""));
            }
            answered = true;
        }
    } catch (...) {
        thumb.clear();  // offline, too slow, or an answer of another shape: Apple's cover stays
    }
    if (!dispatcher) co_return;
    dispatcher.TryEnqueue([id, thumb, answered, pixels, found = std::move(found)]() noexcept {
        try {
            if (answered) Covers()[id] = thumb;
            // The song may have moved on while Apple's image had not yet: never the last song's cover on this one.
            if (PlayingCatalogId() != id) return;
            if (!answered) return found("", "the catalog did not answer for song " + id);
            FromCatalog(thumb, pixels, found);
        } catch (...) {
        }
    });
}

}  // namespace

void FindLargerCover(const imaging::BitmapImage& apple, double pixels, CoverFound found) {
    std::string own = AddressOf(apple);
    if (!own.empty() && apple.PixelWidth() >= pixels * kSoftCoverRatio) return found(own, "");  // only decoded small
    if (own.starts_with("https://")) {
        std::string larger = CatalogCoverAddress(own, pixels);
        if (!larger.empty()) return found(larger, "");
    }
    // The library's cache (often 600 px), an image given as data, another kind of address: the catalog's cover.
    std::string id = PlayingCatalogId();
    if (id.empty()) {
        // Not in the catalog by its id: Apple's own loader may still have the catalog's address of the cover.
        std::string loader = AddressOf(PlayingArtworkSource(pixels).try_as<imaging::BitmapImage>());
        std::string larger = CatalogCoverAddress(loader, pixels);
        if (!larger.empty()) return found(larger, "");
        return found("", "the song has no catalog id (" + std::to_string(apple.PixelWidth()) + " px asked at " +
                             std::to_string(std::lround(pixels)) + " px)");
    }
    if (auto known = Covers().find(id); known != Covers().end()) return FromCatalog(known->second, pixels, found);
    Lookup(id, pixels, std::move(found));
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
