// Windows only: the Host's files (settings.txt and storage\<id>.json), and the GIFs it makes of a video.
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "../src/core/settings.hpp"
#include "../src/win/video_gif.hpp"
#include "../src/win/win_host.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

// The number of `part` in `text`.
size_t CountOf(const std::string& text, const std::string& part) {
    size_t count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) count++;
    return count;
}

// The first frame of a GIF as top-down BGRA rows, read back by WIC; empty when it cannot be read.
std::vector<BYTE> FirstFrame(const std::string& gif, UINT* width, UINT* height) {
    using Microsoft::WRL::ComPtr;
    std::vector<BYTE> pixels;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapSource> bgra;
    LARGE_INTEGER start{};
    ULONG written = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(stream->Write(gif.data(), static_cast<ULONG>(gif.size()), &written)) ||
        FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &bgra)) ||
        FAILED(bgra->GetSize(width, height))) {
        return pixels;
    }
    pixels.resize(static_cast<size_t>(*width) * *height * 4);
    if (FAILED(bgra->CopyPixels(nullptr, *width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) pixels.clear();
    return pixels;
}

}  // namespace

// A fragmented MP4 as Apple Music's motion artworks are (H.264, 25 frames a second; 2 s, 96 pixels, made by ffmpeg)
// becomes a GIF that loops, at the asked size and pace.
TEST_CASE(WinGif_TurnsAFragmentedMp4IntoALoopingGif) {
    std::ifstream file(fs::path(AMPIFY_SOURCE_DIR) / "tests" / "data" / "motion-loop.mp4", std::ios::binary);
    std::string video((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(!video.empty());
    ampify::GifHost::Options options;
    options.side = 48;
    options.fps = 10;
    std::string error;
    std::optional<std::string> gif = ampify::win::GifFromVideoWithMf(video, options, &error);
    CHECK(gif.has_value());
    if (!gif) std::printf("    GIF refused: %s\n", error.c_str());
    if (gif && gif->size() > 13) {
        CHECK(gif->compare(0, 6, "GIF89a") == 0);
        auto word = [&](size_t at) { return static_cast<unsigned char>((*gif)[at]) | static_cast<unsigned char>((*gif)[at + 1]) << 8; };
        CHECK(word(6) == 48);
        CHECK(word(8) == 48);
        CHECK(gif->find("NETSCAPE2.0") != std::string::npos);
        // A graphic control block per frame (21 F9 04, flags, delay): 2 s at 10 a second, each shown 10 hundredths.
        const std::string control("\x21\xF9\x04", 3);
        CHECK(CountOf(*gif, control) == 20);
        size_t first = gif->find(control);
        if (first != std::string::npos && first + 6 < gif->size()) CHECK(word(first + 4) == 10);

        // The picture itself, the right way up: ffmpeg's test pattern is dark at the top, with red, blue and cyan
        // bars from left to right at the bottom. A wrong row pitch or a bottom-up copy would move them.
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        UINT width = 0;
        UINT height = 0;
        std::vector<BYTE> pixels = FirstFrame(*gif, &width, &height);
        CHECK(width == 48 && height == 48 && pixels.size() == 48 * 48 * 4);
        if (pixels.size() == 48 * 48 * 4) {
            auto at = [&](int x, int y) { return &pixels[(static_cast<size_t>(y) * 48 + x) * 4]; };  // B, G, R
            const BYTE* dark = at(4, 4);
            const BYTE* red = at(4, 28);
            const BYTE* blue = at(28, 28);
            const BYTE* cyan = at(44, 44);
            CHECK(dark[0] + dark[1] + dark[2] < 300);
            CHECK(red[2] > 180 && red[1] < 90 && red[0] < 90);
            CHECK(blue[0] > 180 && blue[1] < 90 && blue[2] < 90);
            CHECK(cyan[0] > 180 && cyan[1] > 180 && cyan[2] < 90);
        }
        if (com) CoUninitialize();
    }

    // The first second only.
    options.maxMs = 1000;
    std::optional<std::string> shorter = ampify::win::GifFromVideoWithMf(video, options, &error);
    CHECK(shorter.has_value() && CountOf(*shorter, std::string("\x21\xF9\x04", 3)) == 10);

    // Not a video: no GIF, and the reason.
    std::optional<std::string> none = ampify::win::GifFromVideoWithMf("not a video", options, &error);
    CHECK(!none.has_value());
    CHECK(!error.empty());
}

TEST_CASE(WinHost_StorageRoundTripsThroughTheKitFolder) {
    ampify::test::ScratchDir kit("storage");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);

    CHECK(!host.ReadStorage("demo").has_value());
    CHECK(host.WriteStorage("demo", R"({"launches":2})"));
    CHECK(host.ReadStorage("demo").value_or("") == R"({"launches":2})");
    CHECK(fs::exists(kit / "storage" / "demo.json"));
    CHECK(!fs::exists(kit / "storage" / "demo.json.tmp"));
}

TEST_CASE(WinHost_SettingsArePersistedToSettingsTxt) {
    ampify::test::ScratchDir kit("settings");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    host.WriteSetting("extensions.demo.volume", "7");
    CHECK(host.ReadSetting("extensions.demo.volume").value_or("") == "7");

    ampify::Settings reloaded;
    CHECK(reloaded.Load((kit / "settings.txt").string()));
    CHECK(reloaded.GetInt("extensions.demo.volume", 0) == 7);
    CHECK(host.Process() == "ui");
    CHECK(!host.Locale().empty());
}

TEST_CASE(WinHost_SettingWriteKeepsAChangeMadeSinceTheKernelRead) {
    ampify::test::ScratchDir kit("settings-merge");
    ampify::Settings settings;  // the kernel's copy, read before Ampify's settings page writes
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);
    std::string error;
    CHECK(ampify::Settings::UpdateFile((kit / "settings.txt").string(), "extensions.demo.side", "Right", error));

    host.WriteSetting("extensions.demo.volume", "7");

    ampify::Settings reloaded;
    CHECK(reloaded.Load((kit / "settings.txt").string()));
    CHECK(reloaded.GetString("extensions.demo.side", "") == "Right");
    CHECK(reloaded.GetInt("extensions.demo.volume", 0) == 7);
}

