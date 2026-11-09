#pragma once

// The vocabulary of Ampify.ui in one place: the primitives and what each one takes, the placements with their
// permission, and the limits of a tree. src/core/ui_model.cpp validates from it, the natives clip their texts with it,
// and the JavaScript prelude reads it back (VocabularyJson) instead of keeping copies. Portable, no renderer.

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace ampify::uivocab {

// Limits of a tree sent by an element. The validation messages are written from these.
inline constexpr int kMaxTreeDepth = 8;
inline constexpr int kMaxTreeNodes = 300;
inline constexpr std::size_t kMaxText = 500;  // bytes of one text attribute
inline constexpr double kMaxTooltipDelayMs = 10000;
inline constexpr double kMaxLength = 10000;  // width, height, minWidth, maxWidth, in pixels
inline constexpr double kMaxCorner = 1000;
inline constexpr double kMaxScale = 10;
inline constexpr double kMinTextSize = 8;
inline constexpr double kMaxTextSize = 120;
inline constexpr double kMaxLines = 100;
inline constexpr double kMaxPadding = 1000;  // each side
inline constexpr double kMaxTransitionMs = 10000;
inline constexpr int kMaxGridColumns = 12;
inline constexpr double kMaxGridGap = 1000;  // the space between the cells of a grid, in pixels
inline constexpr int kMaxSubmenuEntries = 20;

// Limits of the texts the natives take, in bytes: each is clipped at a character boundary (ClipUtf8).
inline constexpr std::size_t kMaxControlLabel = 60;
inline constexpr std::size_t kMaxControlIcon = 8000;
inline constexpr std::size_t kMaxTitle = 120;
inline constexpr std::size_t kMaxToastTitle = 120;
inline constexpr std::size_t kMaxToastText = 500;

// The style attributes of any node, and those of a box (stack, row, layer, card).
inline constexpr std::array<std::string_view, 7> kCommonStyle = {"opacity", "scale",  "transition", "align",
                                                                 "valign",  "grow",   "inert"};
inline constexpr std::array<std::string_view, 7> kBoxStyle = {"background", "padding",   "cornerRadius", "width",
                                                              "height",     "minWidth",  "maxWidth"};

// Which style attributes beyond the common ones a primitive reads.
enum class StyleKind {
    Plain,  // none
    Box,    // background, padding, cornerRadius, sizes
    Image,  // fit, cornerRadius, width, height
    Text,   // size, weight, color, wrap, maxLines
    Grid,   // gap, columns
};

struct Primitive {
    std::string_view name;
    bool children = false;  // may hold child nodes
    bool click = false;     // takes onClick
    bool change = false;    // takes onChange
    StyleKind style = StyleKind::Plain;
    bool textAlign = false;                // align means left, center or right instead of start, center, end, stretch
    std::string_view onlyChild;            // the one type of node it holds, if it holds one type only
    std::string_view container;            // the one parent it can sit in, if it has one
    std::string_view textAttribute;        // where the text children of a primitive without children go (React)
    double gap = -1;                       // spacing between children when the tree gives no `gap`; -1: none
};

std::span<const Primitive> Primitives();
const Primitive* FindPrimitive(std::string_view name);

// Where a placement shows: the top bar or the play bar, the sidebar, or a menu.
enum class Slot { Bar, Sidebar, Menu };

struct Placement {
    std::string_view name;
    std::string_view permission;
    Slot slot;
};

std::span<const Placement> Placements();
const Placement* FindPlacement(std::string_view name);

// The default spacing of a primitive's children, or `fallback` when it has none.
double DefaultGap(std::string_view primitive, double fallback);

// The lists the JavaScript prelude needs, as one JSON object: containers, place, box, textAttribute and
// maxSubmenuEntries.
std::string VocabularyJson();

}  // namespace ampify::uivocab
