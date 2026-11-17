#pragma once

// What the studio voice remover needs on top of Ampify, downloaded on first use into the kit's `sing` folder by
// `ampify sing install` (src/cli/sing.cpp), read by the audio process (src/win/audio/sing.cpp). Three parts, each
// fetched only when wanted: ONNX Runtime with DirectML (from its official Python wheel, which holds both libraries at
// a quarter of the NuGet packages' size), and one file per vocal model. Every file is pinned by size and SHA-256: a
// file that differs is refused. A part is in place when its marker file holds its SHA-256, written once its files are
// checked; a part whose download changes is thus fetched again. All MIT.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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

// The wheel's entries kept, and the names they take in the `sing` folder.
inline constexpr std::string_view kRuntimeEntries[] = {"onnxruntime/capi/onnxruntime.dll",
                                                       "onnxruntime/capi/DirectML.dll",
                                                       "onnxruntime/capi/onnxruntime_providers_shared.dll"};
inline constexpr std::string_view kRuntimeLibrary = "onnxruntime.dll";
inline constexpr std::string_view kDirectMlLibrary = "DirectML.dll";
inline constexpr std::string_view kRuntimeMarker = "runtime.ready";

struct Model {
    std::string_view id;      // as src/core/sing/vocal_model.hpp's ModelSpec
    std::string_view file;    // in the `sing` folder
    std::string_view marker;  // in the `sing` folder, holds the download's SHA-256 once the file is in place
    Download download;
};

// UVR's MDX-Net Voc_FT with its weights in float16 (inputs and outputs float32), kept with Ampify's catalog: an output
// within -58 dB of the original's.
inline constexpr Model kStandard{
    "standard", "standard.onnx", "standard.ready",
    {"UVR-MDX-NET-Voc_FT-fp16.onnx",
     "https://github.com/Shirowwww/ampify-catalog/releases/download/sing-model-1/UVR-MDX-NET-Voc_FT-fp16.onnx",
     33401864, "44f9ccc018145b00ed8c6a7fb6ce331986c7c7ff62d3ddb5709c969be2604a7d"}};

// Kim's Mel-Band RoFormer (Kimberley Jensen's weights, ZFTurbo's code), exported for DirectML in float16.
inline constexpr Model kPrecise{
    "precise", "precise.onnx", "precise.ready",
    {"mel-band-roformer-kim-fp16.onnx",
     "https://github.com/Shirowwww/ampify-catalog/releases/download/sing-model-2/mel-band-roformer-kim-fp16.onnx",
     464862435, "944124cbf582161b37629fe5e6a127df10e031174c7b7bf87855a35e949a7ee3"}};

inline constexpr const Model* kModels[] = {&kStandard, &kPrecise};

inline const Model* FindModel(std::string_view id) {
    for (const Model* model : kModels) {
        if (model->id == id) return model;
    }
    return nullptr;
}

// Whether `marker` in `folder` holds `sha256`.
inline bool Marked(const std::filesystem::path& folder, std::string_view marker, std::string_view sha256) {
    std::ifstream in(folder / std::string(marker));
    std::string line;
    return in && std::getline(in, line) && line == sha256;
}

inline bool RuntimeReady(const std::filesystem::path& folder) {
    if (!Marked(folder, kRuntimeMarker, kRuntime.sha256)) return false;
    std::error_code ec;
    for (std::string_view entry : kRuntimeEntries) {
        const std::filesystem::path name = std::filesystem::path(std::string(entry)).filename();
        if (!std::filesystem::is_regular_file(folder / name, ec)) return false;
    }
    return true;
}

inline bool ModelReady(const std::filesystem::path& folder, const Model& model) {
    std::error_code ec;
    return Marked(folder, model.marker, model.download.sha256) &&
           std::filesystem::is_regular_file(folder / std::string(model.file), ec);
}

// Whether `folder` holds all `model` needs: the runtime and its file.
inline bool Ready(const std::filesystem::path& folder, const Model& model) {
    return RuntimeReady(folder) && ModelReady(folder, model);
}

// What using `model` downloads, in bytes: its file, and the runtime when the folder has none.
inline std::uint64_t DownloadSize(const std::filesystem::path& folder, const Model& model) {
    return (RuntimeReady(folder) ? 0 : kRuntime.size) + (ModelReady(folder, model) ? 0 : model.download.size);
}

}  // namespace ampify::sing_pack
