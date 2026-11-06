#pragma once

// What the tests share: scratch folders, element folders with a manifest, the kernel pieces a loader test wires
// together, the interface language, and the synthetic signals. A manifest field or a constructor that changes is one
// edit here, not one per test file.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "../src/core/extension.hpp"
#include "../src/core/extension_loader.hpp"
#include "../src/core/i18n.hpp"
#include "../src/core/journal.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "../src/core/settings.hpp"
#include "fake_host.hpp"

namespace ampify::test {

// ---- Folders ------------------------------------------------------------------------------------------------------

// One folder per process run, so two runs of the suite at once never delete each other's files.
inline const std::filesystem::path& ScratchRoot() {
#ifdef _WIN32
    static const int process = _getpid();
#else
    static const int process = static_cast<int>(getpid());
#endif
    // Removed at exit when empty: every ScratchDir has removed its own folder by then, and what a crash left stays.
    struct Root {
        std::filesystem::path path;
        ~Root() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };
    static const Root root{std::filesystem::temp_directory_path() / ("ampify-tests-" + std::to_string(process))};
    return root.path;
}

// A new empty folder under ScratchRoot, removed with the object. `name` only says what it is for: a counter keeps
// every folder of the run apart, so a name may repeat and a leftover of an earlier run never feeds this one.
class ScratchDir {
public:
    explicit ScratchDir(const std::string& name) {
        static int made = 0;
        path_ = ScratchRoot() / (std::to_string(++made) + "-" + name);
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~ScratchDir() { Remove(); }

    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }

    const std::filesystem::path& path() const { return path_; }
    std::string string() const { return path_.string(); }
    operator const std::filesystem::path&() const { return path_; }
    std::filesystem::path operator/(const std::filesystem::path& child) const { return path_ / child; }

private:
    void Remove() {
        if (path_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::remove(ScratchRoot(), ignored);  // only succeeds once the last folder is gone
    }

    std::filesystem::path path_;
};

inline void WriteFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

inline std::string ReadFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---- Elements -----------------------------------------------------------------------------------------------------

// The fields of a test manifest.json. `permissions` and `processes` are the inside of the JSON list, as typed:
// R"("ui.panel", "storage")". `processes` is left out when empty (the element then runs in the interface only), and
// `extra` is more members, as typed and without a leading comma: R"("colors":"colors.ini")".
struct ManifestSpec {
    std::string id;
    std::string name = "Demo";
    std::string kind = "extension";
    std::string version = "1.0.0";
    std::string author = "Shirowwww";
    std::string description = "Test.";
    bool script = true;  // false: no "main" member, for a theme or a snippet that is only files
    std::string permissions;
    std::string processes;
    std::string extra;
};

inline std::string ManifestJson(const ManifestSpec& spec) {
    std::string json = R"({"id":")" + spec.id + R"(","name":")" + spec.name + R"(","version":")" + spec.version +
                       R"(","kind":")" + spec.kind + R"(","author":")" + spec.author + R"(","description":")" +
                       spec.description + R"(","kitApi":"1.0",)" + (spec.script ? R"("main":"main.js",)" : "") +
                       R"("permissions":[)" + spec.permissions + "]";
    if (!spec.processes.empty()) json += R"(,"processes":[)" + spec.processes + "]";
    if (!spec.extra.empty()) json += "," + spec.extra;
    return json + "}";
}

// Writes `folder`/manifest.json from `manifest` and `folder`/main.js from `main`, which an element with no script
// leaves out.
inline void WriteElement(const std::filesystem::path& folder, const std::string& manifest,
                         const std::string* main = nullptr) {
    WriteFile(folder / "manifest.json", manifest);
    if (main) WriteFile(folder / "main.js", *main);
}

inline void WriteElement(const std::filesystem::path& folder, const ManifestSpec& spec, const std::string& main) {
    WriteElement(folder, ManifestJson(spec), &main);
}

// The manifest as the kernel reads it; throws with the reasons when it is refused.
inline Manifest ParseTestManifest(const std::string& text, const std::string& folderName) {
    ManifestResult result = ParseManifest(text, folderName, 1, [](const std::string&) { return true; });
    if (!result.manifest) {
        std::string reasons;
        for (const std::string& error : result.errors) reasons += error + "; ";
        throw std::runtime_error("test manifest refused: " + reasons);
    }
    return *result.manifest;
}

// One element in a scratch folder of its own.
struct ElementFolder {
    ScratchDir dir;
    Manifest manifest;

    ElementFolder(const ManifestSpec& spec, const std::string& main)
        : dir(spec.id), manifest(ParseTestManifest(ManifestJson(spec), spec.id)) {
        WriteElement(dir.path(), spec, main);
    }
};

// The shipped example (or theme, or snippet) `kind`/`name`, as it is in the repository.
struct ShippedElement {
    std::filesystem::path dir;
    std::optional<Manifest> manifest;  // empty when the manifest is refused

    ShippedElement(const std::string& kind, const std::string& name)
        : dir(std::filesystem::path(AMPIFY_SOURCE_DIR) / kind / name) {
        ManifestResult result = ParseManifest(ReadFile(dir / "manifest.json"), name, 1, [this](const std::string& file) {
            return std::filesystem::exists(dir / file);
        });
        manifest = result.manifest;
    }
};

