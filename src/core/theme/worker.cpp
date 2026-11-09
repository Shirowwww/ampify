#include "worker.hpp"

#include <vector>

#include "accent.hpp"
#include "files.hpp"
#include "glass.hpp"
#include "layout.hpp"
#include "library.hpp"
#include "snippets.hpp"
#include "theme_settings.hpp"
#include "../settings_catalog.hpp"
#include "../text_util.hpp"

namespace ampify::theme {

namespace {

// sidebar.json and home.json as one set of rules; null when neither file exists.
std::shared_ptr<const Theme> LoadLayout(const std::optional<std::string>& sidebar, const std::optional<std::string>& home) {
    if (!sidebar && !home) return nullptr;
    auto layout = std::make_shared<Theme>();
    layout->id = "layout";
    layout->name = "layout";
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

// True when the user.css of this folder uses the artwork's colours: var(--art-1) and the others, or those of the page
// on screen, var(--page-art-1)...
bool NamesArtwork(const std::filesystem::path& folder) {
    std::optional<std::string> css = ReadWholeFile(folder / "user.css");
    return css && (css->find("--art-") != std::string::npos || css->find("--page-art-") != std::string::npos);
}

}  // namespace

std::optional<std::set<std::string>> ParseAllowedKeys(const std::optional<std::string>& text) {
    if (!text) return std::nullopt;
    std::set<std::string> keys;
    size_t start = 0;
    while (start < text->size()) {
        size_t end = text->find('\n', start);
        if (end == std::string::npos) end = text->size();
        std::string line = text->substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') keys.insert(line);
    }
    return keys;
}

ThemeWorker::ThemeWorker(std::filesystem::path kit) : m_kit(std::move(kit)) {}

LoadInputs ThemeWorker::ReadInputs(const Settings& settings, const ThemeSettings& wanted, int minute) {
    const std::filesystem::path themeFolder = m_kit / "themes" / wanted.active;
    std::vector<std::string> schemes;
    std::string defaultScheme;
    if (!wanted.active.empty()) {
        FolderFiles files(themeFolder);
        schemes = ReadSchemes(files);
        defaultScheme = ReadDefaultScheme(files);
    }
    m_scheme = ChooseScheme(wanted.scheme, wanted.schedule, schemes, {m_systemDark, minute}, defaultScheme);

    LoadInputs inputs;
    inputs.active = wanted.active;
    inputs.scheme = m_scheme.scheme;
    inputs.parts = wanted.parts;
    // theme.accent = artwork: the colour follows the song; the theme reloads only when that colour changes.
    inputs.accent = IsArtworkAccent(wanted.accent) ? ArtworkAccent(m_artwork) : ParseAccent(wanted.accent);
    inputs.glass = GlassLevelOf(wanted.glass);
    inputs.values = ReadThemeValues(settings, wanted.active);
    inputs.playbar = ChoiceOf(settings, kPlaybarKey);
    const std::filesystem::path snippetRoot = m_kit / "snippets";
    bool usesArtwork = !wanted.active.empty() && NamesArtwork(themeFolder);
    for (const std::string& id : EnabledSnippets(settings, ListSnippetFolders(snippetRoot))) {
        inputs.snippets.push_back({id, ReadSnippetValues(settings, id)});
        usesArtwork = usesArtwork || NamesArtwork(snippetRoot / id);
    }
    if (usesArtwork) inputs.artwork = m_artwork;
    inputs.sidebar = ReadWholeFile(m_kit / LayoutFileName(LayoutKind::Sidebar));
    inputs.home = ReadWholeFile(m_kit / LayoutFileName(LayoutKind::Home));
    inputs.allowedKeys = ParseAllowedKeys(ReadWholeFile(m_kit / "resource-keys.txt"));
    if (wanted.liveReload) {
        if (!wanted.active.empty()) inputs.signatures.push_back(FolderSignature(themeFolder));
        for (const LoadInputs::Snippet& snippet : inputs.snippets) {
            inputs.signatures.push_back(FolderSignature(snippetRoot / snippet.id));
        }
    }
    return inputs;
}

SessionRequest ThemeWorker::Load(const LoadInputs& inputs) const {
    SessionRequest request;
    request.generation = m_request.generation + 1;
    request.themeId = inputs.active;
    request.schemeReason = m_scheme.reason;
    request.glass = inputs.glass;

    LoadOptions options;
    options.scheme = inputs.scheme;
    options.parts = inputs.parts;
    options.allowedKeys = inputs.allowedKeys ? &*inputs.allowedKeys : nullptr;
    options.settings = inputs.values;
    options.artwork = inputs.artwork;
    options.accent = inputs.accent;
    options.glass = inputs.glass;
    options.playbar = inputs.playbar;

    if (!inputs.active.empty()) {
        const std::filesystem::path folder = m_kit / "themes" / inputs.active;
        FolderFiles files(folder);
        Theme theme = LoadTheme(inputs.active, folder.generic_string(), files, options);
        for (const std::string& problem : m_scheme.problems) theme.problems.push_back(problem);
        request.theme = std::make_shared<const Theme>(std::move(theme));
    } else if (inputs.accent) {
        // No theme, but an accent: Apple Music's look in that accent.
        request.themeId = kAccentOnlyId;
        request.theme = std::make_shared<const Theme>(LoadAccentOnly(options));
    }

    const Theme* base = request.theme && request.theme->usable ? request.theme.get() : nullptr;
    if (std::optional<Theme> layout = LoadPlaybarLayout(base, options)) {
        request.snippets.push_back(std::make_shared<const Theme>(std::move(*layout)));
    }
    for (const LoadInputs::Snippet& snippet : inputs.snippets) {
        const std::filesystem::path folder = m_kit / "snippets" / snippet.id;
        FolderFiles files(folder);
        LoadOptions snippetOptions = options;
        snippetOptions.settings = snippet.values;
        Theme loaded = LoadSnippet(snippet.id, folder.generic_string(), files, base, snippetOptions);
        request.snippets.push_back(std::make_shared<const Theme>(std::move(loaded)));
    }
    request.layout = LoadLayout(inputs.sidebar, inputs.home);
    return request;
}

SessionRequest ThemeWorker::Tick(const Settings& settings, int minute) {
    ThemeSettings wanted = ReadThemeSettings(settings);
    LoadInputs inputs = ReadInputs(settings, wanted, minute);
    if (!m_loaded || inputs != *m_loaded) {
        m_request = Load(inputs);
        m_loaded = std::move(inputs);
    }
    m_request.inspect = wanted.inspect;
    return m_request;
}

}  // namespace ampify::theme
