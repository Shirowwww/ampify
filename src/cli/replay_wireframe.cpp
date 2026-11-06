#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "replay_util.hpp"

namespace ampify::theme::replay {

namespace {

std::string Escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

// An SVG colour and its opacity from a shown value: a colour, a gradient's first stop, an acrylic's tint. Nothing for
// an image or an object the dump could not express (Apple's own brushes read "present").
std::optional<std::pair<std::string, double>> Paint(const std::optional<Value>& value) {
    if (!value) return std::nullopt;
    std::string text;
    if (value->kind == ValueKind::Color) text = value->text;
    else if (value->kind == ValueKind::Acrylic) text = value->text.substr(0, 9);
    else if (value->kind == ValueKind::Gradient) {
        size_t hash = value->text.find('#');
        if (hash != std::string::npos) text = value->text.substr(hash, 9);
    }
    if (text.size() != 9 || text[0] != '#') return std::nullopt;
    const double alpha = std::strtol(text.substr(1, 2).c_str(), nullptr, 16) / 255.0;
    if (alpha <= 0) return std::nullopt;
    return std::make_pair("#" + text.substr(3), alpha);
}

double FirstNumber(const std::optional<Value>& value, double fallback) {
    if (!value) return fallback;
    double number = std::atof(value->text.c_str());
    return std::isfinite(number) ? number : fallback;
}

std::string Fmt(double number) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", number);
    return buffer;
}

bool IsText(const ReplayBackend::Node& node) {
    return node.type == "TextBlock" || node.type == "FontIcon" || node.type == "SymbolIcon" || node.type == "Run";
}

std::string BoxShape(ReplayBackend& tree, int index, const NodeBox& box, double opacity) {
    const ReplayBackend::Node& node = tree.Nodes()[index];
    std::string shape;
    std::string tip = "<title>" + Escape(Label(node)) + " [" + std::to_string(index) + "]</title>";
    std::optional<std::pair<std::string, double>> fill = Paint(tree.Shown(index, "Background"));
    std::optional<std::pair<std::string, double>> border = Paint(tree.Shown(index, "BorderBrush"));
    const double thickness = FirstNumber(tree.Shown(index, "BorderThickness"), 0);
    const double radius = FirstNumber(tree.Shown(index, "CornerRadius"), 0);
    if (fill || (border && thickness > 0)) {
        shape += "<rect x=\"" + Fmt(box.x) + "\" y=\"" + Fmt(box.y) + "\" width=\"" + Fmt(box.width) + "\" height=\"" +
                 Fmt(box.height) + "\" rx=\"" + Fmt(radius) + "\"";
        shape += fill ? " fill=\"" + fill->first + "\" fill-opacity=\"" + Fmt(fill->second * opacity) + "\""
                      : std::string(" fill=\"none\"");
        if (border && thickness > 0) {
            shape += " stroke=\"" + border->first + "\" stroke-opacity=\"" + Fmt(border->second * opacity) +
                     "\" stroke-width=\"" + Fmt(thickness) + "\"";
        }
        shape += ">" + tip + "</rect>\n";
    }
    if (IsText(node)) {
        // A text is a bar in its colour, as tall as its letters and as wide as its box.
        std::optional<std::pair<std::string, double>> ink = Paint(tree.Shown(index, "Foreground"));
        const double size = std::min(FirstNumber(tree.Shown(index, "FontSize"), 14), box.height);
        if (ink && box.width > 2) {
            const double height = std::max(2.0, size * 0.55);
            shape += "<rect x=\"" + Fmt(box.x + 1) + "\" y=\"" + Fmt(box.y + (box.height - height) / 2) + "\" width=\"" +
                     Fmt(std::max(1.0, box.width - 2)) + "\" height=\"" + Fmt(height) + "\" rx=\"" + Fmt(height / 2) +
                     "\" fill=\"" + ink->first + "\" fill-opacity=\"" + Fmt(ink->second * opacity * 0.8) + "\">" + tip +
                     "</rect>\n";
        }
    }
    return shape;
}

std::string LaidOutWireframe(ReplayBackend& tree, const std::vector<NodeInfo>& nodes, const std::string& title) {
    double width = 0;
    double height = 0;
    if (std::optional<ViewSize> size = tree.WindowSize()) {
        width = size->width;
        height = size->height;
    }
    std::vector<double> opacity(nodes.size(), 1.0);  // what the element shows of itself, its parents' included
    std::string body;
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        const int index = static_cast<int>(nodes[i].id - 1);
        const ReplayBackend::Node& node = tree.Nodes()[index];
        double own = node.reachable ? FirstNumber(tree.Shown(index, "Opacity"), 1) : 1;
        std::optional<Value> visibility = tree.Shown(index, "Visibility");
        if (visibility && visibility->text == "Collapsed") own = 0;
        opacity[i] = (nodes[i].parent >= 0 ? opacity[nodes[i].parent] : 1.0) * std::clamp(own, 0.0, 1.0);
        if (!node.box || opacity[i] <= 0 || node.box->width <= 0 || node.box->height <= 0) continue;
        width = std::max(width, node.box->x + node.box->width);
        height = std::max(height, node.box->y + node.box->height);
        body += BoxShape(tree, index, *node.box, opacity[i]);
    }
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + Fmt(width) + "\" height=\"" + Fmt(height) +
           "\" viewBox=\"0 0 " + Fmt(width) + " " + Fmt(height) + "\" font-family=\"Segoe UI, sans-serif\">\n<title>" +
           Escape(title) + "</title>\n<rect width=\"100%\" height=\"100%\" fill=\"#808080\" fill-opacity=\"0.15\"/>\n" +
           body + "</svg>\n";
}

