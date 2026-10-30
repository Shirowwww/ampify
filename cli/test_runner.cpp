// `amk test`: runs an element's main.js in the kernel's JavaScript engine, with a host that records what the element
// asks for. Nothing reaches Apple Music, the network or the disk outside the test: the host answers in memory.

#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <sstream>
#include <vector>

#include "../core/audio/audio_engine.hpp"
#include "../core/host.hpp"
#include "../core/natives/discord.hpp"
#include "../core/natives/fs.hpp"
#include "../core/natives/http.hpp"
#include "../core/natives/midi.hpp"
#include "../core/natives/remote.hpp"
#include "../core/natives/tray.hpp"
#include "../core/journal.hpp"
#include "../core/manifest.hpp"
#include "../core/ui_model.hpp"
#include "../core/script_extension.hpp"
#include "archive.hpp"
#include "internal.hpp"

namespace amk::cli {

namespace {

// AMK.audio during a test run: the kernel's real analysis and equalizer, fed with a synthetic signal instead of a
// sound card (see PlaySyntheticAudio). The loudness and the player volume are fixed values.
class TestAudio : public audio::AudioEngine {
public:
    explicit TestAudio(std::function<void(const std::string&)> count) : m_count(std::move(count)) {
        PublishPlayerVolume(0.5);
    }

    void SubscribeFrames(const std::string& elementId, FrameSink sink) override {
        m_count("audio : images d'analyse");
        AudioEngine::SubscribeFrames(elementId, std::move(sink));
    }
    AudioAnalysisResult Analyze(const std::string& elementId) override {
        m_count("audio : analyse");
        return AudioEngine::Analyze(elementId);
    }
    LoudnessReading Loudness() override {
        PublishLoudness({-14.0, -14.0});
        return AudioEngine::Loudness();
    }
    std::optional<double> PlayerVolume() override {
        m_count("audio : volume (lecture)");
        return AudioEngine::PlayerVolume();
    }
    bool SetEqBand(const std::string& elementId, size_t band, double gainDb) override {
        m_count("audio : égaliseur");
        return AudioEngine::SetEqBand(elementId, band, gainDb);
    }

    // 8 s of a 440 Hz tone with a click every 0.5 s (120 BPM), at 48 kHz, then one delivery of the newest frame.
    void PlaySyntheticAudio() {
        if (!AnalysisWanted()) return;
        constexpr double kRate = 48000;
        constexpr size_t kBlock = 480;
        std::vector<float> block(2 * kBlock);
        for (size_t start = 0; start < static_cast<size_t>(8 * kRate); start += kBlock) {
            for (size_t i = 0; i < kBlock; i++) {
                size_t n = start + i;
                double tone = 0.25 * std::sin(2 * std::numbers::pi * 440.0 * static_cast<double>(n) / kRate);
                double click = (n % 24000) < 240 ? 0.5 : 0.0;
                block[2 * i] = block[2 * i + 1] = static_cast<float>(tone + click);
            }
            FeedAnalysis(block.data(), kBlock, 2, kRate);
        }
        DeliverPending();
    }

private:
    std::function<void(const std::string&)> m_count;
};

// The host of a test run. Settings and storage live in memory. Every other call answers with a fixed value, and each
// call is counted, so the report says what the element used. It offers the optional services too (POST, Discord,
// icon menu, MIDI, remote control, output files), so that an element using them runs; nothing leaves the process.
// Those services still answer "off" until their setting is on, as in the kernel, so the report shows that line.
class TestHost : public Host,
                 public HttpPostHost,
                 public DiscordHost,
                 public TrayHost,
                 public MidiHost,
                 public RemoteHost,
                 public OutputFilesHost {
public:
    std::string Process() const override { return "ui"; }
    std::string Locale() const override { return "fr-FR"; }

    std::optional<std::string> ReadSetting(const std::string& key) override {
        Count("réglages (lecture)");
        auto found = m_settings.find(key);
        if (found == m_settings.end()) return std::nullopt;
        return found->second;
    }
    void WriteSetting(const std::string& key, const std::string& value) override {
        Count("réglages (écriture)");
        m_settings[key] = value;
    }
    std::optional<std::string> ReadStorage(const std::string& elementId) override {
        Count("stockage (lecture)");
        auto found = m_storage.find(elementId);
        if (found == m_storage.end()) return std::nullopt;
        return found->second;
    }
    bool WriteStorage(const std::string& elementId, const std::string& json) override {
        Count("stockage (écriture)");
        m_storage[elementId] = json;
        return true;
    }
    PlayerSnapshot Player() override {
        Count("lecteur (lecture)");
        return {};
    }
    bool Control(PlayerCommand, int64_t) override {
        Count("lecteur (commande)");
        return true;
    }
    void SetGainDb(const std::string&, double) override { Count("gain audio"); }
    void Notify(const std::string&, const std::string&, const std::string&) override { Count("notification"); }
    bool RegisterKey(const std::string&, const std::string&) override {
        Count("raccourci");
        return true;
    }
    void UnregisterKeys(const std::string&) override {}
    HttpResponse HttpGet(const std::string& url) override {
        Count("réseau : " + url);
        return HttpResponse{200, "{}"};
    }
    HttpResponse HttpPost(const std::string& url, const std::string&, const HttpHeaders&) override {
        Count("réseau (envoi) : " + url);
        return HttpResponse{200, "{}"};
    }
    bool SetActivity(const std::string&, const std::string&, const discord::Activity&) override {
        Count("discord (présence)");
        return true;
    }
    void ClearActivity(const std::string&) override {}
    bool SetTrayMenu(const std::string&, const std::vector<TrayItem>&) override {
        Count("icône de notification (menu)");
        return true;
    }
    void RemoveTrayMenu(const std::string&) override {}
    bool SubscribeMidi(const std::string&) override {
        Count("midi");
        return true;
    }
    void UnsubscribeMidi(const std::string&) override {}
    std::vector<std::string> MidiDevices() override { return {}; }
    bool PublishRemoteState(const std::string&, const std::string&) override {
        Count("télécommande (état)");
        return true;
    }
    void RemoveRemoteState(const std::string&) override {}
    bool WriteOutputText(const std::string&, const std::string& name, const std::string&, std::string*) override {
        Count("fichier de sortie : " + name);  // counted, not written
        return true;
    }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<HttpPostHost>(name, this)) return found;
        if (void* found = Offer<DiscordHost>(name, this)) return found;
        if (void* found = Offer<TrayHost>(name, this)) return found;
        if (void* found = Offer<MidiHost>(name, this)) return found;
        if (void* found = Offer<RemoteHost>(name, this)) return found;
        if (void* found = Offer<OutputFilesHost>(name, this)) return found;
        if (name == AudioHost::kCapabilityName) return static_cast<AudioHost*>(&m_audio);
        return Host::QueryCapability(name);
    }

