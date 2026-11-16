#pragma once

// What the studio voice remover needs on top of Ampify, downloaded on first use into the kit's `sing` folder by
// `ampify sing install` (src/cli/sing.cpp), read by the audio process (src/win/audio/sing.cpp). Pinned by size and
// SHA-256: a file that differs is refused. ONNX Runtime with DirectML comes from its official Python wheel, which
// holds both libraries at a quarter of the NuGet packages' size; the model is UVR's MDX-Net Voc_FT. Both MIT.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace ampify::sing_pack {

struct Download {
    std::string_view name;  // file name in the downloads folder while it comes in
    std::string_view url;
    std::uint64_t size;
    std::string_view sha256;
};

inline constexpr Download kRuntime{
    "onnxruntime_directml-1.24.4-cp311-cp311-win_amd64.whl",
    "https://files.pythonhosted.org/packages/be/90/99566dc6398028e7691a5b12720fd85f757a0901818b84599d28abb3f085/"
    "onnxruntime_directml-1.24.4-cp311-cp311-win_amd64.whl",
    25106000, "96642a787e5a6f33bf043521c0f06eb1eb663f6b830e5862a2026d03f9c90543"};

inline constexpr Download kModel{
    "UVR-MDX-NET-Voc_FT.onnx",
    "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-MDX-NET-Voc_FT.onnx", 66762490,
    "534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111"};

// The wheel's entries kept, and the names they take in the `sing` folder.
inline constexpr std::string_view kRuntimeEntries[] = {"onnxruntime/capi/onnxruntime.dll",
                                                       "onnxruntime/capi/DirectML.dll",
                                                       "onnxruntime/capi/onnxruntime_providers_shared.dll"};
inline constexpr std::string_view kRuntimeLibrary = "onnxruntime.dll";
inline constexpr std::string_view kDirectMlLibrary = "DirectML.dll";
inline constexpr std::string_view kModelFile = "vocals.onnx";
// Written last: its presence says every file above is in place and checked.
inline constexpr std::string_view kReadyFile = "ready.txt";
inline constexpr std::string_view kReadyText = "sing pack 1";

// Whether `folder` holds a finished pack: its ready file says so and every file it stands for is there.
inline bool Ready(const std::filesystem::path& folder) {
    std::ifstream in(folder / std::string(kReadyFile));
    std::string line;
    if (!in || !std::getline(in, line) || line != kReadyText) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(folder / std::string(kModelFile), ec)) return false;
    for (std::string_view entry : kRuntimeEntries) {
        const std::filesystem::path name = std::filesystem::path(std::string(entry)).filename();
        if (!std::filesystem::is_regular_file(folder / name, ec)) return false;
    }
    return true;
}

}  // namespace ampify::sing_pack
