#include "setup_window.hpp"

#include <commctrl.h>
#include <dwmapi.h>
// oleacc.lib does not hold the GUIDs of the property services: defined here.
#include <initguid.h>
#include <oleacc.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>

#include "../win/win_common.hpp"
#include "setup_window_state.hpp"

namespace ampify::setup {

using namespace detail;

namespace {

constexpr UINT kStepsChanged = WM_APP + 1;
constexpr UINT kWorkDone = WM_APP + 2;
constexpr UINT_PTR kFrameTimer = 1;

constexpr int kHeaderId = 10;
constexpr int kTitleId = 11;
constexpr int kTextId = 12;
constexpr int kCheckboxId = 13;
constexpr int kNoticeId = 100;
constexpr int kStepId = 200;

constexpr wchar_t kWindowClass[] = L"AmpifySetupWindow";

Window::State* StateOf(HWND window) {
    return reinterpret_cast<Window::State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

// Every child paints its whole rectangle itself: no erase, no flicker. The buttons also follow the pointer.
LRESULT CALLBACK ChildProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto* state = reinterpret_cast<Window::State*>(data);
    Element* element = state->Find(hwnd);
    bool button = element && IsButton(element->kind);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (!button) break;
        SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_HAND)));
        return TRUE;
    case WM_MOUSEMOVE:
        if (button && state->hover != hwnd) {
            state->SetHover(hwnd);
            TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
        }
        break;
    case WM_MOUSELEAVE:
        if (state->hover == hwnd) state->SetHover(nullptr);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    Window::State* state = StateOf(hwnd);
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        state->Paint();
        return 0;
    case WM_DRAWITEM:
        state->Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_COMMAND:
        state->Command(LOWORD(wParam));
        return 0;
    case WM_CLOSE:
        if (!state->running) state->answer = Choice::Secondary;  // the work cannot be left half done
        return 0;
    case WM_TIMER:
        state->Frame();
        return 0;
    case kStepsChanged:
        state->StepsChanged();
        return 0;
    case kWorkDone:
        state->done = true;
        return 0;
    case WM_DPICHANGED:
        state->dpi = HIWORD(wParam);
        state->CreateTarget();
        state->Layout(false);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

namespace detail {

std::uint64_t Now() { return GetTickCount64(); }

}  // namespace detail

Element* Window::State::Find(HWND hwnd) {
    for (Element& element : elements) {
        if (element.hwnd == hwnd) return &element;
    }
    return nullptr;
}

Element* Window::State::FindKind(Kind kind) {
    for (Element& element : elements) {
        if (element.kind == kind) return &element;
    }
    return nullptr;
}

ComPtr<IDWriteTextFormat> Window::State::Format(const wchar_t* face, DWRITE_FONT_WEIGHT weight, float size) {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-us";
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    ComPtr<IDWriteTextFormat> format;
    dwrite->CreateTextFormat(face, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, locale,
                             &format);
    return format;
}

// Factories, fonts and the pen of the marks. The fonts are Windows' own: nothing is shipped with the installer.
void Window::State::CreateResources() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite->GetSystemFontCollection(&fonts))) fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists);
    std::wstring text = exists ? L"Segoe UI Variable Text" : L"Segoe UI";
    std::wstring display = exists ? L"Segoe UI Variable Display" : L"Segoe UI";
    brand = Format(display.c_str(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 15);
    note = Format(text.c_str(), DWRITE_FONT_WEIGHT_NORMAL, 13);
    title = Format(display.c_str(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 24);
    body = Format(text.c_str(), DWRITE_FONT_WEIGHT_NORMAL, 14);
    label = Format(text.c_str(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 14);
    label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    label->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    brand->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    noteMiddle = Format(text.c_str(), DWRITE_FONT_WEIGHT_NORMAL, 13);
    noteMiddle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    bodyMiddle = Format(text.c_str(), DWRITE_FONT_WEIGHT_NORMAL, 14);
    bodyMiddle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    D2D1_STROKE_STYLE_PROPERTIES pen = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                   D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    d2d->CreateStrokeStyle(pen, nullptr, 0, &round);
    BOOL motion = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion, 0);
    animate = motion != FALSE;
}

// One target draws every child in turn (WM_DRAWITEM hands each one's DC). Made again when the DPI changes.
void Window::State::CreateTarget() {
    target.Reset();
    logo.Reset();
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi), static_cast<float>(dpi));
    d2d->CreateDCRenderTarget(&properties, &target);
}

float Window::State::Measure(IDWriteTextFormat* format, const std::wstring& text, float width, float* textWidth) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 10000, &layout))) {
        return 0;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    if (textWidth) *textWidth = metrics.widthIncludingTrailingWhitespace;
    return std::ceil(metrics.height);
}

