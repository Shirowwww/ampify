// `amk test`: runs an element's main.js in the kernel's JavaScript engine, with a host that records what the element
// asks for. Nothing reaches Apple Music, the network or the disk outside the test: the host answers in memory.

#include <map>
#include <sstream>

#include "../core/host.hpp"
#include "../core/journal.hpp"
#include "../core/manifest.hpp"
#include "../core/script_extension.hpp"
#include "archive.hpp"
#include "internal.hpp"

namespace amk::cli {

namespace {

// The host of a test run. Settings and storage live in memory. Every other call answers with a fixed value, and each
// call is counted, so the report says what the element used.
class TestHost : public Host {
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

    const std::map<std::string, int>& Calls() const { return m_calls; }

private:
    void Count(const std::string& what) { m_calls[what]++; }

    std::map<std::string, std::string> m_settings;
    std::map<std::string, std::string> m_storage;
    std::map<std::string, int> m_calls;
};

}  // namespace

int CmdTest(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "test attend le dossier de l'élément");

    std::filesystem::path folder = PathArgument(args[1]);
    FolderValidation validation = ValidateElementFolder(folder);
    if (ContainsLink(folder)) validation.errors.push_back("lien symbolique interdit dans l'élément");
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
    if (started) element.Pump();
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
