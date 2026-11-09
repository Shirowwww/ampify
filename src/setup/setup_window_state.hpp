#pragma once

// The inside of the installer window (setup_window.hpp): its palette, its layout, its motion and its state, shared by
// setup_window.cpp (the window, the layout, the messages) and setup_window_draw.cpp (the drawing).

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "setup_motion.hpp"
#include "setup_window.hpp"

namespace ampify::setup::detail {

using Microsoft::WRL::ComPtr;

// The palette of Ampify's site (docs/assets/site.css), so the installer looks like the rest of Ampify.
inline constexpr UINT32 kBackground = 0x0F0D0E;
inline constexpr UINT32 kRaise = 0x191617;
inline constexpr UINT32 kRaise2 = 0x221E20;
inline constexpr UINT32 kLine = 0x2F2A2C;
inline constexpr UINT32 kLineStrong = 0x5A5255;  // the edge of a control: it must stand out from kRaise2
inline constexpr UINT32 kInk = 0xF4F0F1;
inline constexpr UINT32 kMuted = 0xA59DA0;
inline constexpr UINT32 kAccent = 0xFA2D48;
inline constexpr UINT32 kButton = 0xE0203C;  // the accent a little darker, so white text on it keeps a 4.5:1 contrast
inline constexpr UINT32 kButtonHover = 0xE72742;
inline constexpr UINT32 kButtonPressed = 0xC51B35;
inline constexpr UINT32 kOk = 0x5CCF8C;
inline constexpr UINT32 kOkInk = 0x0B1A10;
inline constexpr UINT32 kWhite = 0xFFFFFF;

// Layout, in device-independent pixels (96 per inch).
inline constexpr float kWidth = 560;
inline constexpr float kPad = 32;
inline constexpr float kHeaderTop = 24;
inline constexpr float kLogo = 28;
inline constexpr float kFooter = 72;
inline constexpr float kButtonHeight = 36;
inline constexpr float kButtonMinWidth = 108;
inline constexpr float kFocusGap = 4;  // room around a control for its keyboard focus ring
inline constexpr float kStepRow = 32;
inline constexpr float kMark = 18;  // the circle of a step, the box of the checkbox
inline constexpr float kNoticeIndent = 42;

// Motion. A page's content rises into place one element after the other; a step's mark settles in; colours follow
// the pointer. With Windows' animations off, nothing moves: the fades stay, shorter.
inline constexpr std::uint64_t kIntroMs = 240;
inline constexpr std::uint64_t kIntroCalmMs = 120;  // the same fade, without movement
inline constexpr std::uint64_t kStaggerMs = 35;
inline constexpr size_t kStaggerSteps = 5;  // later elements share the last delay, so the page is never slow
inline constexpr float kRise = 6;
inline constexpr std::uint64_t kHoverMs = 150;
inline constexpr std::uint64_t kCheckMs = 160;
inline constexpr std::uint64_t kMarkMs = 220;
inline constexpr std::uint64_t kSpinTurnMs = 900;
inline constexpr std::uint64_t kResizeMs = 220;  // the window gliding to the height of its next page
inline constexpr std::uint64_t kLastMarkHoldMs = 350;  // the finished steps stay in sight a moment before the result
inline constexpr UINT kFrameMs = 15;

enum class Kind { Header, Title, Text, Notice, Checkbox, Step, Primary, Secondary };

struct Element {
    Kind kind;
    int id = 0;
    std::wstring text;
    size_t index = 0;  // which notice or step
    HWND hwnd = nullptr;
    D2D1_RECT_F box{};  // its place in the client area
    std::uint64_t delay = 0;  // of its entrance after the page's
    Tween hover;  // 1 under the pointer
    Tween check;  // the checkbox: 1 ticked
    Tween mark;   // a step: 1 once its tick or cross has settled
};

inline bool IsButton(Kind kind) {
    return kind == Kind::Primary || kind == Kind::Secondary || kind == Kind::Checkbox;
}

inline D2D1_COLOR_F Colour(UINT32 rgb, float alpha = 1) { return D2D1::ColorF(rgb, alpha); }

inline D2D1_RECT_F Inset(const D2D1_RECT_F& box, float by) {
    return D2D1::RectF(box.left + by, box.top + by, box.right - by, box.bottom - by);
}

std::uint64_t Now();

}  // namespace ampify::setup::detail