// The children of a page, in reading order, which is also the order of Tab and of their entrance.
void Window::State::Build(const Page& next) {
    for (Element& element : elements) DestroyWindow(element.hwnd);
    elements.clear();
    hover = nullptr;
    page = next;
    elements.push_back({Kind::Header, kHeaderId, L"Ampify " + version});
    elements.push_back({Kind::Title, kTitleId, page.title});
    if (!page.text.empty()) elements.push_back({Kind::Text, kTextId, page.text});
    for (size_t i = 0; i < page.notices.size(); i++) {
        elements.push_back({Kind::Notice, kNoticeId + static_cast<int>(i), page.notices[i].text, i});
    }
    if (!page.checkbox.empty()) elements.push_back({Kind::Checkbox, kCheckboxId, page.checkbox});
    for (size_t i = 0; i < page.steps.size(); i++) {
        elements.push_back({Kind::Step, kStepId + static_cast<int>(i), page.steps[i], i});
    }
    if (!page.secondary.empty()) elements.push_back({Kind::Secondary, IDCANCEL, page.secondary});
    if (!page.primary.empty()) elements.push_back({Kind::Primary, IDOK, page.primary});
    steps.assign(page.steps.size(), StepState::Pending);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    for (size_t i = 0; i < elements.size(); i++) {
        Element& element = elements[i];
        element.delay = (std::min)(i, kStaggerSteps) * kStaggerMs;
        element.check.Jump(element.kind == Kind::Checkbox && page.checked ? 1.0f : 0.0f);
        bool button = IsButton(element.kind);
        DWORD style = WS_CHILD | WS_VISIBLE | (button ? WS_TABSTOP | BS_OWNERDRAW : SS_OWNERDRAW);
        element.hwnd = CreateWindowExW(0, button ? L"BUTTON" : L"STATIC", element.text.c_str(), style, 0, 0, 0, 0,
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(element.id)), instance, nullptr);
        SetWindowSubclass(element.hwnd, ChildProc, 1, reinterpret_cast<DWORD_PTR>(this));
        if (element.kind == Kind::Checkbox) AnnotateCheckbox(element.hwnd);
        if (!button) Annotate(element.hwnd, ROLE_SYSTEM_STATICTEXT, -1);
    }
    pageStart = Now();
    StartFrames();
}

// Places the content from the top down. Returns where it ends.
float Window::State::StackContent() {
    const float content = kWidth - 2 * kPad;
    float y = kHeaderTop;
    for (Element& element : elements) {
        float height = 0;
        switch (element.kind) {
        case Kind::Header: height = kLogo; break;
        case Kind::Title: height = Measure(title.Get(), element.text, content); break;
        case Kind::Text: height = Measure(body.Get(), element.text, content); break;
        case Kind::Notice:
            y += 16;
            height = Measure(note.Get(), element.text, content - kNoticeIndent - 16) + 24;
            break;
        case Kind::Checkbox:
            y += 20;
            height = (std::max)(kMark, Measure(body.Get(), element.text, content - kMark - 12));
            break;
        case Kind::Step:
            if (element.index == 0) y += 16;
            height = kStepRow;
            break;
        default:
            continue;  // the buttons go in the band at the bottom
        }
        element.box = D2D1::RectF(kPad, y, kWidth - kPad, y + height);
        if (element.kind == Kind::Checkbox) element.box = Inset(element.box, -kFocusGap);
        y += height;
        if (element.kind == Kind::Header) y += 28;
        if (element.kind == Kind::Title) y += 8;
    }
    return y;
}

// The buttons sit at the right of the band at the bottom, the main one last.
void Window::State::PlaceButtons() {
    float right = kWidth - kPad;
    for (auto kind : {Kind::Primary, Kind::Secondary}) {
        Element* button = FindKind(kind);
        if (!button) continue;
        float textWidth = 0;
        Measure(label.Get(), button->text, 1000, &textWidth);
        float width = (std::max)(kButtonMinWidth, std::ceil(textWidth) + 40);
        float top = footerTop + (kFooter - kButtonHeight) / 2;
        button->box = Inset(D2D1::RectF(right - width, top, right, top + kButtonHeight), -kFocusGap);
        right -= width + 8;
    }
}

// Places the children, then sizes the window to them. `center` puts it in the middle of the screen the pointer is on
// (its first page).
void Window::State::Layout(bool center) {
    footerTop = StackContent() + 28;
    PlaceButtons();
    for (const Element& element : elements) {
        MoveWindow(element.hwnd, static_cast<int>(Px(element.box.left)), static_cast<int>(Px(element.box.top)),
                   static_cast<int>(Px(element.box.right) - Px(element.box.left)),
                   static_cast<int>(Px(element.box.bottom) - Px(element.box.top)), TRUE);
    }
    Resize(center);
}

