#include "video_gif.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace ampify::win {

namespace {

using Microsoft::WRL::ComPtr;

// The GIF reaches JavaScript in base64, which the answer, its parsed object and an upload form each copy: above this
// they would pass the 32 MB an extension has. A 200 pixel motion artwork of 30 s takes under 1 MB.
constexpr size_t kMaxGifBytes = size_t{3} << 20;
// A motion artwork is 360 to 1080 pixels square: a larger frame is refused before it is copied.
constexpr UINT32 kMaxSourceSide = 2048;
// A video that gives fewer frames a second than this says nothing usable about its pace.
constexpr double kMinFrameRate = 1;
constexpr LONGLONG kTicksPerSecond = 10'000'000;  // Media Foundation counts time in 100 ns

// The functions of mfplat.dll and mfreadwrite.dll, loaded once. The GUIDs come from mfuuid.lib, which loads nothing.
struct MfApi {
    HRESULT(WINAPI* startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* shutdown)() = nullptr;
    HRESULT(WINAPI* createAttributes)(IMFAttributes**, UINT32) = nullptr;
    HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* byteStreamOnStream)(IStream*, IMFByteStream**) = nullptr;
    HRESULT(WINAPI* readerFromByteStream)(IMFByteStream*, IMFAttributes*, IMFSourceReader**) = nullptr;
    bool loaded = false;
};

template <typename F>
bool Load(HMODULE module, const char* name, F* slot) {
    *slot = module ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
    return *slot != nullptr;
}

const MfApi& Mf() {
    static MfApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE readwrite = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        api.loaded = Load(plat, "MFStartup", &api.startup) && Load(plat, "MFShutdown", &api.shutdown) &&
                     Load(plat, "MFCreateAttributes", &api.createAttributes) &&
                     Load(plat, "MFCreateMediaType", &api.createMediaType) &&
                     Load(plat, "MFCreateMFByteStreamOnStream", &api.byteStreamOnStream) &&
                     Load(readwrite, "MFCreateSourceReaderFromByteStream", &api.readerFromByteStream);
    });
    return api;
}

// A COM stream over a copy of `bytes`, at its start.
ComPtr<IStream> StreamOf(const std::string& bytes) {
    ComPtr<IStream> stream;
    ULONG written = 0;
    LARGE_INTEGER start{};
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
        FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written)) || written != bytes.size() ||
        FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
        return nullptr;
    }
    return stream;
}

// The part of a decoded frame that shows: decoders round the height up (a 360 pixel video decodes as 368).
RECT VisibleArea(IMFMediaType* type, UINT32 width, UINT32 height) {
    RECT visible{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    MFVideoArea area{};
    for (const GUID& key : {MF_MT_MINIMUM_DISPLAY_APERTURE, MF_MT_GEOMETRIC_APERTURE}) {
        UINT32 size = 0;
        if (FAILED(type->GetBlob(key, reinterpret_cast<UINT8*>(&area), sizeof area, &size)) || size != sizeof area) {
            continue;
        }
        LONG left = area.OffsetX.value;
        LONG top = area.OffsetY.value;
        if (left < 0 || top < 0 || area.Area.cx <= 0 || area.Area.cy <= 0 ||
            left + area.Area.cx > static_cast<LONG>(width) || top + area.Area.cy > static_cast<LONG>(height)) {
            continue;
        }
        return RECT{left, top, left + area.Area.cx, top + area.Area.cy};
    }
    return visible;
}

// The video's frames as top-down BGRA, read one at a time.
class FrameReader {
public:
    bool Open(const MfApi& mf, const std::string& video, std::string* error) {
        ComPtr<IStream> stream = StreamOf(video);
        ComPtr<IMFByteStream> bytes;
        ComPtr<IMFAttributes> settings;
        if (!stream || FAILED(mf.byteStreamOnStream(stream.Get(), bytes.GetAddressOf())) ||
            FAILED(mf.createAttributes(settings.GetAddressOf(), 1))) {
            *error = "the video could not be opened";
            return false;
        }
        // Without a name or a type, the reader would have to guess the container.
        ComPtr<IMFAttributes> about;
        if (SUCCEEDED(bytes.As(&about))) about->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mp4");
        settings->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);  // the decoder's YUV comes out as RGB32
        ComPtr<IMFMediaType> rgb;
        if (FAILED(mf.readerFromByteStream(bytes.Get(), settings.Get(), m_reader.GetAddressOf())) ||
            FAILED(m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) ||
            FAILED(m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE)) ||
            FAILED(mf.createMediaType(rgb.GetAddressOf())) || FAILED(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
            FAILED(m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
                                                 rgb.Get()))) {
            *error = "no video stream that Windows can decode";
            return false;
        }
        if (!ReadSize()) {
            *error = "the video has no usable size";
            return false;
        }
        return true;
    }

    UINT32 Width() const { return static_cast<UINT32>(m_visible.right - m_visible.left); }
    UINT32 Height() const { return static_cast<UINT32>(m_visible.bottom - m_visible.top); }
    double FrameRate() const { return m_rate; }  // 0 when the video does not say

    // The next frame into `bgra` (Width() x Height()) with its time, false at the end or on a failure (`error`).
    bool Next(std::vector<BYTE>* bgra, LONGLONG* time, std::string* error) {
        for (;;) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(m_reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                            time, sample.GetAddressOf()))) {
                *error = "the video could not be decoded";
                return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !ReadSize()) {
                *error = "the video changed to an unusable size";
                return false;
            }
            if (!sample) continue;  // a gap in the stream
            if (Copy(sample.Get(), bgra)) return true;
            *error = "a frame of the video could not be read";
            return false;
        }
    }