TEST_CASE(WinHost_ShortcutPressIsDeliveredToTheRegisteringThread) {
    ampify::test::ScratchDir kit("hotkeys");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    CHECK(host.RegisterKey("hk", "Ctrl+Alt+F12"));
    CHECK(!host.RegisterKey("hk", "Ctrl+Alt+Nope"));  // unknown key name

    // The system posts WM_HOTKEY with the registration id (the first one, in a fresh host) to the thread.
    PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 1, 0);
    std::string delivered;
    host.Service([&](const std::string& elementId, const std::string& combo) { delivered = elementId + " " + combo; });
    CHECK(delivered == "hk Ctrl+Alt+F12");

    host.UnregisterKeys("hk");
    delivered.clear();
    PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 1, 0);
    host.Service([&](const std::string& elementId, const std::string& combo) { delivered = elementId + " " + combo; });
    CHECK(delivered.empty());  // the registration is gone, so the id is ignored
}

TEST_CASE(WinHost_HttpFailureComesBackAsAnErrorValue) {
    ampify::test::ScratchDir kit("http");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);

    ampify::HttpResponse response = host.HttpGet("https://nonexistent.invalid/x");
    CHECK(response.status == 0);
    CHECK(!response.text.empty());
}

TEST_CASE(WinHost_NotifyWritesTheJournalLine) {
    ampify::test::ScratchDir kit("notify");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    host.Notify("hk", "Title", "Text");
    CHECK(log.str().find("[hk] notification: Title - Text") != std::string::npos);
}

TEST_CASE(WinHost_DecodesAnImageScaledToFit) {
    ampify::test::ScratchDir kit("image");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);
    ampify::ImageHost* images = host.Capability<ampify::ImageHost>();
    CHECK(images != nullptr);
    if (!images) return;

    // A 4 x 2 PNG: two red columns, then two blue ones.
    const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x7F, 0xA8, 0x7D, 0x63, 0x00, 0x00, 0x00,
        0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x84, 0xA1, 0xD4, 0x7F, 0x06,
        0x74, 0x01, 0x00, 0x07, 0x29, 0x0F, 0xF1, 0x27, 0xDA, 0x74, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82};
    const std::string bytes(reinterpret_cast<const char*>(png), sizeof png);

    std::optional<ampify::RgbaImage> whole = images->DecodeImage(bytes, 128);
    CHECK(whole && whole->width == 4 && whole->height == 2 && whole->pixels.size() == 32);
    if (whole) {
        CHECK(whole->pixels[0] == 255 && whole->pixels[1] == 0 && whole->pixels[2] == 0 && whole->pixels[3] == 255);
        CHECK(whole->pixels[12] == 0 && whole->pixels[13] == 0 && whole->pixels[14] == 255);
    }
    std::optional<ampify::RgbaImage> halved = images->DecodeImage(bytes, 2);
    CHECK(halved && halved->width == 2 && halved->height == 1);
    if (halved) {
        CHECK(halved->pixels[0] > 200 && halved->pixels[2] < 50);  // the red half
        CHECK(halved->pixels[4] < 50 && halved->pixels[6] > 200);  // the blue half
    }
    CHECK(!images->DecodeImage("not an image", 128).has_value());
    CHECK(!images->DecodeImage("", 128).has_value());
}