// The window takes the height of its new page. After the first page it glides there, top edge fixed, so the page
// does not jump; the frames of Frame() carry the move.
void Window::State::Resize(bool center) {
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    RECT frame{0, 0, static_cast<LONG>(Px(kWidth)), static_cast<LONG>(Px(footerTop + FooterHeight()))};
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
    windowWidth = frame.right - frame.left;
    int height = frame.bottom - frame.top;
    if (!center && animate) {
        RECT now{};
        GetWindowRect(window, &now);
        windowHeight.Jump(static_cast<float>(now.bottom - now.top));
        windowHeight.To(static_cast<float>(height), Now(), kResizeMs);
        sizing = true;
        InvalidateRect(window, nullptr, TRUE);
        StartFrames();
        return;
    }
    windowHeight.Jump(static_cast<float>(height));
    int width = windowWidth;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    int x = 0;
    int top = 0;
    if (center) {
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof monitor};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        x = (monitor.rcWork.left + monitor.rcWork.right - width) / 2;
        top = (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2;
    } else {
        flags |= SWP_NOMOVE;
    }
    SetWindowPos(window, nullptr, x, top, width, height, flags);
    InvalidateRect(window, nullptr, TRUE);
}

// Screen readers take a drawn button for a push button and a drawn static for a picture: this tells them what each
// one is (the checkbox with its state). `state` is left alone when negative.
void Window::State::Annotate(HWND control, long role, long state) {
    ComPtr<IAccPropServices> services;
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&services)))) return;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = role;
    services->SetHwndProp(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_ROLE, value);
    if (state < 0) return;
    value.lVal = state;
    services->SetHwndProp(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_STATE, value);
}

void Window::State::AnnotateCheckbox(HWND checkbox) {
    Annotate(checkbox, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_FOCUSABLE | (page.checked ? STATE_SYSTEM_CHECKED : 0));
}

// --- Motion: one timer, running only while something moves, and redrawing only what moves ---

bool Window::State::Animating(const Element& element, std::uint64_t now) const {
    std::uint64_t intro = animate ? kIntroMs : kIntroCalmMs;
    if (now < pageStart + element.delay + intro) return true;
    if (element.hover.Moving(now) || element.check.Moving(now) || element.mark.Moving(now)) return true;
    return animate && element.kind == Kind::Step && steps[element.index] == StepState::Running;
}

// True while a step's mark is still settling, or was settled less than kLastMarkHoldMs ago.
bool Window::State::StepsSettling(std::uint64_t now) const {
    for (const Element& element : elements) {
        if (element.kind == Kind::Step && element.mark.EndsAt() != 0 && now < element.mark.EndsAt() + kLastMarkHoldMs) {
            return true;
        }
    }
    return false;
}

void Window::State::StartFrames() { SetTimer(window, kFrameTimer, kFrameMs, nullptr); }

