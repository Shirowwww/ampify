#pragma once
// What xaml_properties.cpp, xaml_read.cpp and xaml_motion.cpp share.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "apple_properties.hpp"
#include "artwork_cap.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win::xaml_detail {

theme::Value NumberValue(double value);

std::vector<xaml::Documents::Inline> InlinesOf(const controls::TextBlock& text);

bool HasLink(const std::vector<xaml::Documents::Inline>& inlines);

winrt::Windows::Foundation::TimeSpan Milliseconds(double ms);

// A storyboard easing the transform's scale, rotation and translation from `from` to `to`, empty when nothing moves.
animation::Storyboard EaseTransform(const media::CompositeTransform& transform, const double (&from)[5],
                                    const std::vector<double>& to, double ms);

}  // namespace ampify::win::xaml_detail

#endif  // AMPIFY_WITH_XAML
