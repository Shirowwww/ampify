#pragma once
// The right margin Ampify gives the playback bar's song so that it ends short of the toggles (FitSongRoom in
// src/win/ui_service_playbar.cpp), told apart from the margin the layout gives it: Apple's, or a theme's.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "ui_render/style.hpp"

namespace ampify {

using uirender::Edges;

inline bool SameEdges(const Edges& a, const Edges& b) {
    constexpr double kTolerance = 0.5;  // layout rounding
    return std::abs(a.left - b.left) < kTolerance && std::abs(a.top - b.top) < kTolerance &&
           std::abs(a.right - b.right) < kTolerance && std::abs(a.bottom - b.bottom) < kTolerance;
}

// A margin Ampify wrote can come back later: the theme engine keeps the value it found when a theme sets the margin,
// and puts it back when that theme goes, sometimes a layout pass after Ampify fitted the song again. So a margin is
// the layout's only when it is none of the last ones Ampify wrote.
class SongMargin {
public:
    // Fitting the song: the layout's margin, to fit from. A margin Ampify did not write is the layout's new one.
    Edges Base(const Edges& current) {
        if (!base_ || !Mine(current)) base_ = current;
        return *base_;
    }

    void Wrote(const Edges& margin) {
        if (written_.size() == kRemembered) written_.erase(written_.begin());
        written_.push_back(margin);
    }

    // Not fitting any more: the layout's margin to put back, only over Ampify's own (a theme's own margin stays).
    std::optional<Edges> Release(const Edges& current) const {
        if (!base_ || !Mine(current)) return std::nullopt;
        return base_;
    }

private:
    // The engine puts back the margin of the fit just before a theme change; a few more cover a resize meanwhile.
    static constexpr std::size_t kRemembered = 8;

    bool Mine(const Edges& current) const {
        return std::any_of(written_.begin(), written_.end(), [&](const Edges& own) { return SameEdges(current, own); });
    }

    std::optional<Edges> base_;
    std::vector<Edges> written_;
};

}  // namespace ampify
