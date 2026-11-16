#include "sing_model.hpp"

#include <windows.h>

#include <array>
#include <cstring>
#include <cstdint>

#include "../../core/sing_pack.hpp"
#include "onnxruntime_c_api.h"

namespace ampify::win::sing {

namespace {

constexpr std::int64_t kShape[] = {1, 4, static_cast<std::int64_t>(ampify::sing::kBins), static_cast<std::int64_t>(ampify::sing::kFrames)};
constexpr size_t kValues = ampify::sing::kSpectrumSize;

using GetApiBase_t = const OrtApiBase*(ORT_API_CALL*)();
using AppendDml_t = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, int);

}  // namespace

struct OrtVocalModel::Handles {
    HMODULE directMl = nullptr;
    HMODULE runtime = nullptr;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;

    // Empty when the call succeeded, else ONNX Runtime's message. The status is released either way.
    std::string Check(OrtStatus* status) const {
        if (!status) return {};
        std::string message = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        return message.empty() ? "ONNX Runtime failed" : message;
    }

    ~Handles() {
        if (api) {
            if (memory) api->ReleaseMemoryInfo(memory);
            if (session) api->ReleaseSession(session);
            if (options) api->ReleaseSessionOptions(options);
            if (env) api->ReleaseEnv(env);
        }
        // The libraries stay loaded: DirectML keeps threads of its own that may outlive the session by a moment.
    }
};

namespace {

// DirectML first, by its full path, so ONNX Runtime finds this copy and not an older one in System32.
std::string LoadLibraries(const std::filesystem::path& folder, HMODULE& directMl, HMODULE& runtime) {
    directMl = LoadLibraryExW((folder / std::string(sing_pack::kDirectMlLibrary)).c_str(), nullptr,
                              LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!directMl) return "DirectML.dll could not be loaded (code " + std::to_string(GetLastError()) + ")";
    runtime = LoadLibraryExW((folder / std::string(sing_pack::kRuntimeLibrary)).c_str(), nullptr,
                             LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!runtime) return "onnxruntime.dll could not be loaded (code " + std::to_string(GetLastError()) + ")";
    return {};
}

// The graphics card that performs best (a laptop's discrete one, not its integrated one); the plain DirectML entry
// when this runtime does not take those options.
std::string AppendDirectMl(const OrtApi* api, OrtSessionOptions* options, HMODULE runtime) {
    const char* keys[] = {"performance_preference", "device_filter"};
    const char* values[] = {"high_performance", "gpu"};
    OrtStatus* status = api->SessionOptionsAppendExecutionProvider(options, "DML", keys, values, 2);
    if (!status) return {};
    api->ReleaseStatus(status);
    auto append = reinterpret_cast<AppendDml_t>(GetProcAddress(runtime, "OrtSessionOptionsAppendExecutionProvider_DML"));
    if (!append) return "this ONNX Runtime has no DirectML";
    status = append(options, 0);
    if (!status) return {};
    std::string message = api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return "no graphics card DirectML can use (" + message + ")";
}

}  // namespace

std::unique_ptr<OrtVocalModel> OrtVocalModel::Load(const std::filesystem::path& folder, std::string& why) {
    std::unique_ptr<OrtVocalModel> model(new OrtVocalModel());
    model->m_handles = std::make_unique<Handles>();
    Handles& h = *model->m_handles;
    why = LoadLibraries(folder, h.directMl, h.runtime);
    if (!why.empty()) return nullptr;
    auto getApiBase = reinterpret_cast<GetApiBase_t>(GetProcAddress(h.runtime, "OrtGetApiBase"));
    h.api = getApiBase ? getApiBase()->GetApi(ORT_API_VERSION) : nullptr;
    if (!h.api) {
        why = "this ONNX Runtime does not offer version " + std::to_string(ORT_API_VERSION) + " of its interface";
        return nullptr;
    }
    why = h.Check(h.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "ampify-sing", &h.env));
    if (why.empty()) why = h.Check(h.api->CreateSessionOptions(&h.options));
    // DirectML wants one run at a time and no memory pattern.
    if (why.empty()) why = h.Check(h.api->DisableMemPattern(h.options));
    if (why.empty()) why = h.Check(h.api->SetSessionExecutionMode(h.options, ORT_SEQUENTIAL));
    if (why.empty()) why = h.Check(h.api->SetSessionGraphOptimizationLevel(h.options, ORT_ENABLE_ALL));
    if (why.empty()) why = AppendDirectMl(h.api, h.options, h.runtime);
    if (why.empty()) {
        const std::filesystem::path file = folder / std::string(sing_pack::kModelFile);
        why = h.Check(h.api->CreateSession(h.env, file.c_str(), h.options, &h.session));
    }
    if (why.empty()) why = h.Check(h.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &h.memory));
    if (!why.empty()) return nullptr;
    return model;
}

OrtVocalModel::~OrtVocalModel() = default;

bool OrtVocalModel::Run(const float* spectrum, float* vocals) {
    Handles& h = *m_handles;
    OrtValue* input = nullptr;
    OrtValue* output = nullptr;
    // ONNX Runtime reads the input in place: the cast does not let it write there.
    m_lastError = h.Check(h.api->CreateTensorWithDataAsOrtValue(
        h.memory, const_cast<float*>(spectrum), kValues * sizeof(float), kShape, std::size(kShape),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
    const char* inputNames[] = {"input"};
    const char* outputNames[] = {"output"};
    if (m_lastError.empty()) {
        m_lastError = h.Check(h.api->Run(h.session, nullptr, inputNames, &input, 1, outputNames, 1, &output));
    }
    float* data = nullptr;
    if (m_lastError.empty()) m_lastError = h.Check(h.api->GetTensorMutableData(output, reinterpret_cast<void**>(&data)));
    if (m_lastError.empty() && data) std::memcpy(vocals, data, kValues * sizeof(float));
    if (output) h.api->ReleaseValue(output);
    if (input) h.api->ReleaseValue(input);
    return m_lastError.empty() && data;
}

}  // namespace ampify::win::sing