// Without boxes (a version 1 dump), one row per element that shows a colour or that the theme changed: its name,
// indented by depth, and swatches of its background, border and text colours.
std::string ListWireframe(ReplayBackend& tree, const std::vector<NodeInfo>& nodes, const std::string& title) {
    std::set<int> changed;
    for (const ReplayBackend::Change& change : tree.Changes()) {
        if (change.accepted && change.node >= 0) changed.insert(change.node);
    }
    constexpr double kRow = 16;
    std::string body;
    double y = 24;
    double width = 400;
    for (const NodeInfo& info : nodes) {
        const int index = static_cast<int>(info.id - 1);
        const ReplayBackend::Node& node = tree.Nodes()[index];
        if (!node.reachable) continue;
        std::optional<std::pair<std::string, double>> swatches[3] = {Paint(tree.Shown(index, "Background")),
                                                                     Paint(tree.Shown(index, "BorderBrush")),
                                                                     Paint(tree.Shown(index, "Foreground"))};
        const bool painted = swatches[0] || swatches[1] || swatches[2];
        if (!painted && !changed.count(index)) continue;
        double x = 4 + info.depth * 8.0;
        for (const auto& swatch : swatches) {
            if (swatch) {
                body += "<rect x=\"" + Fmt(x) + "\" y=\"" + Fmt(y - 11) + "\" width=\"12\" height=\"12\" fill=\"" +
                        swatch->first + "\" fill-opacity=\"" + Fmt(swatch->second) + "\" stroke=\"#808080\"/>";
            }
            x += 14;
        }
        body += "<text x=\"" + Fmt(x + 2) + "\" y=\"" + Fmt(y) + "\" font-size=\"11\"" +
                (changed.count(index) ? std::string(" font-weight=\"bold\"") : std::string()) + ">" +
                Escape(Label(node)) + " [" + std::to_string(index) + "]</text>\n";
        width = std::max(width, x + 12 + 7.0 * static_cast<double>(Label(node).size()));
        y += kRow;
    }
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + Fmt(width) + "\" height=\"" + Fmt(y) +
           "\" font-family=\"Segoe UI, sans-serif\">\n<title>" + Escape(title) + "</title>\n<text x=\"4\" y=\"14\" "
           "font-size=\"11\">No boxes in this dump: elements with colours, bold when the theme changed them.</text>\n" +
           body + "</svg>\n";
}

}  // namespace

std::string Wireframe(ReplayBackend& tree, const std::string& title) {
    std::vector<NodeInfo> nodes = tree.Snapshot();
    return tree.HasBoxes() ? LaidOutWireframe(tree, nodes, title) : ListWireframe(tree, nodes, title);
}

}  // namespace ampify::theme::replay
