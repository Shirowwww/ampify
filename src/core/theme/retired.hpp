#pragma once

// The elements Ampify retired or merged into another, and where settings.txt moves their keys. The only place that
// moves keys between versions: a retired element gets one row in kRetired (retired.cpp), in this format.
//
// Ampify 1.3.0 also placed the playback bar for every theme (apple.playbar): the Bottom playback bar snippet and the
// themes' own bar-position settings went, and their value moves to apple.playbar (MoveRetiredSettings).

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../element_ref.hpp"
#include "../settings.hpp"

namespace ampify::theme {

// One setting of a merged element, carried to the element that took its place: <from> of the old one becomes <to> of
// the new one. With `fromValue`, only that value moves (in any case), written as `toValue`; with `atMost`, a number
// above it is written as it. With `whenNew`, the row applies only when the new one was not in use before the move: it
// turns off what the new one does by default and the old one did not, and leaves the choices of someone who already
// used both.
struct CarriedSetting {
    std::string_view from;
    std::string_view to;
    std::string_view fromValue = {};
    std::string_view toValue = {};
    std::string_view atMost = {};
    bool whenNew = false;
};

struct RetiredElement {
    ElementKind kind = ElementKind::Snippet;  // a theme or a snippet
    std::string_view id;
    // Merged: the element of the same kind that took its place. While the old one was in use (the active theme, a
    // snippet switched on), the new one is, with the settings listed. Empty: retired, and an active theme gives way to
    // no theme.
    std::string_view into = {};
    std::span<const CarriedSetting> settings = {};
};

// Every retired or merged element, oldest first.
std::span<const RetiredElement> RetiredElements();

// Whether an element is in the kit, its folder and manifest there. The kernel asks its kit; the tests give a list.
using InstalledCheck = std::function<bool(const ElementRef&)>;

// The merged elements in use (the active theme, a snippet switched on) whose replacement is not installed: they keep
// their keys and their folder until it is, so a start without the catalog leaves the look as it was. The `waiting`
// spans below take these pointers, rows of RetiredElements(), and match them by address.
std::vector<const RetiredElement*> AwaitingReplacement(const Settings& settings, const InstalledCheck& installed);

// The replacements to fetch from the catalog, each once: those of the elements awaiting them, and those switched on
// but missing (moved by 1.3.0, which did not install them).
std::vector<ElementRef> ReplacementsToInstall(const Settings& settings, const InstalledCheck& installed);

// Moves the keys of retired elements and settings to what replaced them, and removes them, except for the elements of
// `waiting`. True when it changed anything: the kernel then writes the file once.
bool MoveRetiredSettings(Settings& settings, std::span<const RetiredElement* const> waiting = {});

// True for an element of kRetired: the Marketplace neither lists nor installs it.
bool IsRetired(ElementKind kind, std::string_view id);

// The folders of the retired elements, relative to the kit (snippets/bottom-playbar...), except those of `waiting`. Only
// Ampify's own copies go: a folder whose manifest has another author stays.
std::vector<std::string> RetiredFolders(std::span<const RetiredElement* const> waiting = {});

}  // namespace ampify::theme
