#include "snippets.hpp"

#include <algorithm>
#include <system_error>

#include "../element_ref.hpp"
#include "library.hpp"

namespace ampify::theme {

namespace {

// The text of a dictionary without its XML declaration, which may only appear at the start of a document.
std::string WithoutDeclaration(const std::string& xaml) {
    size_t start = xaml.find("<?xml");
    if (start == std::string::npos) return xaml;
    size_t end = xaml.find("?>", start);
    if (end == std::string::npos) return xaml;
    return xaml.substr(0, start) + xaml.substr(end + 2);
}

void AddLayer(const Theme& source, int layer, Theme& composed) {
    for (Assignment assignment : source.assignments) {
        assignment.layer += layer;  // keeps the theme's own -1 layer (color.ini translated) below its user.css
        composed.assignments.push_back(std::move(assignment));
    }
}

}  // namespace

std::vector<std::string> ListSnippetFolders(const std::filesystem::path& root) {
    return ListFoldersHolding(root, kSnippetFiles);
}

std::vector<std::string> EnabledSnippets(const Settings& settings, const std::vector<std::string>& installed) {
    std::vector<std::string> enabled;
    for (const std::string& id : installed) {
        if (IsElementOn(settings, {ElementKind::Snippet, id})) enabled.push_back(id);
    }
    std::sort(enabled.begin(), enabled.end());
    return enabled;
}

std::string CombineDictionaries(const std::vector<std::string>& dictionaries) {
    std::vector<const std::string*> kept;
    for (const std::string& xaml : dictionaries) {
        if (!xaml.empty() && !IsEmptyDictionary(xaml)) kept.push_back(&xaml);
    }
    if (kept.empty()) return {};
    if (kept.size() == 1) return *kept.front();

    std::string combined =
        "<ResourceDictionary xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n"
        "                    xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\">\n"
        "  <ResourceDictionary.MergedDictionaries>\n";
    for (const std::string* xaml : kept) combined += WithoutDeclaration(*xaml) + "\n";
    combined += "  </ResourceDictionary.MergedDictionaries>\n</ResourceDictionary>\n";
    return combined;
}

Theme ComposeTheme(const Theme* theme, const std::vector<std::shared_ptr<const Theme>>& snippets, const Theme* layout) {
    // The theme as a whole, so a field it gains is composed too; its problems stay its own.
    const bool hasTheme = theme && theme->usable;
    Theme composed = hasTheme ? *theme : Theme{};
    composed.usable = true;
    composed.problems.clear();
    std::vector<std::string> dictionaries;
    std::vector<std::string> names;
    if (hasTheme) dictionaries.push_back(theme->resourceDictionary);

    int layer = 1;
    for (const std::shared_ptr<const Theme>& snippet : snippets) {
        if (!snippet || !snippet->usable) continue;
        names.push_back(snippet->id);
        dictionaries.push_back(snippet->resourceDictionary);
        AddLayer(*snippet, layer++, composed);
        for (const auto& [role, value] : snippet->fonts) {
            FontRole& merged = composed.fonts[role];
            if (!value.family.empty()) merged.family = value.family;
            if (!value.weight.empty()) merged.weight = value.weight;
            if (!value.size.empty()) merged.size = value.size;
        }
    }

    if (layout) AddLayer(*layout, layer, composed);

    composed.resourceDictionary = CombineDictionaries(dictionaries);
    if (composed.name.empty()) composed.name = names.empty() ? std::string("layout") : std::string("snippets");
    if (!names.empty()) {
        composed.name += " + ";
        for (size_t i = 0; i < names.size(); i++) composed.name += (i ? ", " : "") + names[i];
    }
    return composed;
}

}  // namespace ampify::theme
