// The drawing of the installer window (setup_window.hpp): each child draws itself with Direct2D, at the opacity and
// the offset of its entrance.

#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "setup_window_state.hpp"

namespace ampify::setup {

using namespace detail;

namespace {

// A colour between two, `t` from 0 (the first) to 1 (the second).
UINT32 Mix(UINT32 from, UINT32 to, float t) {
    auto channel = [t](UINT32 a, UINT32 b, int shift) {
        float x = static_cast<float>((a >> shift) & 0xFF);
        float y = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<UINT32>(std::lround(x + (y - x) * t)) << shift;
    };
    return channel(from, to, 16) | channel(from, to, 8) | channel(from, to, 0);
}

bool ShowsFocus(UINT itemState) { return (itemState & ODS_FOCUS) && !(itemState & ODS_NOFOCUSRECT); }

}  // namespace

// The window itself only shows the background and the band of the buttons; the children draw the rest.
void Window::State::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    auto fill = [dc](RECT box, UINT32 rgb) {
        HBRUSH brush = CreateSolidBrush(RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        FillRect(dc, &box, brush);
        DeleteObject(brush);
    };
    // A page without buttons (the steps while they run) has no band: the content simply ends.
    bool band = FooterHeight() == kFooter;
    LONG footer = band ? static_cast<LONG>(Px(footerTop)) : client.bottom;
    fill({client.left, client.top, client.right, footer}, kBackground);
    if (!band) {
        EndPaint(window, &paint);
        return;
    }
    fill({client.left, footer, client.right, client.bottom}, kRaise);
    fill({client.left, footer, client.right, footer + (std::max)(1L, static_cast<LONG>(Px(1)))}, kLine);
    EndPaint(window, &paint);
}

// How far the element's entrance has gone, from 0 (not yet shown) to 1 (in place).
float Window::State::Entrance(const Element& element, std::uint64_t now) const {
    std::uint64_t start = pageStart + element.delay;
    if (now <= start) return 0;
    float progress = static_cast<float>(now - start) / static_cast<float>(animate ? kIntroMs : kIntroCalmMs);
    return kEaseOut.At(progress);
}

void Window::State::Draw(const DRAWITEMSTRUCT& item) {
    Element* element = Find(item.hwndItem);
    if (!element || !target) return;
    RECT pixels = item.rcItem;
    if (FAILED(target->BindDC(item.hDC, &pixels))) return;
    std::uint64_t now = Now();
    float entrance = Entrance(*element, now);
    alpha = entrance;
    base = D2D1::Matrix3x2F::Translation(0, animate ? (1 - entrance) * kRise : 0);
    D2D1_SIZE_F size = target->GetSize();
    D2D1_RECT_F whole = D2D1::RectF(0, 0, size.width, size.height);
    target->BeginDraw();
    bool inFooter = element->kind == Kind::Primary || element->kind == Kind::Secondary;
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(Colour(inFooter ? kRaise : kBackground));
    target->SetTransform(base);
    switch (element->kind) {
    case Kind::Header: DrawHeader(size); break;
    case Kind::Title: Write(title.Get(), element->text, whole, page.failure ? kAccent : kInk); break;
    case Kind::Text: Write(body.Get(), element->text, whole, kMuted); break;
    case Kind::Notice: DrawNotice(*element, size); break;
    case Kind::Checkbox: DrawCheckbox(*element, size, item.itemState, now); break;
    case Kind::Step: DrawStep(*element, size, now); break;
    default: DrawButton(*element, size, item.itemState, now); break;
    }
    if (target->EndDraw() == D2DERR_RECREATE_TARGET) {
        CreateTarget();
        InvalidateRect(window, nullptr, TRUE);
    }
}

ComPtr<ID2D1SolidColorBrush> Window::State::Brush(UINT32 colour, float opacity) {
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(Colour(colour, alpha * opacity), &brush);
    return brush;
}

void Window::State::Write(IDWriteTextFormat* format, const std::wstring& text, const D2D1_RECT_F& box, UINT32 colour) {
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, box, Brush(colour).Get());
}

