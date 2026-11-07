#include "element_picker.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr std::int64_t kRequestLifeSeconds = 60;
constexpr std::size_t kMaxElements = 30;

struct WindowSearch {
    DWORD processId = 0;
    HWND best = nullptr;
    long bestArea = 0;
};

// The biggest visible top-level window of this process that nothing owns: Apple Music's main window.
BOOL CALLBACK FindWindowOfProcess(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search->processId || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return TRUE;
    long area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search->bestArea) {
        search->bestArea = area;
        search->best = window;
    }
    return TRUE;
}

std::string ShortClassName(const xaml::DependencyObject& object) {
    std::string name = winrt::to_string(winrt::get_class_name(object));
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::string Describe(const xaml::UIElement& element) {
    std::ostringstream line;
    line << ShortClassName(element);
    auto framework = element.try_as<xaml::FrameworkElement>();
    if (framework && !framework.Name().empty()) line << "#" << winrt::to_string(framework.Name());
    if (framework) line << "  " << static_cast<int>(framework.ActualWidth()) << "x" << static_cast<int>(framework.ActualHeight());
    if (element.Visibility() != xaml::Visibility::Visible) line << "  collapsed";
    winrt::hstring automation = xaml::Automation::AutomationProperties::GetName(element);
    if (!automation.empty()) line << "  \"" << winrt::to_string(automation) << "\"";
    return line.str();
}

// The elements under a place of the screen, topmost first, as text.
std::string ElementsAt(long screenX, long screenY) {
    xaml::Window window = AppWindowOf(WindowRole::Main);
    xaml::UIElement content = window ? window.Content() : nullptr;
    if (!content || !content.XamlRoot()) return "Apple Music's main window has no content yet.\n";

    WindowSearch search;
    search.processId = GetCurrentProcessId();
    EnumWindows(FindWindowOfProcess, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return "Apple Music's main window was not found.\n";

    POINT client{screenX, screenY};
    ScreenToClient(search.best, &client);
    double scale = content.XamlRoot().RasterizationScale();
    winrt::Windows::Foundation::Point point{static_cast<float>(client.x / scale), static_cast<float>(client.y / scale)};

    std::vector<xaml::UIElement> hits;
    for (const xaml::UIElement& element : xaml::Media::VisualTreeHelper::FindElementsInHostCoordinates(point, content, true)) {
        hits.push_back(element);
        if (hits.size() >= kMaxElements) break;
    }

    std::ostringstream text;
    text << "Elements under the pointer (screen " << screenX << "," << screenY << ", window " << client.x << ","
         << client.y << " px), topmost first.\n";
    if (hits.empty()) {
        text << "Nothing there: the pointer was not over Apple Music's main window.\n";
        return text.str();
    }
    for (const xaml::UIElement& element : hits) {
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (framework && !framework.Name().empty()) {
            text << "Selector for a theme: " << ShortClassName(element) << "#" << winrt::to_string(framework.Name())
                 << " (see docs/themes/names.md for the names Ampify keeps)\n";
            break;
        }
    }
    int index = 0;
    for (const xaml::UIElement& element : hits) text << "  " << ++index << "  " << Describe(element) << "\n";
    return text.str();
}

}  // namespace

bool AnswerPickRequest(const std::filesystem::path& kit, const std::filesystem::path& output, Journal& journal) noexcept {
    try {
        std::filesystem::path request = kit / "pick.txt";
        std::string content;
        {
            std::ifstream in(request, std::ios::binary);
            if (!in) return false;
            std::ostringstream all;
            all << in.rdbuf();
            content = all.str();
        }
        std::error_code ec;
        std::filesystem::remove(request, ec);

        long x = 0;
        long y = 0;
        long long asked = 0;
        std::istringstream fields(content);
        if (!(fields >> x >> y >> asked)) {
            journal.Write("Inspector: the pick request is not readable, dropped");
            return false;
        }
        long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - asked > kRequestLifeSeconds) {
            journal.Write("Inspector: a pick request older than a minute was dropped");
            return false;
        }
        std::ofstream(output, std::ios::binary | std::ios::trunc) << ElementsAt(x, y);
        journal.Write("Inspector: the element under the pointer is in logs\\picked.txt");
        return true;
    } catch (...) {
        journal.Write("Inspector: the element under the pointer could not be read");
        return false;
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
