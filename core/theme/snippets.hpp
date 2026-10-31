#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../settings.hpp"
#include "theme.hpp"

namespace ampify::theme {

// Snippets: small pieces of style that the user switches on one by one, as in Spicetify. A snippet is a folder of
// `snippets\` in the Ampify folder (manifest kind "snippet") with resources.xaml, user.css, or both. It is loaded with
// LoadSnippet (theme.hpp) and applied over the active theme, or alone when there is no theme.

// The snippet folders under `root`, sorted by name: the folders that hold resources.xaml or user.css.
std::vector<std::string> ListSnippetFolders(const std::filesystem::path& root);

// The ids of `installed` whose setting snippets.<id>.enabled is on (as `ampify snippet enable` writes it), sorted by id.
std::vector<std::string> EnabledSnippets(const Settings& settings, const std::vector<std::string>& installed);

// One dictionary holding several: each one becomes a merged dictionary, in order. XAML looks up the merged dictionaries
// from the last one to the first, so a later dictionary (a snippet) wins over an earlier one (the theme). Empty
// dictionaries are left out. A single dictionary comes back as it is; none gives an empty text.
std::string CombineDictionaries(const std::vector<std::string>& dictionaries);

// What the session applies: the theme (may be null or unusable, then left out), then each usable snippet in the
// given order, then the layout rules of sidebar.json and home.json (may be null). Each source gets its own layer, so
// a snippet beats the theme and the layout beats both (see Beats in engine.cpp). Colours come from the theme only.
// The font roles are merged, a snippet's role parts beating the theme's. Problems are not copied: each source keeps its own.
Theme ComposeTheme(const Theme* theme, const std::vector<std::shared_ptr<const Theme>>& snippets, const Theme* layout);

}  // namespace ampify::theme