// The logo, from the icon every Ampify file carries, at the size this screen needs; then "Ampify" and the version.
void Window::State::DrawHeader(const D2D1_SIZE_F& size) {
    if (!logo) {
        int pixels = static_cast<int>(Px(kLogo));
        HICON icon = static_cast<HICON>(
            LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICFormatConverter> converter;
        if (icon && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bitmap)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                            0, WICBitmapPaletteTypeCustom))) {
            target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &logo);
        }
        if (icon) DestroyIcon(icon);
    }
    if (logo) target->DrawBitmap(logo.Get(), D2D1::RectF(0, 0, kLogo, kLogo), alpha);
    const std::wstring name = L"Ampify";
    float nameWidth = 0;
    Measure(brand.Get(), name, 1000, &nameWidth);
    Write(brand.Get(), name, D2D1::RectF(kLogo + 10, 0, size.width, size.height), kInk);
    Write(noteMiddle.Get(), version, D2D1::RectF(kLogo + 10 + nameWidth + 8, 1, size.width, size.height), kMuted);
}

void Window::State::DrawNotice(const Element& element, const D2D1_SIZE_F& size) {
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, size.width, size.height), 10, 10),
                                 Brush(kRaise2).Get());
    // A round badge: "!" in the accent colour for a warning, a quiet "i" for something to know.
    bool warning = page.notices[element.index].warning;
    D2D1_POINT_2F centre = D2D1::Point2F(16 + 8, 12 + 9);
    target->FillEllipse(D2D1::Ellipse(centre, 8, 8), Brush(warning ? kAccent : kLineStrong).Get());
    ComPtr<ID2D1SolidColorBrush> white = Brush(kWhite);
    float barTop = warning ? -4.5f : -0.8f;  // "!": the bar, then the dot; "i": the dot, then the bar
    float dot = warning ? 3.8f : -3.8f;
    target->DrawLine(D2D1::Point2F(centre.x, centre.y + barTop), D2D1::Point2F(centre.x, centre.y + barTop + 5.3f),
                     white.Get(), 1.8f, round.Get());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centre.x, centre.y + dot), 1.1f, 1.1f), white.Get());
    Write(note.Get(), element.text, D2D1::RectF(kNoticeIndent, 12, size.width - 16, size.height - 12), kInk);
}

void Window::State::DrawCheck(const D2D1_RECT_F& box, UINT32 colour, float opacity) {
    ComPtr<ID2D1SolidColorBrush> brush = Brush(colour, opacity);
    float w = box.right - box.left;
    D2D1_POINT_2F a = D2D1::Point2F(box.left + w * 0.27f, box.top + w * 0.52f);
    D2D1_POINT_2F b = D2D1::Point2F(box.left + w * 0.44f, box.top + w * 0.68f);
    D2D1_POINT_2F c = D2D1::Point2F(box.left + w * 0.74f, box.top + w * 0.35f);
    target->DrawLine(a, b, brush.Get(), 2, round.Get());
    target->DrawLine(b, c, brush.Get(), 2, round.Get());
}

void Window::State::DrawCross(const D2D1_POINT_2F& centre, float opacity) {
    ComPtr<ID2D1SolidColorBrush> brush = Brush(kWhite, opacity);
    target->DrawLine(D2D1::Point2F(centre.x - 3.5f, centre.y - 3.5f), D2D1::Point2F(centre.x + 3.5f, centre.y + 3.5f),
                     brush.Get(), 2, round.Get());
    target->DrawLine(D2D1::Point2F(centre.x + 3.5f, centre.y - 3.5f), D2D1::Point2F(centre.x - 3.5f, centre.y + 3.5f),
                     brush.Get(), 2, round.Get());
}

void Window::State::DrawFocus(const D2D1_RECT_F& around, float radius, UINT itemState) {
    if (!ShowsFocus(itemState)) return;
    target->DrawRoundedRectangle(D2D1::RoundedRect(around, radius, radius), Brush(kInk).Get(), 1.5f);
}