private:
    bool ReadSize() {
        ComPtr<IMFMediaType> current;
        UINT32 width = 0;
        UINT32 height = 0;
        if (FAILED(m_reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                                 current.GetAddressOf())) ||
            FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0 ||
            width > kMaxSourceSide || height > kMaxSourceSide) {
            return false;
        }
        m_width = width;
        m_height = height;
        m_defaultStride = static_cast<LONG>(MFGetAttributeUINT32(current.Get(), MF_MT_DEFAULT_STRIDE, width * 4));
        m_visible = VisibleArea(current.Get(), width, height);
        UINT32 numerator = 0;
        UINT32 denominator = 0;
        bool rated = SUCCEEDED(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &numerator, &denominator));
        double rate = rated && denominator != 0 ? static_cast<double>(numerator) / denominator : 0;
        m_rate = rate >= kMinFrameRate ? rate : 0;
        return true;
    }

    // Unlocks the buffer however the copy ends: a failed allocation must not leave it locked.
    struct Locked {
        IMF2DBuffer2* planes = nullptr;
        IMFMediaBuffer* buffer = nullptr;
        ~Locked() {
            if (planes) planes->Unlock2D();
            else if (buffer) buffer->Unlock();
        }
    };

    // The visible rows of the sample, top first: a 2D buffer gives its own pitch (negative when bottom-up). Each row
    // is checked to lie inside the buffer, which comes from a decoder fed by the network.
    bool Copy(IMFSample* sample, std::vector<BYTE>* bgra) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) return false;
        ComPtr<IMF2DBuffer2> planes;
        BYTE* first = nullptr;
        LONG pitch = 0;
        BYTE* start = nullptr;
        DWORD length = 0;
        Locked locked;
        if (SUCCEEDED(buffer.As(&planes)) &&
            SUCCEEDED(planes->Lock2DSize(MF2DBuffer_LockFlags_Read, &first, &pitch, &start, &length))) {
            locked.planes = planes.Get();
        } else {
            if (FAILED(buffer->Lock(&start, nullptr, &length))) return false;
            locked.buffer = buffer.Get();
            pitch = m_defaultStride;
            first = pitch < 0 ? start + static_cast<size_t>(-pitch) * (m_height - 1) : start;
        }
        UINT32 width = Width();
        UINT32 height = Height();
        auto row = [&](LONG index) { return first + static_cast<ptrdiff_t>(pitch) * index + m_visible.left * 4; };
        const BYTE* top = row(m_visible.top);
        const BYTE* bottom = row(m_visible.bottom - 1);
        const BYTE* lowest = (std::min)(top, bottom);
        const BYTE* highest = (std::max)(top, bottom) + static_cast<size_t>(width) * 4;
        if (std::abs(pitch) < static_cast<LONG>(width * 4) || lowest < start || highest > start + length) return false;
        bgra->resize(static_cast<size_t>(width) * height * 4);
        for (UINT32 index = 0; index < height; index++) {
            std::memcpy(bgra->data() + static_cast<size_t>(index) * width * 4,
                        row(m_visible.top + static_cast<LONG>(index)), static_cast<size_t>(width) * 4);
        }
        return true;
    }

    ComPtr<IMFSourceReader> m_reader;
    UINT32 m_width = 0;
    UINT32 m_height = 0;
    LONG m_defaultStride = 0;
    RECT m_visible{};
    double m_rate = 0;
};

