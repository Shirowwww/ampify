// Built with AMPIFY_WITH_XAML, like the interface service that calls it.

#if defined(AMPIFY_WITH_XAML)

#include "video_frames.hpp"
#include "xaml_ending.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.Imaging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace ampify::win {

namespace {

namespace d3d = winrt::Windows::Graphics::DirectX::Direct3D11;
namespace imaging = winrt::Windows::Graphics::Imaging;
namespace playback = winrt::Windows::Media::Playback;
namespace xamlimaging = winrt::Microsoft::UI::Xaml::Media::Imaging;

constexpr int kMinSide = 32;  // below this the picture is a blur anyway
// A frame on its way longer than this was lost (the image left the tree, the window's queue closed): the next one goes.
constexpr std::int64_t kLostFrameMs = 1000;

std::int64_t NowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// One Direct3D device for every video, made at the first frame. Never destroyed: a static destructor would run after
// Windows shut COM down. Multithread-protected: the frames are copied on Media Foundation's threads.
winrt::com_ptr<ID3D11Device> Device() {
    static std::mutex lock;
    static auto* device = new winrt::com_ptr<ID3D11Device>();
    static bool tried = false;  // made once: a machine without one is not asked again at every frame
    std::lock_guard<std::mutex> guard(lock);
    if (*device || tried) return *device;
    tried = true;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        winrt::com_ptr<ID3D11Device> made;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                        D3D11_SDK_VERSION, made.put(), nullptr, nullptr))) {
            if (auto guarded = made.try_as<ID3D10Multithread>()) guarded->SetMultithreadProtected(TRUE);
            *device = made;
            break;
        }
    }
    return *device;
}

d3d::IDirect3DSurface MakeSurface(int width, int height) {
    winrt::com_ptr<ID3D11Device> device = Device();
    if (!device) return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, texture.put()));
    winrt::com_ptr<::IInspectable> surface;
    winrt::check_hresult(CreateDirect3D11SurfaceFromDXGISurface(texture.as<IDXGISurface>().get(), surface.put()));
    return surface.as<d3d::IDirect3DSurface>();
}

}  // namespace

struct VideoFrames::Shared {
    std::mutex lock;  // the surface and its size, made on Media Foundation's threads
    d3d::IDirect3DSurface surface{nullptr};
    int width = 0;
    int height = 0;
    std::atomic<int> side{0};
    std::atomic<bool> busy{false};     // a frame is on its way to the image: the next ones are skipped
    std::atomic<std::int64_t> busySince{0};
    std::atomic<bool> stopped{false};
    std::atomic<bool> shown{false};  // the first frame reached the image
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    winrt::weak_ref<xamlimaging::SoftwareBitmapSource> source;
    std::function<void()> firstFrame;
};

namespace {

using Shared = VideoFrames::Shared;

// On the window's thread: the frame goes to the image; the next one may come once the image took it. The image shows
// only once the first frame is in it, so it never flashes empty.
void Show(const std::shared_ptr<Shared>& shared, const imaging::SoftwareBitmap& bitmap) {
    auto source = shared->source.get();
    if (shared->stopped || !source) {
        shared->busy = false;
        return;
    }
    auto action = source.SetBitmapAsync(bitmap);
    action.Completed([shared](auto&&, winrt::Windows::Foundation::AsyncStatus status) noexcept {
        shared->busy = false;
        if (status != winrt::Windows::Foundation::AsyncStatus::Completed || shared->shown.exchange(true)) return;
        TryEnqueueUi(shared->queue, [shared]() noexcept {
            try {
                if (!shared->stopped && shared->firstFrame) shared->firstFrame();
            } catch (...) {
                // Work queued on Apple Music's thread: nothing may escape it.
            }
        });
    });
}

winrt::fire_and_forget Convert(std::shared_ptr<Shared> shared, d3d::IDirect3DSurface surface) {
    try {
        imaging::SoftwareBitmap bitmap =
            co_await imaging::SoftwareBitmap::CreateCopyFromSurfaceAsync(surface, imaging::BitmapAlphaMode::Premultiplied);
        if (!TryEnqueueUi(shared->queue, [shared, bitmap]() noexcept {
                try {
                    Show(shared, bitmap);
                } catch (...) {
                    shared->busy = false;  // Work queued on Apple Music's thread: nothing may escape it.
                }
            })) {
            shared->busy = false;
        }
    } catch (...) {
        shared->busy = false;
    }
}

// On Media Foundation's thread: the frame, at the size it is shown, into the surface, then on to the image.
void OnFrame(const std::shared_ptr<Shared>& shared, const playback::MediaPlayer& player) noexcept {
    if (shared->stopped) return;
    const std::int64_t now = NowMs();
    if (shared->busy.exchange(true) && now - shared->busySince.load() < kLostFrameMs) return;
    shared->busySince = now;
    try {
        auto session = player.PlaybackSession();
        const int naturalWidth = static_cast<int>(session.NaturalVideoWidth());
        const int naturalHeight = static_cast<int>(session.NaturalVideoHeight());
        const int longest = std::max(naturalWidth, naturalHeight);
        if (longest <= 0) {
            shared->busy = false;
            return;
        }
        const int side = std::clamp(shared->side.load(), std::min(kMinSide, longest), longest);
        const double scale = static_cast<double>(side) / longest;
        const int width = std::max(1, static_cast<int>(naturalWidth * scale + 0.5));
        const int height = std::max(1, static_cast<int>(naturalHeight * scale + 0.5));
        d3d::IDirect3DSurface surface{nullptr};
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            if (!shared->surface || shared->width != width || shared->height != height) {
                shared->surface = MakeSurface(width, height);
                shared->width = width;
                shared->height = height;
            }
            surface = shared->surface;
        }
        if (!surface) {
            shared->busy = false;
            return;
        }
        player.CopyFrameToVideoSurface(surface);
        Convert(shared, surface);
    } catch (...) {
        shared->busy = false;
    }
}

}  // namespace

std::shared_ptr<VideoFrames> VideoFrames::Start(const playback::MediaPlayer& player,
                                                const xamlimaging::SoftwareBitmapSource& source,
                                                const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
                                                std::function<void()> firstFrame) {
    std::shared_ptr<VideoFrames> frames(new VideoFrames());
    frames->m_shared = std::make_shared<Shared>();
    frames->m_shared->queue = queue;
    frames->m_shared->source = winrt::make_weak(source);
    frames->m_shared->firstFrame = std::move(firstFrame);
    frames->m_player = player;
    player.IsVideoFrameServerEnabled(true);
    std::weak_ptr<Shared> weak = frames->m_shared;
    frames->m_token = player.VideoFrameAvailable([weak](const playback::MediaPlayer& sender, auto&&) noexcept {
        if (auto shared = weak.lock()) OnFrame(shared, sender);
    });
    return frames;
}

void VideoFrames::Side(int pixels) noexcept {
    if (m_shared) m_shared->side = pixels;
}

void VideoFrames::Stop() noexcept {
    if (!m_shared) return;
    m_shared->stopped = true;
    try {
        if (m_player) m_player.VideoFrameAvailable(m_token);
    } catch (...) {
        // The player is closing: it serves no more frames anyway.
    }
    m_player = nullptr;
}

VideoFrames::~VideoFrames() {
    Stop();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