// The box fills with the accent and the tick grows in as it is ticked; both go back the same way.
void Window::State::DrawCheckbox(const Element& element, const D2D1_SIZE_F& size, UINT itemState, std::uint64_t now) {
    D2D1_RECT_F mark = D2D1::RectF(kFocusGap, kFocusGap + 1, kFocusGap + kMark, kFocusGap + 1 + kMark);
    float ticked = element.check.At(now);
    float hovered = element.hover.At(now);
    target->FillRoundedRectangle(D2D1::RoundedRect(mark, 5, 5), Brush(Mix(kRaise2, kButton, ticked)).Get());
    target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(mark, 0.75f), 4.5f, 4.5f),
                                 Brush(Mix(kLineStrong, kMuted, hovered), 1 - ticked).Get(), 1.5f);
    if (ticked > 0) {
        float scale = animate ? 0.8f + 0.2f * ticked : 1.0f;
        D2D1_POINT_2F centre = D2D1::Point2F((mark.left + mark.right) / 2, (mark.top + mark.bottom) / 2);
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, centre) * base);
        DrawCheck(mark, kWhite, ticked);
        target->SetTransform(base);
    }
    DrawFocus(Inset(mark, -3), 7, itemState);
    Write(body.Get(), element.text, D2D1::RectF(kFocusGap + kMark + 12, kFocusGap, size.width, size.height), kInk);
}

// A quarter of a ring turning at a steady pace: steady is what says "still working".
void Window::State::DrawSpinner(const D2D1_POINT_2F& centre, float radius, std::uint64_t now) {
    constexpr float kTurn = 6.2831853f;
    float start = animate ? kTurn * static_cast<float>(now % kSpinTurnMs) / static_cast<float>(kSpinTurnMs) : -1.57f;
    float end = start + 1.9f;
    ComPtr<ID2D1PathGeometry> arc;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2d->CreatePathGeometry(&arc)) || FAILED(arc->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(centre.x + radius * std::cos(start), centre.y + radius * std::sin(start)),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(centre.x + radius * std::cos(end), centre.y + radius * std::sin(end)),
                                  D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    target->DrawGeometry(arc.Get(), Brush(kAccent).Get(), 2, round.Get());
}

// A step: an empty ring before it starts, a turning arc while it runs, then a green tick or a red cross that settles in
// as the ring fades out.
void Window::State::DrawStep(const Element& element, const D2D1_SIZE_F& size, std::uint64_t now) {
    StepState state = steps[element.index];
    D2D1_POINT_2F centre = D2D1::Point2F(kMark / 2, size.height / 2);
    float radius = kMark / 2 - 1;
    bool settled = state == StepState::Done || state == StepState::Failed;
    float mark = settled ? element.mark.At(now) : 0;
    if (mark < 1) {
        target->DrawEllipse(D2D1::Ellipse(centre, radius, radius), Brush(kLineStrong, 1 - mark).Get(), 1.5f);
    }
    if (state == StepState::Running) DrawSpinner(centre, radius, now);
    if (settled && mark > 0) {
        float scale = animate ? 0.6f + 0.4f * mark : 1.0f;
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, centre) * base);
        bool done = state == StepState::Done;
        target->FillEllipse(D2D1::Ellipse(centre, radius + 1, radius + 1), Brush(done ? kOk : kAccent, mark).Get());
        if (done) {
            DrawCheck(D2D1::RectF(0, centre.y - kMark / 2, kMark, centre.y + kMark / 2), kOkInk, mark);
        } else {
            DrawCross(centre, mark);
        }
        target->SetTransform(base);
    }
    Write(bodyMiddle.Get(), element.text, D2D1::RectF(kMark + 14, 0, size.width, size.height),
          state == StepState::Pending ? kMuted : kInk);
}

// The colour follows the pointer in and out; a press shows at once, as feedback must.
void Window::State::DrawButton(const Element& element, const D2D1_SIZE_F& size, UINT itemState, std::uint64_t now) {
    bool primary = element.kind == Kind::Primary;
    bool pressed = (itemState & ODS_SELECTED) != 0;
    float hovered = element.hover.At(now);
    D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(Inset(D2D1::RectF(0, 0, size.width, size.height), kFocusGap), 8, 8);
    UINT32 fill = primary ? (pressed ? kButtonPressed : Mix(kButton, kButtonHover, hovered))
                          : (pressed ? kLine : Mix(kRaise, kRaise2, hovered));
    target->FillRoundedRectangle(shape, Brush(fill).Get());
    if (!primary) {
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(shape.rect, 0.5f), 7.5f, 7.5f), Brush(kLineStrong).Get(), 1);
    }
    DrawFocus(Inset(shape.rect, -2.5f), 10, itemState);
    Write(label.Get(), element.text, shape.rect, primary ? kWhite : kInk);
}

}  // namespace ampify::setup
