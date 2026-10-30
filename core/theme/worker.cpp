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

namespace amk::theme {

namespace {

std::optional<std::string> ReadText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

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
    if (!wanted.active.empty()) {
        FolderFiles files(themeFolder);
        schemes = ReadSchemes(files);
    }
    m_scheme = ChooseScheme(wanted.scheme, wanted.schedule, schemes, {m_systemDark, minute});

    const std::filesystem::path snippetRoot = m_kit / "snippets";
    std::vector<std::string> snippets = EnabledSnippets(settings, ListSnippetFolders(snippetRoot));
    std::optional<std::string> sidebar = ReadText(m_kit / LayoutFileName(LayoutKind::Sidebar));
    std::optional<std::string> home = ReadText(m_kit / LayoutFileName(LayoutKind::Home));
    std::optional<std::string> keysText = ReadText(m_kit / "resource-keys.txt");

    // Everything that changes what is applied goes into the key. A new key is a new generation.
    std::string key = (wanted.active.empty() ? std::string("none") : wanted.active) + "|" + m_scheme.scheme + "|" +
                      PartsKey(wanted.parts) + "|";
    for (const std::string& id : snippets) key += id + ",";
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

        if (!wanted.active.empty()) {
            FolderFiles files(themeFolder);
            Theme theme = LoadTheme(wanted.active, themeFolder.generic_string(), files, options);
            for (const std::string& problem : m_scheme.problems) theme.problems.push_back(problem);
            request.theme = std::make_shared<const Theme>(std::move(theme));
        }

        const Theme* base = request.theme && request.theme->usable ? request.theme.get() : nullptr;
        for (const std::string& id : snippets) {
            FolderFiles files(snippetRoot / id);
            request.snippets.push_back(
                std::make_shared<const Theme>(LoadSnippet(id, (snippetRoot / id).generic_string(), files, base, options)));
        }
        request.layout = LoadLayout(sidebar, home);
        m_request = std::move(request);
    }

    m_request.inspect = wanted.inspect;
    return m_request;
}

}  // namespace amk::theme
