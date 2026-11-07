#include "lcd_links.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#include "win_host.hpp"
#include "winrt_abi.hpp"
#include "win_common.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif
#include "xaml_chrome.hpp"

#include "../core/amp_abi.hpp"
#include "../core/ui_watchdog.hpp"
#include "../core/journal.hpp"
#include "../core/now_playing.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <windows.h>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::Foundation::IInspectable;

constexpr wchar_t kDisplayClass[] = L"AMP.UI.Views.NowPlayingControl";
constexpr wchar_t kDisplayName[] = L"LCD";
constexpr wchar_t kTitleName[] = L"TrackTitleTickerTape";
constexpr wchar_t kArtistName[] = L"ArtistTitleTickerTape";
constexpr wchar_t kTextName[] = L"ScrollingText";
constexpr std::wstring_view kArtistAlbumSeparator = L" — ";  // Apple's "Artist — Album"
constexpr auto kSearchEvery = std::chrono::seconds(5);
constexpr auto kSearchEveryMax = std::chrono::seconds(60);  // the wait after misses doubles up to this
constexpr int kSearchLimit = 6000;  // elements walked in the main window to find the display
constexpr int kCatalogResults = 10;

struct Links {
    Journal* journal = nullptr;
    winrt::weak_ref<xaml::FrameworkElement> display;  // Apple's own objects: a weak reference resolves
    winrt::weak_ref<xaml::FrameworkElement> title;
    winrt::weak_ref<xaml::FrameworkElement> artist;
    std::chrono::steady_clock::time_point nextSearch{};
    std::chrono::steady_clock::duration searchWait = kSearchEvery;
    std::string trackKey;  // the song of the last search: a new one starts the searches again
    bool told = false;          // the log said once that the links are in place
    bool commandMissing = false;  // the log said once that Apple's "go to" command was not found
};

Links& State() {
    static Links* state = new Links();  // never destroyed: it holds XAML references, released with the process
    return *state;
}

xaml::FrameworkElement FindNamed(const xaml::DependencyObject& root, std::wstring_view name,
                                 std::wstring_view className = {}) {
    ampify::ActivityScope activity("player display tree search");
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(kSearchLimit); i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>(); element && element.Name() == name &&
                                                                         (className.empty() || winrt::get_class_name(element) == className)) {
            return element;
        }
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

controls::TextBlock TextOf(const xaml::FrameworkElement& ticker) {
    xaml::FrameworkElement text = ticker ? FindNamed(ticker, kTextName) : nullptr;
    return text ? text.try_as<controls::TextBlock>() : nullptr;
}

// Apple's "go to" command of the display's view model: the album of the song playing, at that song.
bool GoToAlbum(const xaml::FrameworkElement& display) {
    Links& state = State();
    rt::Ref unknown(winrt::detach_abi(display.as<winrt::Windows::Foundation::IUnknown>()));
    rt::Ref control, modelObject, model, actionObject;
    if (!rt::Query(unknown, rt::GuidOf(amp::kIidNowPlayingControl), control) ||
        FAILED(rt::CallSlot(control, amp::kNowPlayingControlViewModel, modelObject.Put())) || !modelObject ||
        !rt::Query(modelObject, rt::GuidOf(amp::kIidNowPlayingViewModel), model) ||
        FAILED(rt::CallSlot(model, amp::kNowPlayingViewModelGoToItemAction, actionObject.Put())) || !actionObject) {
        if (!state.commandMissing && state.journal) state.journal->Write("Display links: Apple's view model was not found");
        state.commandMissing = true;
        return false;
    }
    IInspectable action{nullptr};
    winrt::copy_from_abi(action, actionObject.Get());
    if (auto command = action.try_as<xaml::Input::ICommand>()) {
        if (!command.CanExecute(nullptr)) return false;
        command.Execute(nullptr);
        return true;
    }
    // Apple Music 1.1792 gives one of its own actions, which its UIBroker runs as its "···" menu does.
    rt::Ref broker, operation;
    rt::Activation activation = rt::GetActivationFactory(winrt::to_hstring(amp::kClassUiBroker).c_str(),
                                                         rt::GuidOf(amp::kIidUiBrokerStatics), broker);
    if (FAILED(activation.result) ||
        FAILED(rt::CallSlot(broker, amp::kUiBrokerGlobalDispatchAction, actionObject.Get(), operation.Put()))) {
        if (!state.commandMissing && state.journal) state.journal->Write("Display links: Apple's go-to action did not run");
        state.commandMissing = true;
        return false;
    }
    return true;
}