// Builds elements against any host and journal. Their folders live as long as the shelf.
class ElementShelf {
public:
    std::unique_ptr<ScriptExtension> Make(const ManifestSpec& spec, const std::string& main, Host& host,
                                          Journal& journal, Clock clock = {}, js::Limits limits = {}) {
        folders_.push_back(std::make_unique<ElementFolder>(spec, main));
        const ElementFolder& folder = *folders_.back();
        return std::make_unique<ScriptExtension>(folder.manifest, folder.dir.path(), host, journal, clock, limits);
    }

private:
    std::vector<std::unique_ptr<ElementFolder>> folders_;
};

// Elements run against a host of type H, with a journal on a string.
template <class H = FakeHost>
struct ElementFixture {
    H host;
    std::ostringstream out;
    Journal journal{out, [] { return std::string("t"); }};
    ElementShelf shelf;

    std::unique_ptr<ScriptExtension> Make(const ManifestSpec& spec, const std::string& main, Clock clock = {},
                                          js::Limits limits = {}) {
        return shelf.Make(spec, main, host, journal, clock, limits);
    }

    std::unique_ptr<ScriptExtension> Make(const ManifestSpec& spec, const std::string& main, Host& onHost,
                                          Clock clock = {}, js::Limits limits = {}) {
        return shelf.Make(spec, main, onHost, journal, clock, limits);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

// The loader and what it needs, writing to a journal on a string: for the tests of discovery, link and theme scripts.
struct KernelFixture {
    FakeHost host;
    std::ostringstream out;
    Journal journal{out, [] { return std::string("t"); }};
    ExtensionHost hosts{journal};
    ExtensionLoader loader;
    Settings settings;

    // Reads the elements in `kit`, or in its folder `extensions` when given.
    explicit KernelFixture(const ScratchDir& kit, const std::string& extensions = "", const std::string& process = "ui")
        : loader(hosts, host, journal, extensions.empty() ? kit.path() : kit.path() / extensions) {
        host.process = process;
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

// A kit folder with its `extensions` and `apps` folders, and the kernel pieces that read it.
struct KitRoot {
    ScratchDir root;
    explicit KitRoot(const std::string& name) : root(name) {
        std::filesystem::create_directories(root / "extensions");
        std::filesystem::create_directories(root / "apps");
    }
};

struct KitFixture : KitRoot, KernelFixture {
    explicit KitFixture(const std::string& name, const std::string& process = "ui")
        : KitRoot(name), KernelFixture(root, "extensions", process) {
        loader.SetAppRoot(root / "apps");
    }
};

// ---- Language -----------------------------------------------------------------------------------------------------

// Sets the interface language for the scope and puts the previous one back, even when the case throws.
class LanguageScope {
public:
    explicit LanguageScope(const std::string& code) : before_(i18n::Language()) { i18n::SetLanguage(code); }
    ~LanguageScope() { i18n::SetLanguage(before_); }

    LanguageScope(const LanguageScope&) = delete;
    LanguageScope& operator=(const LanguageScope&) = delete;

private:
    std::string before_;
};

// ---- Signals ------------------------------------------------------------------------------------------------------

// Interleaved sine, the same on every channel.
inline std::vector<float> Sine(double frequency, double amplitude, double sampleRate, size_t frames, int channels = 2,
                               size_t phase = 0) {
    std::vector<float> out(frames * static_cast<size_t>(channels));
    for (size_t i = 0; i < frames; i++) {
        float value = static_cast<float>(
            amplitude * std::sin(2 * std::numbers::pi * frequency * static_cast<double>(i + phase) / sampleRate));
        for (int c = 0; c < channels; c++) out[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = value;
    }
    return out;
}

// The same, for `seconds`.
inline std::vector<float> SineSeconds(double sampleRate, int channels, double seconds, double frequency,
                                      double amplitude) {
    return Sine(frequency, amplitude, sampleRate, static_cast<size_t>(sampleRate * seconds), channels);
}

// The same mono signal on two channels, interleaved as a stereo stream is.
inline std::vector<float> Stereo(const std::vector<float>& mono) {
    std::vector<float> interleaved(mono.size() * 2);
    for (size_t frame = 0; frame < mono.size(); frame++) {
        interleaved[frame * 2] = mono[frame];
        interleaved[frame * 2 + 1] = mono[frame];
    }
    return interleaved;
}

// A click (a 2 ms burst) every 60 / bpm seconds, stereo.
inline std::vector<float> Clicks(double bpm, double rate, double seconds) {
    size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> out(frames * 2, 0.0f);
    double period = 60.0 / bpm * rate;
    size_t width = static_cast<size_t>(rate * 0.002);
    for (double start = 0; start < static_cast<double>(frames); start += period) {
        size_t first = static_cast<size_t>(start);
        for (size_t i = first; i < std::min(frames, first + width); i++) out[2 * i] = out[2 * i + 1] = 0.8f;
    }
    return out;
}

// `frames` stereo frames at 48 kHz from frame `first` on: a 1 kHz tone with a click every 0.5 s (120 BPM).
inline std::vector<float> ToneWithClicks(size_t first, size_t frames) {
    std::vector<float> block(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        size_t n = first + i;
        double tone = 0.25 * std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(n) / 48000.0);
        double click = (n % 24000) < 96 ? 0.6 : 0.0;
        block[2 * i] = block[2 * i + 1] = static_cast<float>(tone + click);
    }
    return block;
}

}  // namespace ampify::test