void Window::State::Frame() {
    std::uint64_t now = Now();
    bool any = false;
    for (const Element& element : elements) {
        if (!Animating(element, now)) continue;
        any = true;
        InvalidateRect(element.hwnd, nullptr, FALSE);
    }
    if (sizing) {
        sizing = windowHeight.Moving(now);  // the last frame puts the exact height
        any = any || sizing;
        SetWindowPos(window, nullptr, 0, 0, windowWidth, static_cast<int>(std::lround(windowHeight.At(now))),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // While the last marks are held in sight the frames go on: they are what ends the wait in Run.
    if (!any && running && StepsSettling(now)) return;
    // One more frame draws the final state of what just stopped.
    if (!any) {
        KillTimer(window, kFrameTimer);
        for (const Element& element : elements) InvalidateRect(element.hwnd, nullptr, FALSE);
    }
}

void Window::State::SetHover(HWND hwnd) {
    std::uint64_t now = Now();
    for (Element& element : elements) {
        bool under = element.hwnd == hwnd;
        bool was = element.hwnd == hover;
        if (under != was) element.hover.To(under ? 1.0f : 0.0f, now, kHoverMs, kEase);
    }
    hover = hwnd;
    StartFrames();
}

void Window::State::StepsChanged() {
    std::vector<StepState> next;
    {
        std::lock_guard<std::mutex> lock(stepsLock);
        next = pendingSteps;
    }
    if (next.size() != steps.size()) return;
    std::uint64_t now = Now();
    for (Element& element : elements) {
        if (element.kind != Kind::Step) continue;
        StepState state = next[element.index];
        bool settles = state == StepState::Done || state == StepState::Failed;
        if (settles && state != steps[element.index]) {
            element.mark.Jump(0);
            element.mark.To(1, now, animate ? kMarkMs : kIntroCalmMs);
        }
    }
    steps = next;
    StartFrames();
}

// Enter answers the focused button, or the main one; Escape and the other button answer Secondary.
void Window::State::Command(int id) {
    if (running) return;
    Element* checkbox = FindKind(Kind::Checkbox);
    if (id == kCheckboxId && checkbox) {
        page.checked = !page.checked;
        checkbox->check.To(page.checked ? 1.0f : 0.0f, Now(), kCheckMs);
        AnnotateCheckbox(checkbox->hwnd);
        StartFrames();
        return;
    }
    Element* primary = FindKind(Kind::Primary);
    Element* secondary = FindKind(Kind::Secondary);
    if (id == IDOK) {
        bool onSecondary = secondary && GetFocus() == secondary->hwnd;
        answer = primary && !onSecondary ? Choice::Primary : Choice::Secondary;
    } else if (id == IDCANCEL) {
        answer = Choice::Secondary;
    }
}

namespace {

// Each step that changes posts one message; the window copies the whole list when it gets it.
class WindowProgress final : public Progress {
public:
    explicit WindowProgress(Window::State& state) : m_state(state) {}
    void Set(size_t step, StepState value) override {
        {
            std::lock_guard<std::mutex> lock(m_state.stepsLock);
            if (step < m_state.pendingSteps.size()) m_state.pendingSteps[step] = value;
        }
        PostMessageW(m_state.window, kStepsChanged, 0, 0);
    }

private:
    Window::State& m_state;
};

void Pump(HWND window, const std::function<bool()>& finished) {
    MSG message;
    while (!finished() && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Show(Window::State& state, const Page& page) {
    bool first = !IsWindowVisible(state.window);
    state.Build(page);
    state.Layout(first);
    if (first) {
        ShowWindow(state.window, SW_SHOWNORMAL);
        SetForegroundWindow(state.window);
    }
    Element* focus = state.FindKind(Kind::Primary);
    if (!focus) focus = state.FindKind(Kind::Secondary);
    SetFocus(focus ? focus->hwnd : state.window);
}

}  // namespace

Window::Window(const std::wstring& version) : m_state(std::make_unique<State>()) {
    State& state = *m_state;
    state.version = version;
    state.CreateResources();
    WNDCLASSEXW windowClass{sizeof windowClass};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    windowClass.hIcon = ampify::win::AmpifyIcon(true);
    windowClass.hIconSm = ampify::win::AmpifyIcon(false);
    RegisterClassExW(&windowClass);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    state.window = CreateWindowExW(0, kWindowClass, L"Ampify", style, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr,
                                   nullptr, windowClass.hInstance, &state);
    state.dpi = GetDpiForWindow(state.window);
    // A dark title bar whose colour runs into the page (the colour attribute exists from Windows 11).
    BOOL dark = TRUE;
    DwmSetWindowAttribute(state.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF captionColour = RGB((kBackground >> 16) & 0xFF, (kBackground >> 8) & 0xFF, kBackground & 0xFF);
    DwmSetWindowAttribute(state.window, DWMWA_CAPTION_COLOR, &captionColour, sizeof captionColour);
    // As in a dialog, the focus ring shows once the keyboard is used, not on a click.
    SendMessageW(state.window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    state.CreateTarget();
}

Window::~Window() {
    if (m_state->window) DestroyWindow(m_state->window);
}

Choice Window::Ask(const Page& page, bool* checked) {
    State& state = *m_state;
    Show(state, page);
    state.answer.reset();
    Pump(state.window, [&state] { return state.answer.has_value(); });
    if (checked) *checked = state.page.checked;
    return state.answer.value_or(Choice::Secondary);
}

void Window::Run(const Page& page, const std::function<void(Progress&)>& work) {
    State& state = *m_state;
    Show(state, page);
    {
        std::lock_guard<std::mutex> lock(state.stepsLock);
        state.pendingSteps.assign(page.steps.size(), StepState::Pending);
    }
    state.running = true;
    state.done = false;
    WindowProgress progress(state);
    std::exception_ptr failure;
    std::thread worker([&] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            work(progress);
        } catch (...) {
            failure = std::current_exception();
        }
        if (SUCCEEDED(com)) CoUninitialize();
        PostMessageW(state.window, kWorkDone, 0, 0);
    });
    // The work may end in a moment: its last marks settle, and stay in sight briefly, before the next page comes.
    Pump(state.window, [&state] { return state.done && !state.StepsSettling(Now()); });
    worker.join();
    state.running = false;
    if (failure) std::rethrow_exception(failure);
}

}  // namespace ampify::setup
