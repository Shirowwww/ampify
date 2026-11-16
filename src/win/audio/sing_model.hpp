#pragma once

// Sing's studio voice remover on the graphics card: UVR's MDX-Net Voc_FT run by ONNX Runtime with DirectML, both
// loaded from the kit's `sing` folder (src/core/sing_pack.hpp). One window of the model's spectrum in, the voice's
// out. Used from Sing's worker thread only.

#include <filesystem>
#include <memory>
#include <string>

#include "../../core/sing/vocal_model.hpp"

namespace ampify::win::sing {

class OrtVocalModel final : public ampify::sing::VocalModel {
public:
    // Null with `why` filled when the libraries, the graphics card or the model cannot be used.
    static std::unique_ptr<OrtVocalModel> Load(const std::filesystem::path& folder, std::string& why);
    ~OrtVocalModel() override;
    OrtVocalModel(const OrtVocalModel&) = delete;
    OrtVocalModel& operator=(const OrtVocalModel&) = delete;

    bool Run(const float* spectrum, float* vocals) override;
    // The last run's failure, for the journal.
    const std::string& LastError() const { return m_lastError; }

private:
    OrtVocalModel() = default;
    struct Handles;
    std::unique_ptr<Handles> m_handles;
    std::string m_lastError;
};

}  // namespace ampify::win::sing
