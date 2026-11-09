#pragma once

// The elements Ampify retired or merged into another, and where settings.txt moves their keys. The only place that
// moves keys between versions: a retired element gets one row in kRetired (retired.cpp), in this format.
//
// Ampify 1.3.0 also placed the playback bar for every theme (apple.playbar): the Bottom playback bar snippet and the
// themes' own bar-position settings went, and their value moves to apple.playbar (MoveRetiredSettings).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../element_ref.hpp"
#include "../settings.hpp"

namespace ampify::theme {

// One setting of a merged element, carried to the element that took its place: <from> of the old one becomes <to> of
// the new one. With `fromValue`, only that value moves (in any case), written as `toValue`; with `atMost`, a number
// above it is written as it.
struct CarriedSetting {
    std::string_view from;
    std::string_view to;
    std::string_view fromValue = {};
    std::string_view toValue = {};
    std::string_view atMost = {};
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

// Moves the keys of retired elements and settings to what replaced them, and removes them. True when it changed
// anything: the kernel then writes the file once.
bool MoveRetiredSettings(Settings& settings);

// True for an element of kRetired: the Marketplace neither lists nor installs it.
bool IsRetired(ElementKind kind, std::string_view id);

// The folders of the retired elements, relative to the kit (snippets/bottom-playbar...). Only Ampify's own copies go:
// a folder whose manifest has another author stays.
std::vector<std::string> RetiredFolders();

}  // namespace ampify::theme
