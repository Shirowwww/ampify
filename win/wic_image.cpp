#include "wic_image.hpp"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>

namespace ampify::win {

namespace {

// One COM reference, released when it goes out of scope.
template <typename T>
class Com {
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (m_ptr) m_ptr->Release();
    }

    T** Put() { return &m_ptr; }
    T* Get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }

private:
    T* m_ptr = nullptr;
};

// An artwork is about 1000 px wide: a larger source is refused before its pixels are decoded.
constexpr UINT kMaxSourceSide = 8192;

std::optional<RgbaImage> Decode(const std::string& bytes, uint32_t maxSide) {
    Com<IWICImagingFactory> factory;
    Com<IWICStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                                reinterpret_cast<void**>(factory.Put()))) ||
        FAILED(factory->CreateStream(stream.Put())) ||
        FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),
                                            static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.Put())) ||
        FAILED(decoder->GetFrame(0, frame.Put()))) {
        return std::nullopt;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 || width > kMaxSourceSide ||
        height > kMaxSourceSide) {
        return std::nullopt;
    }
    double scale = std::min(1.0, static_cast<double>(maxSide) / std::max(width, height));
    UINT outWidth = std::max<UINT>(1, static_cast<UINT>(width * scale));
    UINT outHeight = std::max<UINT>(1, static_cast<UINT>(height * scale));

    Com<IWICBitmapScaler> scaler;
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(scaler.Put())) ||
        FAILED(scaler->Initialize(frame.Get(), outWidth, outHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(converter.Put())) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }

    RgbaImage image;
    image.width = outWidth;
    image.height = outHeight;
    image.pixels.resize(static_cast<size_t>(outWidth) * outHeight * 4);
    if (FAILED(converter->CopyPixels(nullptr, outWidth * 4, static_cast<UINT>(image.pixels.size()),
                                     image.pixels.data()))) {
        return std::nullopt;
    }
    return image;
}

}  // namespace

std::optional<RgbaImage> DecodeWithWic(const std::string& bytes, uint32_t maxSide) {
    if (bytes.empty() || bytes.size() > MAXDWORD || maxSide == 0) return std::nullopt;
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return std::nullopt;
    std::optional<RgbaImage> image = Decode(bytes, maxSide);  // its COM objects are gone before the apartment is left
    if (SUCCEEDED(com)) CoUninitialize();
    return image;
}

}  // namespace ampify::win