    // Interface calls land in an in-memory model, the same one the kernel keeps. Nothing is drawn.
    UiModel& Ui() override {
        Count("interface");
        return m_ui;
    }

    void PlaySyntheticAudio() { m_audio.PlaySyntheticAudio(); }

    const std::map<std::string, int>& Calls() const { return m_calls; }

private:
    void Count(const std::string& what) { m_calls[what]++; }

    std::map<std::string, std::string> m_settings;
    std::map<std::string, std::string> m_storage;
    std::map<std::string, int> m_calls;
    UiModel m_ui;
    TestAudio m_audio{[this](const std::string& what) { Count(what); }};
};

}  // namespace

int CmdTest(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "test attend le dossier de l'élément");

    std::filesystem::path folder = PathArgument(args[1]);
    FolderValidation validation = ValidateElementFolder(folder);
    if (!validation.ok() || !validation.errors.empty() || !validation.manifest) {
        ctx.err << Utf8(folder) << " : invalide\n";
        for (const std::string& problem : validation.errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    const ElementManifest& manifest = *validation.manifest;

    // The engine checks the manifest again with its own rules, as it does at load time.
    std::string text;
    if (!ReadWholeFile(folder / "manifest.json", text)) return Failure(ctx, "manifest.json illisible");
    std::error_code ec;
    ManifestResult parsed = amk::ParseManifest(text, FolderName(folder), kKitApiMajor,
                                               [&](const std::string& file) {
                                                   return std::filesystem::is_regular_file(folder / file, ec);
                                               });
    if (!parsed.manifest) {
        ctx.err << "le moteur refuse le manifeste :\n";
        for (const std::string& problem : parsed.errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    for (const std::string& warning : parsed.warnings) ctx.out << "  attention  " << warning << "\n";

    std::ostringstream journalText;
    Journal journal(journalText, Journal::LocalTimeStamp);
    TestHost host;
    ScriptExtension element(*parsed.manifest, folder, host, journal);
    bool started = element.Start();
    if (started) {
        host.PlaySyntheticAudio();  // only when the element asked for analysis frames or the tempo
        element.Pump();
    }
    element.Stop();

    ctx.out << "amk test : " << manifest.id << " (" << manifest.kind << " " << manifest.version << ")\n";
    ctx.out << "  permissions  ";
    if (manifest.permissions.empty()) ctx.out << "aucune";
    for (size_t i = 0; i < manifest.permissions.size(); i++) ctx.out << (i ? ", " : "") << manifest.permissions[i];
    ctx.out << "\n  main.js      " << (started ? "chargé et exécuté sans erreur" : "échec : " + element.lastError())
            << "\n";

    std::istringstream lines(journalText.str());
    for (std::string line; std::getline(lines, line);) ctx.out << "  journal      " << line << "\n";
    for (const auto& [what, count] : host.Calls()) ctx.out << "  appel        " << what << " x" << count << "\n";

    if (!started) return 1;
    ctx.out << "Aucune erreur. Les appels ci-dessus sont ceux que le moteur a faits pour l'élément, avec un hôte factice.\n";
    return 0;
}

}  // namespace amk::cli