bool SameName(const winrt::hstring& a, std::wstring_view b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

// The artist page of the catalog search's answer: the artist named exactly so, else the first one of a shared credit
// ("A & B", "A, B"), else the first artist found.
std::wstring ArtistLink(const winrt::hstring& reply, std::wstring_view name) {
    auto results = winrt::Windows::Data::Json::JsonObject::Parse(reply).GetNamedArray(L"results", nullptr);
    if (!results || results.Size() == 0) return {};
    std::wstring_view first = name.substr(0, std::min(name.find(L" & "), name.find(L", ")));
    winrt::hstring fallback;
    for (const auto& value : results) {
        auto artist = value.GetObject();
        winrt::hstring link = artist.GetNamedString(L"artistLinkUrl", L"");
        winrt::hstring shown = artist.GetNamedString(L"artistName", L"");
        if (link.empty()) continue;
        if (SameName(shown, name)) return std::wstring(link);
        if (fallback.empty() || SameName(shown, first)) fallback = link;
    }
    return std::wstring(fallback);
}

// https://music.apple.com/fr/artist/name/123?uo=4 -> music://music.apple.com/fr/artist/name/123
std::string MusicLink(std::wstring link) {
    constexpr std::wstring_view kWeb = L"https://music.apple.com/";
    if (link.rfind(kWeb, 0) != 0) return {};
    link = L"music://music.apple.com/" + link.substr(kWeb.size());
    if (auto query = link.find(L'?'); query != std::wstring::npos) link.resize(query);
    return winrt::to_string(link);
}

winrt::fire_and_forget OpenArtist(std::wstring name) {
    Journal* journal = State().journal;
    try {
        std::wstring url = L"https://itunes.apple.com/search?entity=musicArtist&limit=" + std::to_wstring(kCatalogResults) +
                           L"&country=" + CatalogStorefront() + L"&term=" +
                           std::wstring(winrt::Windows::Foundation::Uri::EscapeComponent(name));
        winrt::Windows::Web::Http::HttpClient client;
        winrt::hstring reply = co_await client.GetStringAsync(winrt::Windows::Foundation::Uri(url));
        std::string link = MusicLink(ArtistLink(reply, name));
        bool opened = !link.empty() && OpenInAppleMusic(link);
        if (journal) journal->Write(opened ? "Display links: artist page opened" : "Display links: artist not found in the catalog");
    } catch (...) {
        if (journal) journal->Write("Display links: the catalog search failed");
    }
}

// Where the artist ends on the line "Artist — Album", in the text's own coordinates.
double ArtistEnd(const controls::TextBlock& text, std::size_t separator, std::size_t length) {
    try {
        auto position = text.ContentStart().GetPositionAtOffset(static_cast<int>(separator) + 1,
                                                                xaml::Documents::LogicalDirection::Forward);
        if (position) return position.GetCharacterRect(xaml::Documents::LogicalDirection::Forward).X;
    } catch (...) {
    }
    return length ? text.ActualWidth() * static_cast<double>(separator) / static_cast<double>(length) : 0;
}

void OnTitle() {
    if (auto display = State().display.get()) GoToAlbum(display);
}

void OnArtistLine(const xaml::Input::TappedRoutedEventArgs& args) {
    Links& state = State();
    auto display = state.display.get();
    controls::TextBlock text = TextOf(state.artist.get());
    if (!display || !text) return;
    std::wstring line(text.Text());
    std::size_t separator = line.find(kArtistAlbumSeparator);
    bool onArtist = separator == std::wstring::npos ||
                    args.GetPosition(text).X < ArtistEnd(text, separator, line.size());
    if (!onArtist) {
        GoToAlbum(display);
        return;
    }
    std::wstring artist = separator == std::wstring::npos ? line : line.substr(0, separator);
    if (!artist.empty()) OpenArtist(std::move(artist));
}

// The ticker underlines its text under the pointer, the sign that it is a link.
void Link(const xaml::FrameworkElement& ticker, bool title) {
    ticker.IsTapEnabled(true);
    ticker.Tapped([title](IInspectable const&, xaml::Input::TappedRoutedEventArgs const& args) noexcept {
        try {
            if (title) {
                OnTitle();
            } else {
                OnArtistLine(args);
            }
            args.Handled(true);
        } catch (...) {
        }
    });
    auto underline = [weak = winrt::make_weak(ticker)](bool on) {
        if (auto text = TextOf(weak.get())) {
            text.TextDecorations(on ? winrt::Windows::UI::Text::TextDecorations::Underline
                                    : winrt::Windows::UI::Text::TextDecorations::None);
        }
    };
    ticker.PointerEntered([underline](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
        try {
            underline(true);
        } catch (...) {
        }
    });
    ticker.PointerExited([underline](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
        try {
            underline(false);
        } catch (...) {
        }
    });
}

}  // namespace


void KeepDisplayLinks(Journal& journal) noexcept {
    try {
        Links& state = State();
        state.journal = &journal;
        if (auto display = state.display.get(); display && display.IsLoaded() && state.title.get() && state.artist.get()) {
            state.searchWait = kSearchEvery;
            return;
        }
        auto now = std::chrono::steady_clock::now();
        // Apple builds the tickers with a song, so a new one is worth a search at once. Each walk costs the interface
        // thread: a miss makes the next wait twice as long.
        if (std::string track = CurrentNowPlaying().trackKey; track != state.trackKey) {
            state.trackKey = std::move(track);
            state.searchWait = kSearchEvery;
            state.nextSearch = {};
        }
        if (now < state.nextSearch) return;
        state.nextSearch = now + state.searchWait;
        state.searchWait = std::min<std::chrono::steady_clock::duration>(state.searchWait * 2, kSearchEveryMax);
        xaml::Window window = AppWindowOf(WindowRole::Main);
        auto content = window ? window.Content() : nullptr;
        xaml::FrameworkElement display = content ? FindNamed(content, kDisplayName, kDisplayClass) : nullptr;
        xaml::FrameworkElement title = display ? FindNamed(display, kTitleName) : nullptr;
        xaml::FrameworkElement artist = display ? FindNamed(display, kArtistName) : nullptr;
        if (!title || !artist) return;  // nothing loaded yet: Apple builds the tickers with the first song
        if (title != state.title.get()) Link(title, true);  // a display loaded again keeps its tickers and handlers
        if (artist != state.artist.get()) Link(artist, false);
        state.display = winrt::make_weak(display);
        state.title = winrt::make_weak(title);
        state.artist = winrt::make_weak(artist);
        state.searchWait = kSearchEvery;
        if (!state.told) journal.Write("Display links: the title, artist and album of the display open their pages");
        state.told = true;
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