namespace ampify::setup {

struct Window::State {
    HWND window = nullptr;
    UINT dpi = 96;
    std::wstring version;
    detail::ComPtr<ID2D1Factory> d2d;
    detail::ComPtr<IDWriteFactory> dwrite;
    detail::ComPtr<ID2D1DCRenderTarget> target;
    detail::ComPtr<ID2D1StrokeStyle> round;
    detail::ComPtr<ID2D1Bitmap> logo;
    detail::ComPtr<IDWriteTextFormat> brand, note, title, body, label;
    detail::ComPtr<IDWriteTextFormat> noteMiddle, bodyMiddle;  // centred on the height of their box
    Page page;
    std::vector<detail::Element> elements;
    float footerTop = 0;
    float FooterHeight() const { return page.primary.empty() && page.secondary.empty() ? detail::kPad : detail::kFooter; }
    Tween windowHeight;  // in pixels, while the window glides to a new height
    bool sizing = false;
    int windowWidth = 0;
    HWND hover = nullptr;
    std::optional<Choice> answer;
    bool running = false;
    bool done = false;
    bool animate = true;  // Windows' "Show animations in Windows"
    std::uint64_t pageStart = 0;
    float alpha = 1;  // the opacity of what is being drawn, during an entrance; set by Draw()
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Identity();  // the offset of what is being drawn; set by Draw()
    std::mutex stepsLock;
    std::vector<StepState> pendingSteps;  // written by the work's thread
    std::vector<StepState> steps;         // what the window shows

    float Px(float dip) const { return std::round(dip * static_cast<float>(dpi) / 96.0f); }
    detail::Element* Find(HWND hwnd);
    detail::Element* FindKind(detail::Kind kind);

    // setup_window.cpp
    detail::ComPtr<IDWriteTextFormat> Format(const wchar_t* face, DWRITE_FONT_WEIGHT weight, float size);
    void CreateResources();
    void CreateTarget();
    float Measure(IDWriteTextFormat* format, const std::wstring& text, float width, float* textWidth = nullptr);
    void Build(const Page& next);
    float StackContent();
    void PlaceButtons();
    void Layout(bool center);
    void Resize(bool center);
    void Annotate(HWND control, long role, long state);
    void AnnotateCheckbox(HWND checkbox);
    bool Animating(const detail::Element& element, std::uint64_t now) const;
    bool StepsSettling(std::uint64_t now) const;
    void StartFrames();
    void Frame();
    void SetHover(HWND hwnd);
    void StepsChanged();
    void Command(int id);

    // setup_window_draw.cpp
    void Paint();
    void Draw(const DRAWITEMSTRUCT& item);
    float Entrance(const detail::Element& element, std::uint64_t now) const;
    detail::ComPtr<ID2D1SolidColorBrush> Brush(UINT32 colour, float opacity = 1);
    void Write(IDWriteTextFormat* format, const std::wstring& text, const D2D1_RECT_F& box, UINT32 colour);
    void DrawHeader(const D2D1_SIZE_F& size);
    void DrawNotice(const detail::Element& element, const D2D1_SIZE_F& size);
    void DrawCheckbox(const detail::Element& element, const D2D1_SIZE_F& size, UINT itemState, std::uint64_t now);
    void DrawStep(const detail::Element& element, const D2D1_SIZE_F& size, std::uint64_t now);
    void DrawButton(const detail::Element& element, const D2D1_SIZE_F& size, UINT itemState, std::uint64_t now);
    void DrawCheck(const D2D1_RECT_F& box, UINT32 colour, float opacity);
    void DrawCross(const D2D1_POINT_2F& centre, float opacity);
    void DrawSpinner(const D2D1_POINT_2F& centre, float radius, std::uint64_t now);
    void DrawFocus(const D2D1_RECT_F& around, float radius, UINT itemState);
};

}  // namespace ampify::setup