bool SetBytes(IWICMetadataQueryWriter* writer, const wchar_t* name, const UCHAR* bytes, ULONG count) {
    PROPVARIANT value;
    PropVariantInit(&value);
    value.vt = VT_UI1 | VT_VECTOR;
    value.caub.cElems = count;
    value.caub.pElems = const_cast<UCHAR*>(bytes);
    return SUCCEEDED(writer->SetMetadataByName(name, &value));
}

bool SetShort(IWICMetadataQueryWriter* writer, const wchar_t* name, USHORT number) {
    PROPVARIANT value;
    PropVariantInit(&value);
    value.vt = VT_UI2;
    value.uiVal = number;
    return SUCCEEDED(writer->SetMetadataByName(name, &value));
}

// Writes the GIF a frame at a time, each with its own 256 colours, which suits a video's changing pictures.
class GifWriter {
public:
    bool Open(UINT width, UINT height) {
        m_width = width;
        m_height = height;
        ComPtr<IWICMetadataQueryWriter> global;
        constexpr UCHAR kApplication[] = {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
        constexpr UCHAR kLoopForEver[] = {3, 1, 0, 0, 0};  // a sub-block of 3 bytes: 1, then 0 repeats (two bytes)
        bool opened = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                 IID_PPV_ARGS(m_factory.GetAddressOf()))) &&
                      SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, m_stream.GetAddressOf())) &&
                      SUCCEEDED(m_factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, m_encoder.GetAddressOf())) &&
                      SUCCEEDED(m_encoder->Initialize(m_stream.Get(), WICBitmapEncoderNoCache)) &&
                      SUCCEEDED(m_encoder->GetMetadataQueryWriter(global.GetAddressOf())) &&
                      SetBytes(global.Get(), L"/appext/application", kApplication, sizeof kApplication) &&
                      SetBytes(global.Get(), L"/appext/data", kLoopForEver, sizeof kLoopForEver);
        if (opened) {
            // The logical screen takes the first frame's size when it is not given: given, but not required.
            SetShort(global.Get(), L"/logscrdesc/Width", static_cast<USHORT>(width));
            SetShort(global.Get(), L"/logscrdesc/Height", static_cast<USHORT>(height));
        }
        return opened;
    }

    // One frame of the video (sourceWidth x sourceHeight, BGRA), scaled to the GIF, shown for `delay` hundredths.
    bool Add(std::vector<BYTE>& bgra, UINT sourceWidth, UINT sourceHeight, USHORT delay) {
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICPalette> palette;
        if (FAILED(m_factory->CreateBitmapFromMemory(sourceWidth, sourceHeight, GUID_WICPixelFormat32bppBGR,
                                                     sourceWidth * 4, static_cast<UINT>(bgra.size()), bgra.data(),
                                                     bitmap.GetAddressOf())) ||
            FAILED(m_factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(bitmap.Get(), m_width, m_height, WICBitmapInterpolationModeFant)) ||
            FAILED(m_factory->CreatePalette(palette.GetAddressOf())) ||
            FAILED(palette->InitializeFromBitmap(scaler.Get(), 256, FALSE))) {
            return false;
        }
        ComPtr<IWICFormatConverter> indexed;
        if (FAILED(m_factory->CreateFormatConverter(indexed.GetAddressOf())) ||
            FAILED(indexed->Initialize(scaler.Get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeErrorDiffusion,
                                       palette.Get(), 0.0, WICBitmapPaletteTypeCustom))) {
            return false;
        }
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        ComPtr<IWICMetadataQueryWriter> about;
        WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
        return SUCCEEDED(m_encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf())) &&
               SUCCEEDED(frame->Initialize(properties.Get())) && SUCCEEDED(frame->SetSize(m_width, m_height)) &&
               SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat8bppIndexed &&
               SUCCEEDED(frame->SetPalette(palette.Get())) &&
               SUCCEEDED(frame->GetMetadataQueryWriter(about.GetAddressOf())) &&
               SetShort(about.Get(), L"/grctlext/Delay", delay) &&
               SUCCEEDED(frame->WriteSource(indexed.Get(), nullptr)) && SUCCEEDED(frame->Commit());
    }

    // True once the frames written so far pass kMaxGifBytes: checked after each frame, before the next one grows it.
    bool TooLarge() const {
        STATSTG about{};
        return FAILED(m_stream->Stat(&about, STATFLAG_NONAME)) || about.cbSize.QuadPart > kMaxGifBytes;
    }

    // The GIF's bytes, or nullopt when it could not be finished.
    std::optional<std::string> Finish() {
        STATSTG about{};
        LARGE_INTEGER start{};
        if (FAILED(m_encoder->Commit()) || FAILED(m_stream->Stat(&about, STATFLAG_NONAME)) ||
            about.cbSize.QuadPart > kMaxGifBytes || FAILED(m_stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
            return std::nullopt;
        }
        std::string bytes(static_cast<size_t>(about.cbSize.QuadPart), '\0');
        ULONG read = 0;
        if (FAILED(m_stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size()) {
            return std::nullopt;
        }
        return bytes;
    }

private:
    ComPtr<IWICImagingFactory> m_factory;
    ComPtr<IStream> m_stream;
    ComPtr<IWICBitmapEncoder> m_encoder;
    UINT m_width = 0;
    UINT m_height = 0;
};

std::optional<std::string> Convert(const MfApi& mf, const std::string& video, const GifHost::Options& options,
                                   std::string* error) {
    FrameReader reader;
    if (!reader.Open(mf, video, error)) return std::nullopt;
    double scale = std::min(1.0, static_cast<double>(options.side) / std::max(reader.Width(), reader.Height()));
    UINT width = std::max<UINT>(1, static_cast<UINT>(std::lround(reader.Width() * scale)));
    UINT height = std::max<UINT>(1, static_cast<UINT>(std::lround(reader.Height() * scale)));
    GifWriter gif;
    if (!gif.Open(width, height)) {
        *error = "the GIF could not be started";
        return std::nullopt;
    }
    // Every frame of the video is decoded; one every `step` is kept, never more than the video has. A GIF counts in
    // hundredths of a second, so the delays alternate (12 and 13 for 8 a second) to keep the pace.
    double fps = options.fps;
    if (reader.FrameRate() > 0) fps = std::min(fps, reader.FrameRate());
    const LONGLONG step = static_cast<LONGLONG>(kTicksPerSecond / fps);
    // A frame of the video stands for the GIF's when it is within half a frame of the video: half a GIF frame would
    // keep the video's last one as one more (21 frames for a 2 s loop at 10 a second).
    const LONGLONG slack = reader.FrameRate() > 0 ? static_cast<LONGLONG>(kTicksPerSecond / reader.FrameRate() / 2)
                                                  : step / 2;
    const LONGLONG end = static_cast<LONGLONG>(options.maxMs) * (kTicksPerSecond / 1000);
    std::vector<BYTE> bgra;
    LONGLONG time = 0;
    LONGLONG first = -1;
    LONGLONG next = 0;
    size_t frames = 0;
    double shown = 0;  // hundredths given so far
    while (reader.Next(&bgra, &time, error)) {
        if (first < 0) first = time;
        LONGLONG at = time - first;
        if (at >= end) break;
        if (at + slack < next) continue;
        next += step;
        frames++;
        double due = 100.0 * static_cast<double>(frames) / fps;
        USHORT delay = static_cast<USHORT>(std::clamp(std::round(due - shown), 1.0, 65535.0));
        shown += delay;
        if (!gif.Add(bgra, reader.Width(), reader.Height(), delay)) {
            *error = "a frame of the GIF could not be written";
            return std::nullopt;
        }
        if (gif.TooLarge()) {
            *error = "the GIF would be over 3 MB: a smaller side, fewer frames a second or fewer seconds";
            return std::nullopt;
        }
    }
    if (!error->empty()) return std::nullopt;
    if (frames == 0) {
        *error = "the video has no frame";
        return std::nullopt;
    }
    std::optional<std::string> bytes = gif.Finish();
    if (!bytes) *error = "the GIF could not be finished, or is over 3 MB";
    return bytes;
}

}  // namespace

std::optional<std::string> GifFromVideoWithMf(const std::string& video, const GifHost::Options& options,
                                              std::string* error) {
    error->clear();
    if (video.empty() || video.size() > MAXDWORD || options.side == 0 || options.fps == 0 || options.maxMs == 0) {
        *error = "no video to read";
        return std::nullopt;
    }
    const MfApi& mf = Mf();
    if (!mf.loaded) {
        *error = "Media Foundation is not installed (a Windows N edition needs its Media Feature Pack)";
        return std::nullopt;
    }
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        *error = "COM could not start on this thread";
        return std::nullopt;
    }
    std::optional<std::string> gif;
    if (SUCCEEDED(mf.startup(MF_VERSION, MFSTARTUP_LITE))) {
        gif = Convert(mf, video, options, error);  // its COM objects are gone before Media Foundation shuts down
        mf.shutdown();
    } else {
        *error = "Media Foundation could not start";
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return gif;
}

}  // namespace ampify::win
