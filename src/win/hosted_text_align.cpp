#include "hosted_text_align.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ampify::win {

namespace {

namespace th = ampify::theme;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace comp = winrt::Microsoft::UI::Composition;

constexpr int kMaxWrapperDepth = 3;  // containers holding a single container, gone through to reach the rows

struct HostState {
    winrt::weak_ref<controls::Border> host;
    th::RowAlign align = th::RowAlign::Start;
    winrt::event_token sized{};
    bool queued = false;
};

// Interface thread only. Never destroyed, like the other static services (xaml-lessons.md).
std::unordered_map<void*, HostState>& States() {
    static auto* states = new std::unordered_map<void*, HostState>();
    return *states;
}

void ForgetDeadHosts() {
    auto& states = States();
    for (auto it = states.begin(); it != states.end();) {
        it = it->second.host.get() ? std::next(it) : states.erase(it);
    }
}

// The visuals Apple drew the words with: below the Border's child visual, past wrappers that hold one container.
comp::ContainerVisual GlyphLayer(const controls::Border& host) {
    auto layer = xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(host).try_as<comp::ContainerVisual>();
    for (int depth = 0; layer && depth < kMaxWrapperDepth && layer.Children().Count() == 1; ++depth) {
        auto only = layer.Children().First().Current().try_as<comp::ContainerVisual>();
        if (!only) break;
        layer = only;
    }
    return layer;
}

// A word's visual may have no size of its own, only letters inside it: its box then spans them.
th::GlyphBox BoxOf(const comp::Visual& visual) {
    auto offset = visual.Offset();
    auto size = visual.Size();
    th::GlyphBox box{offset.x, offset.y, size.x, size.y};
    auto container = visual.try_as<comp::ContainerVisual>();
    if (!container || (size.x > 0 && size.y > 0)) return box;
    for (const comp::Visual& child : container.Children()) {
        box.width = std::max(box.width, static_cast<double>(child.Offset().x + child.Size().x));
        box.height = std::max(box.height, static_cast<double>(child.Offset().y + child.Size().y));
    }
    return box;
}

struct Rows {
    std::vector<comp::Visual> visuals;
    std::vector<double> shifts;  // where `align` puts each visual
};

Rows RowsOf(const controls::Border& host, th::RowAlign align) {
    Rows rows;
    comp::ContainerVisual layer = GlyphLayer(host);
    if (!layer) return rows;
    std::vector<th::GlyphBox> boxes;
    for (const comp::Visual& visual : layer.Children()) {
        rows.visuals.push_back(visual);
        boxes.push_back(BoxOf(visual));
    }
    rows.shifts = th::RowShifts(boxes, host.ActualWidth(), align);
    return rows;
}

// Without a SizeChanged, Apple may still have drawn new words in the host: the shifts read back tell.
bool RowsInPlace(const controls::Border& host, th::RowAlign align) {
    Rows rows = RowsOf(host, align);
    std::vector<double> current;
    for (const comp::Visual& visual : rows.visuals) current.push_back(visual.TransformMatrix().m41);
    return th::ShiftsInPlace(current, rows.shifts);
}

void AlignRows(const controls::Border& host, th::RowAlign align) {
    auto [visuals, shifts] = RowsOf(host, align);
    for (size_t i = 0; i < visuals.size(); ++i) {
        visuals[i].TransformMatrix(
            winrt::Windows::Foundation::Numerics::make_float4x4_translation(static_cast<float>(shifts[i]), 0.0f, 0.0f));
    }
}

// After the layout pass that resized the host: Apple lays its words out again in that pass.
void QueueAlign(void* key) {
    auto found = States().find(key);
    if (found == States().end() || found->second.queued) return;
    controls::Border host = found->second.host.get();
    if (!host) return;
    found->second.queued = true;
    host.DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [key]() noexcept {
        try {
            auto state = States().find(key);
            if (state == States().end()) return;
            state->second.queued = false;
            if (controls::Border alive = state->second.host.get()) AlignRows(alive, state->second.align);
        } catch (...) {
        }
    });
}

}  // namespace

controls::Border DrawnTextHost(const xaml::DependencyObject& object) {
    auto border = object.try_as<controls::Border>();
    return border && !border.Child() ? border : nullptr;
}

std::string_view HostedTextAlign(const controls::Border& host) noexcept {
    auto found = States().find(winrt::get_abi(host));
    if (found == States().end() || found->second.host.get() != host) return th::RowAlignKeyword(th::RowAlign::Start);
    // Words drawn again at the left read as Start, so the pass writes the theme's alignment again.
    try {
        if (!found->second.queued && !RowsInPlace(host, found->second.align)) return th::RowAlignKeyword(th::RowAlign::Start);
    } catch (...) {
    }
    return th::RowAlignKeyword(found->second.align);
}

void SetHostedTextAlign(const controls::Border& host, th::RowAlign align) noexcept {
    try {
        ForgetDeadHosts();
        void* key = winrt::get_abi(host);
        auto& states = States();
        auto found = states.find(key);
        if (align == th::RowAlign::Start) {
            if (found != states.end()) {
                host.SizeChanged(found->second.sized);
                states.erase(found);
            }
            AlignRows(host, align);
            return;
        }
        if (found == states.end()) {
            HostState state{winrt::make_weak(host), align};
            state.sized = host.SizeChanged([key](const auto&, const auto&) noexcept {
                try {
                    QueueAlign(key);
                } catch (...) {
                }
            });
            found = states.emplace(key, state).first;
        }
        found->second.align = align;
        AlignRows(host, align);
        QueueAlign(key);
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
