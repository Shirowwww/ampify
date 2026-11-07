#include "hosted_text_align.hpp"
#include "xaml_chrome.hpp"

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
    th::RowAlign align = th::RowAlign::Start;
    winrt::event_token sized{};
    bool queued = false;
};

// Interface thread only. Never destroyed, like the other static services (xaml-lessons.md).
using HostStates = ElementMap<controls::Border, HostState>;

HostStates& States() {
    static auto* states = new HostStates();
    return *states;
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
void QueueAlign(HostStates::Key key) {
    auto* found = States().FindKey(key);
    if (!found || found->value.queued) return;
    controls::Border host = found->element.get();
    if (!host) return;
    found->value.queued = true;
    host.DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [key]() noexcept {
        try {
            auto* state = States().FindKey(key);
            if (!state) return;
            state->value.queued = false;
            if (controls::Border alive = state->element.get()) AlignRows(alive, state->value.align);
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
    HostState* found = States().Find(host);
    if (!found) return th::RowAlignKeyword(th::RowAlign::Start);
    // Words drawn again at the left read as Start, so the pass writes the theme's alignment again.
    try {
        if (!found->queued && !RowsInPlace(host, found->align)) return th::RowAlignKeyword(th::RowAlign::Start);
    } catch (...) {
    }
    return th::RowAlignKeyword(found->align);
}

void SetHostedTextAlign(const controls::Border& host, th::RowAlign align) noexcept {
    try {
        auto& states = States();
        states.Sweep();
        HostStates::Key key = HostStates::KeyOf(host);
        HostState* found = states.Find(host);
        if (align == th::RowAlign::Start) {
            if (found) {
                host.SizeChanged(found->sized);
                states.Erase(host);
            }
            AlignRows(host, align);
            return;
        }
        if (!found) {
            HostState state{align};
            state.sized = host.SizeChanged([key](const auto&, const auto&) noexcept {
                try {
                    QueueAlign(key);
                } catch (...) {
                }
            });
            found = &states.Add(host, state);
        }
        found->align = align;
        AlignRows(host, align);
        QueueAlign(key);
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
