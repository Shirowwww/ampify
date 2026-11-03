#include "worker.hpp"

#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

#include "files.hpp"
#include "layout.hpp"
#include "library.hpp"
#include "snippets.hpp"
#include "theme_settings.hpp"
#include "../text_util.hpp"

namespace ampify::theme {

namespace {

std::string PartsKey(const ThemeParts& parts) {
    return std::string(parts.colors ? "c" : "-") + (parts.resources ? "r" : "-") + (parts.assets ? "a" : "-");
}

// sidebar.json and home.json as one set of rules; null when neither file exists.
std::shared_ptr<const Theme> LoadLayout(const std::optional<std::string>& sidebar, const std::optional<std::string>& home) {
    if (!sidebar && !home) return nullptr;
    auto layout = std::make_shared<Theme>();
    layout->id = "disposition";
    layout->name = "disposition";
    layout->usable = true;
    for (auto [text, kind] : {std::pair{&sidebar, LayoutKind::Sidebar}, std::pair{&home, LayoutKind::Home}}) {
        if (!*text) continue;
        std::vector<std::string> problems;
        std::optional<LayoutConfig> config = ParseLayout(**text, problems);
        for (const std::string& problem : problems) layout->problems.push_back(std::string(LayoutFileName(kind)) + " : " + problem);
        if (config) AddLayoutRules(*config, kind, *layout);
    }
    return layout;
}

// True when the user.css of this folder uses the artwork's colours (var(--art-1) and the others).
bool NamesArtwork(const std::filesystem::path& folder) {
    std::optional<std::string> css = ReadWholeFile(folder / "user.css");
    return css && css->find("--art-") != std::string::npos;
}

}  // namespace

std::optional<std::set<std::string>> ReadAllowedKeys(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;

    std::set<std::string> keys;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') keys.insert(line);
    }
    return keys;
}

ThemeWorker::ThemeWorker(std::filesystem::path kit) : m_kit(std::move(kit)) {}

SessionRequest ThemeWorker::Tick(const Settings& settings, int minute) {
    ThemeSettings wanted = ReadThemeSettings(settings);
    const std::filesystem::path themeFolder = m_kit / "themes" / wanted.active;

    std::vector<std::string> schemes;
    std::string defaultScheme;
    if (!wanted.active.empty()) {
        FolderFiles files(themeFolder);
        schemes = ReadSchemes(files);
        defaultScheme = ReadDefaultScheme(files);
    }
    m_scheme = ChooseScheme(wanted.scheme, wanted.schedule, schemes, {m_systemDark, minute}, defaultScheme);

    const std::filesystem::path snippetRoot = m_kit / "snippets";
    std::vector<std::string> snippets = EnabledSnippets(settings, ListSnippetFolders(snippetRoot));
    std::optional<std::string> sidebar = ReadWholeFile(m_kit / LayoutFileName(LayoutKind::Sidebar));
    std::optional<std::string> home = ReadWholeFile(m_kit / LayoutFileName(LayoutKind::Home));
    std::optional<std::string> keysText = ReadWholeFile(m_kit / "resource-keys.txt");

    // Everything that changes what is applied goes into the key. A new key is a new generation.
    std::map<std::string, std::string> values = ReadThemeValues(settings, wanted.active);
    std::optional<Argb> accent = ParseAccent(wanted.accent);
    std::string key = (wanted.active.empty() ? std::string("none") : wanted.active) + "|" + m_scheme.scheme + "|" +
                      PartsKey(wanted.parts) + "|" + (accent ? ToXamlColor(*accent) : std::string("-")) + "|";
    for (const auto& [name, value] : values) key += name + "=" + value + ",";
    key += "|";
    std::map<std::string, std::map<std::string, std::string>> snippetValues;
    for (const std::string& id : snippets) {
        snippetValues[id] = ReadSnippetValues(settings, id);
        key += id + "(";
        for (const auto& [name, value] : snippetValues[id]) key += name + "=" + value + ",";
        key += "),";
    }
    bool usesArtwork = !wanted.active.empty() && NamesArtwork(themeFolder);
    for (const std::string& id : snippets) usesArtwork = usesArtwork || NamesArtwork(snippetRoot / id);
    if (usesArtwork) {
        for (const auto& [name, value] : m_artwork) key += "|" + name + "=" + value;
    }
    key += "|" + sidebar.value_or("-") + "|" + home.value_or("-") + "|" +
           (keysText ? std::to_string(std::hash<std::string>{}(*keysText)) : std::string("-"));
    if (wanted.liveReload) {
        if (!wanted.active.empty()) key += "|" + FolderSignature(themeFolder);
        for (const std::string& id : snippets) key += "|" + FolderSignature(snippetRoot / id);
    }

    if (!m_hasKey || key != m_lastKey) {
        m_hasKey = true;
        m_lastKey = key;

        SessionRequest request;
        request.generation = m_request.generation + 1;
        request.themeId = wanted.active;
        request.schemeReason = m_scheme.reason;

        std::optional<std::set<std::string>> allowed = ReadAllowedKeys(m_kit / "resource-keys.txt");
        LoadOptions options;
        options.scheme = m_scheme.scheme;
        options.parts = wanted.parts;
        options.allowedKeys = allowed ? &*allowed : nullptr;
        options.settings = values;
        options.artwork = usesArtwork ? m_artwork : std::map<std::string, std::string>{};
        options.accent = accent;

        if (!wanted.active.empty()) {
            FolderFiles files(themeFolder);
            Theme theme = LoadTheme(wanted.active, themeFolder.generic_string(), files, options);
            for (const std::string& problem : m_scheme.problems) theme.problems.push_back(problem);
            request.theme = std::make_shared<const Theme>(std::move(theme));
        } else if (accent) {
            // No theme, but an accent: Apple Music's look in that accent.
            request.themeId = kAccentOnlyId;
            request.theme = std::make_shared<const Theme>(LoadAccentOnly(options));
        }

        const Theme* base = request.theme && request.theme->usable ? request.theme.get() : nullptr;
        for (const std::string& id : snippets) {
            FolderFiles files(snippetRoot / id);
            LoadOptions snippetOptions = options;
            snippetOptions.settings = snippetValues[id];
            request.snippets.push_back(std::make_shared<const Theme>(
                LoadSnippet(id, (snippetRoot / id).generic_string(), files, base, snippetOptions)));
        }
        request.layout = LoadLayout(sidebar, home);
        m_request = std::move(request);
    }

    m_request.inspect = wanted.inspect;
    return m_request;
}

}  // namespace ampify::theme
