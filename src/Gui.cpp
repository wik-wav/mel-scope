
// Gui.cpp -- Win32 window with a GDI+ drawn control panel.
#include "Fonts.h"
#include "Gui.h"

#include "Colormaps.h"
#include "MelCore.h"

#include <commdlg.h>
#include <cwctype>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace melscope {

namespace {

// ------------------------------------------------------------------ GDI+ setup
struct GdiplusInit {
    ULONG_PTR token = 0;
    GdiplusInit() { GdiplusStartupInput in; GdiplusStartup(&token, &in, nullptr); }
    // Deliberately never shut GDI+ down. This object is a function-local static, so its
    // destructor runs during DLL_PROCESS_DETACH where GdiplusShutdown blocks on the
    // loader lock - that hung plugin unload (found by the VST3 host test). The process
    // reclaims the token on exit anyway.
    ~GdiplusInit() {}
};
GdiplusInit& gp() { static GdiplusInit i; return i; }

// NOTE: (r, g, b, a) -- not (a, r, g, b). Pass three arguments for opaque.
inline ARGB argb(int r, int g, int b, int a = 255) { return ((ARGB)a << 24) | ((ARGB)r << 16) | ((ARGB)g << 8) | (ARGB)b; }

// Light design tokens (see README): background / surface / surface-muted /
// foreground / muted / border / accent / success.
// Palette: swapped by applyTheme() when the Theme parameter changes.
// Palette from the design system: paper / surface / rule / ink, one saturated
// accent plus a yellow "mark" used for the active state.
ARGB kBg = argb(0xf7, 0xf8, 0xf7);        // paper
ARGB kSurface = argb(0xff, 0xff, 0xff);
ARGB kSurfaceMuted = argb(0xef, 0xf0, 0xee);
ARGB kText = argb(0x0d, 0x0e, 0x1c);      // ink
ARGB kMuted = argb(0x6c, 0x70, 0x76);
ARGB kBorder = argb(0xd7, 0xd9, 0xd7);    // rule
ARGB kAccent = argb(0x14, 0x20, 0xca);    // accent
ARGB kAccentInk = argb(0x0f, 0x18, 0x9c);
ARGB kSuccess = argb(0x2f, 0x74, 0x66);   // accent-2, darkened for small text
ARGB kTrackBg = argb(0xd7, 0xd9, 0xd7);
ARGB kCardEdge = argb(0x0d, 0x0e, 0x1c);  // card frame: ink on paper
ARGB kDisabled = argb(0xae, 0xb2, 0xb8);  // a control that cannot act right now
ARGB kMark = argb(0xf3, 0xda, 0x34);      // mark: the active-state fill
ARGB kOnMark = argb(0x0d, 0x0e, 0x1c);    // text on top of the mark
ARGB kInk = kText;                        // alias used by the chrome helpers
ARGB kPaper = kBg;
ARGB kShadow = argb(0x0d, 0x0e, 0x1c, 31); // 12 % ink, hard offset, no blur
// piano keys keep their own colours: chrome must not turn them orange
ARGB kKeyWhite = argb(0xff, 0xff, 0xff);
ARGB kKeyBlack = argb(0x0d, 0x0e, 0x1c);

void applyTheme(int theme)
{
    if (theme >= 1) {
        // dark: the original look, warm accent on near-black chrome
        kBg = argb(0x13, 0x13, 0x18);
        kSurface = argb(0x1c, 0x1c, 0x22);
        kSurfaceMuted = argb(0x26, 0x26, 0x2e);
        kText = argb(0xe9, 0xe7, 0xf1);
        kMuted = argb(0x9a, 0x97, 0xa8);
        kBorder = argb(0x32, 0x32, 0x3c);
        kAccent = argb(0xff, 0x8a, 0x4c);
        kSuccess = argb(0x86, 0xc0, 0x84);
        kTrackBg = argb(0x3a, 0x3a, 0x45);
        kShadow = argb(0, 0, 0, 140);
        kMark = argb(0xf3, 0xda, 0x34);
        kOnMark = argb(0x13, 0x13, 0x18);
        kCardEdge = argb(0x3a, 0x3a, 0x45);
        kDisabled = argb(0x5c, 0x60, 0x68);
        kAccentInk = argb(0xc3, 0xcb, 0xff);
        kKeyWhite = argb(0xe8, 0xe5, 0xee);
        kKeyBlack = argb(0x10, 0x10, 0x16);
    } else {
        kBg = argb(0xf7, 0xf8, 0xf7);
        kSurface = argb(0xff, 0xff, 0xff);
        kSurfaceMuted = argb(0xef, 0xf0, 0xee);
        kText = argb(0x0d, 0x0e, 0x1c);
        kMuted = argb(0x6c, 0x70, 0x76);
        kBorder = argb(0xd7, 0xd9, 0xd7);
        kAccent = argb(0x14, 0x20, 0xca);
        kAccentInk = argb(0x0f, 0x18, 0x9c);
        kSuccess = argb(0x86, 0xc0, 0x84);
        kTrackBg = argb(0xd7, 0xd9, 0xd7);
        kMark = argb(0xf3, 0xda, 0x34);
        kOnMark = argb(0x0d, 0x0e, 0x1c);
        kCardEdge = argb(0x0d, 0x0e, 0x1c);
        kDisabled = argb(0xae, 0xb2, 0xb8);
        kShadow = argb(0x0d, 0x0e, 0x1c, 31);
        kKeyWhite = argb(0xff, 0xff, 0xff);
        kKeyBlack = argb(0x0d, 0x0e, 0x1c);
    }
    kInk = kText;
    kPaper = kBg;
}
// overlay colours for things drawn ON the image: they flip with the colormap so
// labels stay readable on the light maps as well as the dark ones
const ARGB kOverlayDarkInk = argb(0x0d, 0x0e, 0x1c);
const ARGB kOverlayLightInk = argb(0xff, 0xff, 0xff);

// UTF-8 (the UI strings use -E and ?) to wide, byte-by-byte would mojibake.
std::wstring widen(const char* s)
{
    std::wstring out;
    if (!s) return out;
    while (*s) {
        const unsigned char c = (unsigned char)*s++;
        if (c < 0x80) {
            out.push_back((wchar_t)c);
        } else if ((c & 0xE0) == 0xC0) {
            const unsigned char c2 = (unsigned char)(*s ? *s++ : 0);
            out.push_back((wchar_t)(((c & 0x1Fu) << 6) | (c2 & 0x3Fu)));
        } else if ((c & 0xF0) == 0xE0) {
            const unsigned char c2 = (unsigned char)(*s ? *s++ : 0);
            const unsigned char c3 = (unsigned char)(*s ? *s++ : 0);
            out.push_back((wchar_t)(((c & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu)));
        } else if ((c & 0xF8) == 0xF0) {
            const unsigned char c2 = (unsigned char)(*s ? *s++ : 0);
            const unsigned char c3 = (unsigned char)(*s ? *s++ : 0);
            const unsigned char c4 = (unsigned char)(*s ? *s++ : 0);
            const unsigned cp = ((unsigned)(c & 0x07u) << 18) | ((unsigned)(c2 & 0x3Fu) << 12)
                                | ((unsigned)(c3 & 0x3Fu) << 6) | (unsigned)(c4 & 0x3Fu);
            if (cp >= 0x10000u) {
                const unsigned v = cp - 0x10000u;
                out.push_back((wchar_t)(0xD800 + (v >> 10)));
                out.push_back((wchar_t)(0xDC00 + (v & 0x3FFu)));
            } else {
                out.push_back((wchar_t)cp);
            }
        } else {
            out.push_back(L'?');
        }
    }
    return out;
}

void roundRectPath(GraphicsPath& p, RectF r, float radius)
{
    const float d = radius * 2.0f;
    if (radius <= 0.5f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

// The design system has no rounded corners: every chrome shape is a rectangle
// with a 1px edge. The radius argument is kept so call sites stay unchanged.
void fillRound(Graphics& g, RectF r, float radius, ARGB colour)
{
    (void)radius;
    SolidBrush b(colour);
    g.FillRectangle(&b, r);
}

void strokeRound(Graphics& g, RectF r, float radius, ARGB colour, float w = 1.0f)
{
    (void)radius;
    Pen pen(Color(colour), w);
    g.DrawRectangle(&pen, r.X + 0.5f, r.Y + 0.5f, r.Width - 1.0f, r.Height - 1.0f);
}

// Hard offset shadow (10px 10px 0), the system's only elevation.
void dropShadow(Graphics& g, RectF r, float s)
{
    fillRound(g, RectF(r.X + 6.0f * s, r.Y + 6.0f * s, r.Width, r.Height), 0.0f, kShadow);
}

void drawText(Graphics& g, const std::wstring& s, RectF r, Font* f, ARGB colour,
              StringAlignment align = StringAlignmentNear, StringAlignment valign = StringAlignmentCenter)
{
    if (!f) return;
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(valign);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(colour);
    g.DrawString(s.c_str(), -1, f, r, &fmt, &b);
}

// Chrome button: 1px ink edge, sharp, uppercase mono label. Hover inverts to ink;
// the active state fills with ink and switches the label to the warm mark colour.
void chromeButton(Graphics& g, RectF r, const wchar_t* label, Font* f, bool hover, bool active)
{
    const ARGB fill = active ? kMark : (hover ? kInk : kSurface);
    const ARGB ink = active ? kOnMark : (hover ? kPaper : kInk);
    fillRound(g, r, 0.0f, fill);
    strokeRound(g, r, 0.0f, kInk);
    drawText(g, label, r, f, ink, StringAlignmentCenter);
}

std::wstring upper(const wchar_t* in)
{
    std::wstring out;
    for (const wchar_t* p = in; p && *p; ++p) out.push_back((wchar_t)std::towupper(*p));
    return out;
}

std::wstring upper(const char* in)
{
    std::wstring out;
    for (const char* p = in; p && *p; ++p) out.push_back((wchar_t)std::towupper((wint_t)(unsigned char)*p));
    return out;
}

// Draws text with a 1px dark drop shadow so it stays readable over the image.
void drawTextShadow(Graphics& g, const std::wstring& s, RectF r, Font* f, ARGB colour,
                    StringAlignment align = StringAlignmentNear,
                    ARGB shadowColour = argb(0, 0, 0, 190))
{
    RectF shadow(r.X + 1.0f, r.Y + 1.0f, r.Width, r.Height);
    drawText(g, s, shadow, f, shadowColour, align);
    drawText(g, s, r, f, colour, align);
}

std::string hzText(float hz)
{
    char buf[64];
    if (hz >= 1000.0f) {
        const float k = hz / 1000.0f;
        std::snprintf(buf, sizeof buf, (k < 10.0f ? "%.2f kHz" : "%.1f kHz"), k);
    } else {
        std::snprintf(buf, sizeof buf, "%.0f Hz", hz);
    }
    return buf;
}

bool isBlackKey(int note)
{
    const int n = ((note % 12) + 12) % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

float noteToHz(float note) { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }

float hzToNote(float hz) { return 69.0f + 12.0f * std::log2(std::max(1e-3f, hz) / 440.0f); }

const char* noteName(int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static char buf[8];
    const int n = ((note % 12) + 12) % 12;
    std::snprintf(buf, sizeof buf, "%s%d", names[n], note / 12 - 1);
    return buf;
}

std::string shortHzText(float hz)
{
    char buf[32];
    if (hz >= 1000.0f) std::snprintf(buf, sizeof buf, hz >= 10000.0f ? "%.0fk" : "%.2fk", hz / 1000.0f);
    else std::snprintf(buf, sizeof buf, hz >= 10.0f ? "%.0f" : "%.1f", hz);
    return buf;
}

} // namespace

// ------------------------------------------------------------------- resources
struct GdiResources {
    // The system's three faces: serif display, sans body, mono micro-labels.
    // Merriweather ships inside the binary (OFL), with Georgia as the fallback.
    FontFamily family { L"Segoe UI" };
    FontFamily serif { L"Georgia" };
    FontFamily mono { L"Consolas" };
    PrivateFontCollection displayPfc;
    std::unique_ptr<FontFamily> display;
    bool displayTried = false;
    std::unique_ptr<Font> tiny, small, body, value, title, group;
    float builtScale = 0;

    void loadDisplay()
    {
        if (displayTried) return;
        displayTried = true;
        if (mel::kDisplayFontBytes == 0) return;
        displayPfc.AddMemoryFont((const void*)mel::kDisplayFont, (INT)mel::kDisplayFontBytes);
        INT count = displayPfc.GetFamilyCount();
        if (count <= 0) return;
        FontFamily found[4];
        INT got = 0;
        displayPfc.GetFamilies(4, found, &got);
        if (got > 0) {
            WCHAR name[LF_FACESIZE] = {};
            found[0].GetFamilyName(name);
            display.reset(new FontFamily(name, &displayPfc));
        }
    }

    void ensure(float s)
    {
        loadDisplay();
        if (builtScale == s && body) return;
        builtScale = s;
        tiny.reset(new Font(&mono, 10.0f * s, FontStyleRegular, UnitPixel));
        small.reset(new Font(&family, 11.0f * s, FontStyleRegular, UnitPixel));
        body.reset(new Font(&family, 12.0f * s, FontStyleRegular, UnitPixel));
        value.reset(new Font(&mono, 11.5f * s, FontStyleRegular, UnitPixel));
        title.reset(new Font(display ? (const FontFamily*)display.get() : &serif, 16.0f * s,
                            FontStyleRegular, UnitPixel));
        group.reset(new Font(&mono, 9.5f * s, FontStyleBold, UnitPixel));
    }
};

// ---------------------------------------------------------------------- window
Gui::Gui(App& app) : app_(app) { gp(); }
Gui::~Gui() { destroy(); }

LRESULT CALLBACK Gui::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Gui* self = (Gui*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (Gui*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    }
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Gui::create(HWND parent, int width, int height)
{
    if (hwnd_) return true;
    wantW_ = width;
    wantH_ = height;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_DBLCLKS;
        wc.lpfnWndProc = &Gui::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"MelScopeEditor";
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }

    if (app_.editorWidth() > 0 && app_.editorHeight() > 0) {
        wantW_ = app_.editorWidth();
        wantH_ = app_.editorHeight();
    }
    RECT pr;
    GetClientRect(parent, &pr);
    (void)pr;
    hwnd_ = CreateWindowExW(0, L"MelScopeEditor", L"Mel Scope", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, wantW_, wantH_, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    SetTimer(hwnd_, 1, 15, nullptr);
    layout();
    return true;
}

void Gui::destroy()
{
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (oldBitmap_ && memDC_) SelectObject(memDC_, oldBitmap_);
    if (bitmap_) { DeleteObject(bitmap_); bitmap_ = nullptr; }
    if (memDC_) { DeleteDC(memDC_); memDC_ = nullptr; }
    pixels_ = nullptr;
    bufW_ = bufH_ = 0;
}

void Gui::show()
{
    if (hwnd_) ShowWindow(hwnd_, SW_SHOW);
}

void Gui::hide()
{
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void Gui::setScale(double s)
{
    if (s > 0.1) hostScale_ = (float)s;
    applyUserScale();
    layout();
    dirty_ = true;
}

// The interface is drawn at the host's DPI scale multiplied by the UI scale
// parameter, so the same build stays readable on a dense display.
void Gui::applyUserScale()
{
    const float user = (float)app_.getInt(mel::pUiScale) / 100.0f;
    const float eff = mel::clampf(hostScale_ * user, 0.5f, 3.0f);
    if (std::fabs(eff - scale_) < 1e-4f) return;
    scale_ = eff;
    layout();
    dirty_ = true;
    // Ask the host for a window that fits, but only once the user stops clicking:
    // a resize round-trip per 5 % step is what made the stepper feel sluggish.
    scaleResizePending_ = true;
    scaleResizeAt_ = GetTickCount64() + 350;
}

void Gui::setSize(int width, int height)
{
    width = std::max(320, width);
    height = std::max(240, height);
    wantW_ = width;
    wantH_ = height;
    if (hwnd_) SetWindowPos(hwnd_, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    width_ = width;
    height_ = height;
    ensureBuffer();
    layout();
    dirty_ = true;
}

void Gui::ensureBuffer()
{
    if (width_ <= 0 || height_ <= 0) return;
    if (bufW_ == width_ && bufH_ == height_ && memDC_) return;

    HDC screen = GetDC(hwnd_ ? hwnd_ : nullptr);
    if (memDC_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        oldBitmap_ = nullptr;
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (bitmap_) { DeleteObject(bitmap_); bitmap_ = nullptr; }

    memDC_ = CreateCompatibleDC(screen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap_) oldBitmap_ = (HBITMAP)SelectObject(memDC_, bitmap_);
    pixels_ = bits;
    bufW_ = width_;
    bufH_ = height_;
    ReleaseDC(hwnd_ ? hwnd_ : nullptr, screen);
}

// ---------------------------------------------------------------------- layout
void Gui::layout()
{
    const float s = scale_;
    pad_ = (int)std::lround(14 * s);
    headerH_ = (int)std::lround(44 * s);
    panelW_ = (int)std::lround(292 * s);

    width_ = wantW_;
    height_ = wantH_;
    panelX_ = pad_; // drawer opens from the left
    panelY_ = headerH_;
    panelH_ = height_ - headerH_ - pad_;
    keysW_ = (int)std::lround(46 * s);
    plotX_ = pad_ + keysW_ + (int)std::lround(6 * s);
    plotY_ = headerH_;
    plotW_ = std::max(80, width_ - pad_ - plotX_);
    plotH_ = std::max(60, height_ - plotY_ - pad_);

    gearW_ = (int)std::lround(88 * s);
    gearH_ = (int)std::lround(23 * s);
    gearX_ = pad_;
    gearY_ = (headerH_ - gearH_) / 2 - (int)std::lround(4 * s);
    freezeW_ = (int)std::lround(84 * s);
    freezeH_ = gearH_;
    freezeX_ = width_ - pad_ - freezeW_;
    freezeY_ = gearY_;
    rangeW_ = (int)std::lround(92 * s);
    rangeH_ = freezeH_;
    rangeX_ = freezeX_ - (int)std::lround(8 * s) - rangeW_;
    rangeY_ = freezeY_;
    helpW_ = (int)std::lround(64 * s);
    helpH_ = freezeH_;
    helpX_ = rangeX_ - (int)std::lround(8 * s) - helpW_;
    helpY_ = freezeY_;

    const auto& defs = mel::paramDefs();
    // Display order, independent of id order: Peak range belongs beside Spectrum range
    // rather than at the end of View, but moving it in the id table would shift the ids
    // of everything after it and reinterpret saved state.
    static const int kPanelOrder[] = {
        mel::pFft,    mel::pOverlap, mel::pBands,   mel::pWindow, mel::pChannel, mel::pFmin,
        mel::pFmax,   mel::pRange,   mel::pLineRange, mel::pCeil, mel::pAutoCeil,
        mel::pRefOn,  mel::pRefLevel, mel::pRefWash, mel::pRefRange, mel::pRefInk, mel::pRefBlend,
        mel::pSpan,
        mel::pMode,   mel::pTheme,   mel::pAxis,    mel::pCmap,   mel::pGamma,  mel::pTilt,
        mel::pGlow,   mel::pGrain,   mel::pSmooth,  mel::pKeyLines, mel::pFreeze, mel::pUiScale, mel::pNotes,
        mel::pLegend,
    };
    static_assert(sizeof(kPanelOrder) / sizeof(kPanelOrder[0]) == (size_t)mel::pCount,
                  "the panel order must list every parameter exactly once");
    int nParams = 0, nGroups = 0;
    const char* lastGroup = nullptr;
    for (int pid : kPanelOrder) {
        const char* grp = defs[(size_t)pid].group;
        if (!lastGroup || std::strcmp(lastGroup, grp) != 0) { ++nGroups; lastGroup = grp; }
        ++nParams;
    }

    float groupH = 28.0f * s, rowH = 26.0f * s, buttonH = 30.0f * s;
    const float topPad = 12.0f * s;
    const float buttonGap = 6.0f * s;
    const float listGap = 8.0f * s;   // air between the last row and the buttons
    const float bottomReserve = listGap + buttonH * 2.0f + buttonGap + 12.0f * s;
    const float avail = (float)panelH_;
    float total = topPad + nGroups * groupH + nParams * rowH + bottomReserve;
    if (total > avail && nParams > 0)
        rowH = std::max(20.0f * s, (avail - topPad - nGroups * groupH - bottomReserve) / (float)nParams);
    total = topPad + nGroups * groupH + nParams * rowH + bottomReserve;
    // The card hugs its content: never taller than needed, never taller than the
    // window. Rows that do not fit scroll inside the list area instead of sliding
    // under the buttons.
    panelH_ = (int)std::lround(std::min(avail, total));
    rowH_ = (int)std::lround(rowH);
    groupH_ = (int)std::lround(groupH);
    buttonH_ = (int)std::lround(buttonH);

    rows_.clear();
    // scrolling must reveal later rows: content moves UP as scrollY_ grows
    float y = (float)panelY_ + topPad - scrollY_;
    lastGroup = nullptr;
    for (int pid : kPanelOrder) {
        const auto& d = defs[(size_t)pid];
        if (!lastGroup || std::strcmp(lastGroup, d.group) != 0) {
            Row r;
            r.pid = -1;
            r.title = d.group;
            r.y = y;
            r.h = groupH;
            rows_.push_back(r);
            y += groupH;
            lastGroup = d.group;
        }
        Row r;
        r.pid = pid;
        r.y = y;
        r.h = rowH;
        rows_.push_back(r);
        y += rowH;
    }
    // rows are placed at panelY + topPad - scrollY, so undo that offset here:
    // the content height must not depend on how far the list is scrolled.
    contentH_ = (y + scrollY_) - (float)panelY_ - topPad;
    const float viewH = (float)panelH_ - topPad - bottomReserve;
    const float maxScroll = std::max(0.0f, contentH_ - viewH);
    if (scrollY_ > maxScroll) {
        scrollY_ = maxScroll;
        if (maxScroll > 0.0f) layout(); // re-place rows at the clamped offset
        return;
    }
    if (scrollY_ < 0.0f) {
        scrollY_ = 0.0f;
        layout();
        return;
    }
    buttonY_ = (int)std::lround((float)(panelY_ + panelH_) - bottomReserve + listGap);
    listBottom_ = buttonY_ - (int)std::lround(6.0f * s);
    buttonH_ = (int)std::lround(buttonH);
}

// While frozen the image is held: a parameter that would rebuild the mel bank
// (and therefore blank the held image) is disabled instead of silently wiping it.
bool Gui::rowBlocked(int pid) const
{
    // the peak-line range means nothing while the plain spectrum is showing
    if (pid == mel::pLineRange && app_.getInt(mel::pMode) == 0) return true;
    if (!app_.getBool(mel::pFreeze)) return false;
    switch (pid) {
    case mel::pFft:
    case mel::pOverlap:
    case mel::pBands:
    case mel::pWindow:
    case mel::pFmin:
    case mel::pFmax:
    case mel::pSmooth:
        return true;
    default:
        return false;
    }
}

// Geometry of the UI-scale stepper inside a row. Shared by the drawing and the
// click handling so they cannot drift apart.
void Gui::stepperRects(float rowY, float rowH, float innerX, float innerW, float* out) const
{
    // out: minus.x, minus.y, minus.w, minus.h, value.x, value.y, value.w, value.h,
    //      plus.x, plus.y, plus.w, plus.h
    const float s = scale_;
    const float bw = 26.0f * s, bh = std::min(rowH - 4.0f * s, 20.0f * s);
    const float by = rowY + (rowH - bh) * 0.5f;
    const float plusX = innerX + innerW - bw;
    const float valueX = plusX - 74.0f * s;
    const float minusX = valueX - bw - 4.0f * s;
    out[0] = minusX; out[1] = by; out[2] = bw; out[3] = bh;
    out[4] = valueX; out[5] = rowY; out[6] = 74.0f * s - 6.0f * s; out[7] = rowH;
    out[8] = plusX;  out[9] = by; out[10] = bw; out[11] = bh;
}

bool Gui::buttonRect(int index, float& x, float& y, float& w, float& h) const
{
    if (index < 0 || index > 3) return false;
    const float s = scale_;
    const float innerX = (float)panelX_ + 14.0f * s;
    const float innerW = (float)panelW_ - 28.0f * s;
    w = (innerW - 8.0f * s) * 0.5f;
    h = (float)buttonH_;
    const int row = index / 2, col = index % 2;
    x = innerX + (float)col * (w + 8.0f * s);
    y = (float)buttonY_ + (float)row * (h + 6.0f * s);
    return true;
}

int Gui::rowAt(int x, int y) const
{
    if (!showPanel_) return -1;
    if (x < panelX_ || x > panelX_ + panelW_) return -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].pid < 0) continue;
        if ((float)y >= rows_[i].y && (float)y < rows_[i].y + rows_[i].h) return (int)i;
    }
    return -1;
}

int Gui::dropdownItemAt(int x, int y) const
{
    if (!dropdown_.open) return -1;
    if ((float)x < dropdown_.x || (float)x > dropdown_.x + dropdown_.w) return -1;
    const int n = (int)dropdown_.items.size();
    for (int i = 0; i < n; ++i) {
        const float iy = dropdown_.y + 6.0f * scale_ + i * dropdown_.itemH;
        if ((float)y >= iy && (float)y < iy + dropdown_.itemH) return i;
    }
    return -1;
}

void Gui::openDropdown(int pid)
{
    const auto& def = mel::paramDefs()[(size_t)pid];
    if (def.choices.empty()) return;
    dropdown_.open = true;
    dropdown_.pid = pid;
    // Only offer the maps that belong to the current theme (Ice fits both).
    dropdown_.items.clear();
    const bool dark = app_.getInt(mel::pTheme) >= 1;
    for (int i = 0; i < (int)def.choices.size(); ++i) {
        if (pid == mel::pCmap && !mel::mapFitsTheme(i, dark)) continue;
        dropdown_.items.push_back(i);
    }
    if (dropdown_.items.empty()) {
        for (int i = 0; i < (int)def.choices.size(); ++i) dropdown_.items.push_back(i);
    }
    dropdown_.hover = 0;
    for (int i = 0; i < (int)dropdown_.items.size(); ++i)
        if ((int)app_.get(pid) == dropdown_.items[(size_t)i]) dropdown_.hover = i;
    dropdown_.x = (float)panelX_;
    dropdown_.w = (float)panelW_;
    dropdown_.itemH = std::max(20.0f * scale_, 22.0f * scale_);
    float yTop = 0;
    for (const auto& r : rows_)
        if (r.pid == pid) yTop = r.y + r.h;
    const float h = dropdown_.items.size() * dropdown_.itemH + 12.0f * scale_;
    if (yTop + h > (float)(panelY_ + panelH_)) yTop = yTop - h - 2.0f * scale_;
    dropdown_.y = std::max(yTop, 4.0f * scale_);
    dirty_ = true;
}

void Gui::closeDropdown()
{
    dropdown_.open = false;
    dropdown_.pid = -1;
    dirty_ = true;
}

void Gui::beginDrag(int pid, int mx)
{
    if (rowBlocked(pid)) return;
    dragPid_ = pid;
    dragStartX_ = mx;
    dragStartValue_ = app_.get(pid);
}

void Gui::updateDrag(int mx)
{
    if (dragPid_ < 0) return;
    const auto& def = mel::paramDefs()[(size_t)dragPid_];
    float dx = (float)(mx - dragStartX_) / (170.0f * scale_);
    if (GetKeyState(VK_SHIFT) & 0x8000) dx *= 0.2f;
    float v;
    if (def.logarithmic && def.min > 0.0f) {
        const float k = std::pow(def.max / def.min, dx);
        v = dragStartValue_ * k;
    } else {
        v = dragStartValue_ + dx * (def.max - def.min);
    }
    v = std::max(def.min, std::min(def.max, v));
    app_.set(dragPid_, v);
    dirty_ = true;
}

void Gui::formatValue(int pid, float v, char* out, int cap) const
{
    const auto& def = mel::paramDefs()[(size_t)pid];
    if (!def.choices.empty()) {
        int i = (int)(v + 0.5f);
        i = std::max(0, std::min((int)def.choices.size() - 1, i));
        std::snprintf(out, (size_t)cap, "%s", def.choices[(size_t)i]);
        return;
    }
    const std::string suffix = def.suffix ? def.suffix : "";
    if (pid == mel::pFmin || pid == mel::pFmax) {
        std::snprintf(out, (size_t)cap, "%s", hzText(v).c_str());
        return;
    }
    if (pid == mel::pSpan) {
        if (v < 10.0f) std::snprintf(out, (size_t)cap, "%.2f s", v);
        else std::snprintf(out, (size_t)cap, "%.1f s", v);
        return;
    }
    if (pid == mel::pTilt) {
        std::snprintf(out, (size_t)cap, "%+.1f dB/oct", v);
        return;
    }
    if (pid == mel::pRange || pid == mel::pCeil) {
        std::snprintf(out, (size_t)cap, "%.0f dB", v);
        return;
    }
    if (pid == mel::pGlow || pid == mel::pSmooth || pid == mel::pKeyLines || pid == mel::pGrain) {
        std::snprintf(out, (size_t)cap, "%.0f%%", v * 100.0f);
        return;
    }
    if (pid == mel::pGamma) {
        std::snprintf(out, (size_t)cap, "%.2f", v);
        return;
    }
    std::snprintf(out, (size_t)cap, "%.0f%s", v, suffix.c_str());
}

// ----------------------------------------------------------------------- paint
void Gui::paint()
{
    if (!hwnd_) return;
    // The client rect is the truth: host-driven resizes and creation-time
    // messages can otherwise leave wantW_/wantH_ out of sync with the window.
    {
        RECT rc;
        if (GetClientRect(hwnd_, &rc) && rc.right > 0 && rc.bottom > 0) {
            if (rc.right != width_ || rc.bottom != height_) {
                width_ = rc.right;
                height_ = rc.bottom;
                wantW_ = width_;
                wantH_ = height_;
                ensureBuffer();
                layout();
            }
        }
    }
    ensureBuffer();
    if (!memDC_ || !pixels_) return;

    applyTheme(app_.getInt(mel::pTheme));
    applyUserScale();
    if (!res_) res_.reset(new GdiResources());
    res_->ensure(scale_);

    {
        const double wall = (double)GetTickCount64() / 1000.0;
        if (lastPaintMs_ > 0.0 && wall > lastPaintMs_ + 1e-4) {
            const double inst = 1.0 / (wall - lastPaintMs_);
            fps_ = fps_ > 0.0 ? (fps_ * 0.85 + inst * 0.15) : inst;
        }
        lastPaintMs_ = wall;
    }

    // The analysis thread only holds this lock for a few hundred microseconds,
    // so holding it across the paint keeps the bank/frame data consistent.
    std::lock_guard<std::mutex> lk(app_.engine.lock());

    // ---- 1. background + spectrogram, written straight into the DIB
    uint32_t* px = (uint32_t*)pixels_;
    std::fill(px, px + (size_t)bufW_ * (size_t)bufH_, (uint32_t)kBg);

    mel::ViewParams vp = app_.viewParams();
    // played notes are snapshotted here, on the UI thread, so the renderer never touches
    // the note trace's lock
    app_.notes.snapshot(notes_);
    vp.notes = notes_.empty() ? nullptr : notes_.data();
    vp.noteCount = (int)notes_.size();
    vp.noteLevel = app_.get(mel::pNotes) / 100.0f;
    vp.refInk = app_.getInt(mel::pRefInk);
    vp.refBlend = app_.getInt(mel::pRefBlend);
    vp.refWash = app_.get(mel::pRefWash) / 100.0f;
    vp.refRange = app_.get(mel::pRefRange);
    vp.refStrength = app_.get(mel::pRefLevel) / 100.0f;
    if (app_.getBool(mel::pRefOn)) {
        const mel::FrameStore& rf = app_.engineRef.frames();
        if (rf.count > 1) vp.refFrames = &rf;
    }
    curAxis_ = vp.axis;
    {
        const int cm = std::max(0, std::min(mel::kNumMaps - 1, vp.cmap));
        const bool rev = (vp.light != 0) && (mel::kMapIsLight[cm] == 0);
        const unsigned char* c0 = mel::kMaps[cm][rev ? 255 : 0];
        plotLight_ = (30 * (int)c0[0] + 59 * (int)c0[1] + 11 * (int)c0[2]) / 100 > 128;
    }
    const ARGB inkOnPlot = plotLight_ ? kOverlayDarkInk : kOverlayLightInk;
    const ARGB inkShadow = plotLight_ ? argb(255, 255, 255, 200) : argb(0, 0, 0, 190);
    const ARGB gridOnPlot = plotLight_ ? argb(13, 14, 28, 30) : argb(255, 255, 255, 20);
    float fmin = 0.0f, fmax = 0.0f;
    int bands = 0;
    {
        const mel::MelBank& bank = app_.engine.bank();
        // The analysed range is what the mel bank covers; the VIEW may be a
        // sub-window of it. Zooming only moves the view, so it never rebuilds the
        // bank and never throws the history away.
        if (bankFmin_ != bank.fmin || bankFmax_ != bank.fmax || !viewReady_) {
            bankFmin_ = bank.fmin;
            bankFmax_ = bank.fmax;
            if (!keepView_) {
                viewFmin_ = bank.fmin;
                viewFmax_ = bank.fmax;
            }
            // a zoom stored with the project wins over the plain band, as long as it
            // still lies inside what is analysed
            if (!keepView_ && app_.haveViewRange() && app_.viewLow() >= bank.fmin * 0.999f
                && app_.viewHigh() <= bank.fmax * 1.001f && app_.viewHigh() > app_.viewLow()) {
                viewFmin_ = app_.viewLow();
                viewFmax_ = app_.viewHigh();
            }
            app_.setViewRange(viewFmin_, viewFmax_);
            keepView_ = false;
            viewReady_ = true;
            reqFmin_ = bank.fmin;
            reqFmax_ = bank.fmax;
        }
        clampView();
        vp.fmin = viewFmin_;
        vp.fmax = viewFmax_;
        fmin = viewFmin_;
        fmax = viewFmax_;
        bands = bank.bands;
        shownFmin_ = viewFmin_;
        shownFmax_ = viewFmax_;
        shownBands_ = bank.bands;
        shownFft_ = app_.engine.config().fftSize;
        shownHop_ = app_.engine.config().hopSize();
        if (plotW_ > 2 && plotH_ > 2)
            mel::renderSpectrogram(px + (size_t)plotY_ * (size_t)bufW_ + (size_t)plotX_, plotW_, plotH_,
                                   (size_t)bufW_, app_.engine.frames(), bank, vp);
    }

    // paper grain: stable per pixel, so it reads as texture rather than noise
    if (plotW_ > 2 && plotH_ > 2) {
        const float grain = app_.get(mel::pGrain);
        if (grain > 0.001f)
            mel::addGrain(px + (size_t)plotY_ * (size_t)bufW_ + (size_t)plotX_, plotW_, plotH_,
                          (size_t)bufW_, grain);
    }

    // piano-key centre guides, blended additively so they lighten the image
    // without ever hiding what is underneath
    if (plotW_ > 2 && plotH_ > 2) {
        std::vector<int> guideRows, guideBoosts;
        collectKeyGuides(guideRows, guideBoosts);
        if (!guideRows.empty())
            mel::addGuideLines(px + (size_t)plotY_ * (size_t)bufW_ + (size_t)plotX_, plotW_, plotH_,
                               (size_t)bufW_, guideRows.data(), guideBoosts.data(),
                               (int)guideRows.size());
    }

    // ---- 2. overlays
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    const float s = scale_;
    char scratch[64];

    // frame around the plot
    {
        Pen pen(Color(kBorder), 1.0f);
        g.DrawRectangle(&pen, (float)plotX_ - 0.5f, (float)plotY_ - 0.5f, (float)plotW_ + 1.0f,
                        (float)plotH_ + 1.0f);
    }

    // frequency grid
    if (bands > 0 && plotH_ > 20) {
        static const float kCandidates[] = { 20, 30, 50, 70, 100, 150, 200, 300, 400, 500, 700, 1000,
                                             1500, 2000, 3000, 4000, 5000, 7000, 10000, 15000, 20000 };
        float lastY = -1000.0f;
        Pen gridPen(Color(gridOnPlot), 1.0f);
        for (float f : kCandidates) {
            if (f < fmin * 1.04f || f > fmax * 0.96f) continue;
            const float y = mel::yForFrequency(f, (float)plotH_, app_.engine.bank(), vp.axis, fmin, fmax);
            const float gy = (float)plotY_ + y;
            if (std::fabs(gy - lastY) < 22.0f * s) continue;
            lastY = gy;
            // When the piano-key guides are showing, they already ARE the
            // horizontal ruler: a second full-width rule at an arbitrary
            // frequency just reads as a second, conflicting grid. Keep a short
            // tick so the label stays anchored to its row.
            const bool keyGuidesOn = app_.get(mel::pKeyLines) > 0.001f;
            if (keyGuidesOn)
                g.DrawLine(&gridPen, (float)plotX_, gy, (float)plotX_ + 9.0f * s, gy);
            else
                g.DrawLine(&gridPen, (float)plotX_, gy, (float)(plotX_ + plotW_), gy);
            drawTextShadow(g, widen(shortHzText(f).c_str()),
                           RectF((float)plotX_ + 6.0f * s, gy - 8.0f * s, 70.0f * s, 16.0f * s),
                           res_->tiny.get(), inkOnPlot, StringAlignmentNear, inkShadow);
        }
    }

    // time grid
    if (plotW_ > 60) {
        const float span = vp.span;
        static const float kSteps[] = { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 30.0f, 60.0f };
        float step = kSteps[0];
        for (float c : kSteps) {
            if (span / c >= 3.0f) step = c;
        }
        Pen gridPen(Color(gridOnPlot), 1.0f);
        const float pxPerSec = (float)plotW_ / span;
        // Only draw rules where the analyser has history: on a fresh instance the
        // left part of the view is simply empty, and lines there would suggest data.
        const auto& fr = app_.engine.frames();
        const double oldest = fr.count > 0 ? fr.timeAt(0) : vp.now;
        const double have = std::max(0.0, vp.now - oldest);
        const float dataLeft = (float)plotX_
                               + (float)(1.0 - std::min(1.0, have / std::max(1e-3, (double)span)))
                                     * (float)plotW_;
        for (int k = 0; k * step <= span * 1.0001f; ++k) {
            const float x = (float)(plotX_ + plotW_) - (float)k * step * pxPerSec;
            if (x < (float)plotX_ + 4.0f) break;
            if (x < dataLeft - 2.0f) break;
            g.DrawLine(&gridPen, x, (float)plotY_, x, (float)(plotY_ + plotH_));
            std::snprintf(scratch, sizeof scratch, k == 0 ? "now" : "-%.0fs", (double)(k * step));
            if (k > 0 && step < 1.0f)
                std::snprintf(scratch, sizeof scratch, "-%.1fs", (double)(k * step));
            drawTextShadow(g, widen(scratch),
                           RectF(x - 26.0f * s, (float)(plotY_ + plotH_) - 16.0f * s, 52.0f * s, 14.0f * s),
                           res_->tiny.get(), inkOnPlot, StringAlignmentCenter, inkShadow);
        }
    }

    // test hook: a headless run cannot hold the pointer over the plot, because the
    // real cursor triggers WM_MOUSELEAVE. Debug builds only.
    if (std::getenv("MELSCOPE_DEBUG")) {
        if (const char* hov = std::getenv("MELSCOPE_HOVER")) {
            int hx = 0, hy = 0;
            if (std::sscanf(hov, "%d,%d", &hx, &hy) == 2) {
                hoverX_ = hx;
                hoverY_ = hy;
                hoverRow_ = -1;
            }
        }
    }
    // crosshair + readout
    if (hoverRow_ < 0 && hoverX_ >= plotX_ && hoverX_ < plotX_ + plotW_ && hoverY_ >= plotY_
        && hoverY_ < plotY_ + plotH_ && vp.span > 0
        && !(showPanel_ && hoverX_ >= panelX_ - 6 && hoverX_ <= panelX_ + panelW_ + 6)) {
        const int rx = hoverX_ - plotX_;
        const int ry = hoverY_ - plotY_;
        Pen cross(Color(gridOnPlot), 1.0f);
        g.DrawLine(&cross, (float)hoverX_, (float)plotY_, (float)hoverX_, (float)(plotY_ + plotH_));
        g.DrawLine(&cross, (float)plotX_, (float)hoverY_, (float)(plotX_ + plotW_), (float)hoverY_);
        double t = 0;
        float hz = 0;
        float db = -200.0f;
        db = mel::valueAtPixel(rx, ry, plotW_, plotH_, app_.engine.frames(), app_.engine.bank(), vp, &t, &hz);
        // note, frequency, level and time relative to the right edge (same convention
        // as the time axis), on a plate that follows the pointer so it never sits on
        // the axis labels and never lets the guide lines show through the numbers
        char line[192];
        // Nearest semitone, not the floor of it: truncating put every note's boundary at
        // its own centre frequency, so 219 Hz read G#3 and A3 only began at 220. A singer
        // treats a note as a region around its centre, so round to the closest semitone
        // (boundaries at the midpoint, i.e. +/-50 cents) and say how far off it is.
        const float noteValue = hzToNote(hz);
        const int noteIndex = (int)std::lround(noteValue);
        const float cents = (noteValue - (float)noteIndex) * 100.0f;
        char note[48];
        if (std::fabs(cents) < 0.5f)
            std::snprintf(note, sizeof note, "%s", noteName(noteIndex));
        else
            std::snprintf(note, sizeof note, "%s %+.0f\xC2\xA2", noteName(noteIndex), (double)cents);
        std::snprintf(line, sizeof line, "%s   %s   %.1f dB   %.2f s", note,
                      hzText(hz).c_str(), (double)db, t - vp.now);
        const std::wstring wide = widen(line);
        RectF measured;
        g.MeasureString(wide.c_str(), -1, res_->value.get(), PointF(0.0f, 0.0f), &measured);
        const float chipW = measured.Width + 4.0f * s, chipH = 16.0f * s;
        float bx = (float)hoverX_ + 14.0f * s;
        float by = (float)hoverY_ + 14.0f * s;
        if (bx + chipW > (float)(plotX_ + plotW_) - 6.0f * s) bx = (float)hoverX_ - chipW - 14.0f * s;
        if (by + chipH > (float)(plotY_ + plotH_) - 22.0f * s) by = (float)hoverY_ - chipH - 14.0f * s;
        const RectF plate(bx - 6.0f * s, by - 2.0f * s, chipW + 12.0f * s, chipH + 4.0f * s);
        {
            LinearGradientBrush plateBrush(
                PointF(plate.X, plate.Y), PointF(plate.X, plate.Y + plate.Height),
                Color((ARGB)(plotLight_ ? kSurface : argb(0x1c, 0x1c, 0x22))),
                Color((ARGB)(plotLight_ ? kSurfaceMuted : argb(0x26, 0x26, 0x2e))));
            g.FillRectangle(&plateBrush, plate);
        }
        strokeRound(g, plate, 0.0f, plotLight_ ? kBorder : argb(0x3a, 0x3a, 0x45));
        drawText(g, wide, RectF(bx, by, chipW, chipH), res_->value.get(),
                 plotLight_ ? kText : argb(0xf2, 0xf0, 0xf6));
    }

    // ---- 2b. piano keyboard on the left of the plot
    drawKeyboard();

    // ---- 2c. vertical pan bar, only while zoomed in
    if (bankFmax_ > bankFmin_ && (viewFmin_ > bankFmin_ + 1e-3f || viewFmax_ < bankFmax_ - 1e-3f)) {
        float bx = 0, by = 0, bw = 0, bh = 0;
        panBarRect(bx, by, bw, bh);
        const int axis = curAxis_;
        auto toAxis = [axis](float f) -> float {
            if (axis == 1) return std::log(std::max(1.0f, f));
            if (axis == 0) return (float)mel::hzToMelSlaney(std::max(1.0f, f));
            return f;
        };
        const float uLo = toAxis(bankFmin_), uHi = toAxis(bankFmax_);
        const float uSpan = uHi - uLo;
        if (uSpan > 0.0f) {
            fillRound(g, RectF(bx, by, bw, bh), bw * 0.5f, argb(0, 0, 0, 26));
            const float fracLo = mel::clampf((toAxis(viewFmin_) - uLo) / uSpan, 0.0f, 1.0f);
            const float fracHi = mel::clampf((toAxis(viewFmax_) - uLo) / uSpan, 0.0f, 1.0f);
            const float thumbH = std::max(bh * 0.06f, bh * (fracHi - fracLo));
            const float thumbY = by + bh * (1.0f - fracHi);
            fillRound(g, RectF(bx, thumbY, bw, thumbH), bw * 0.5f, argb(0x0d, 0x0e, 0x1c, 150));
        }
    }

    // ---- 3. header
    drawHeader();

    // ---- 4. control panel, as an overlay opened from the header
    if (showPanel_) drawPanel();

    // ---- 5. dropdown on top, help above everything
    drawDropdown();
    if (showHelp_) drawHelp();

    g.Flush();

    // ---- 6. blit
    HDC dc = GetDC(hwnd_);
    if (dc) {
        BitBlt(dc, 0, 0, width_, height_, memDC_, 0, 0, SRCCOPY);
        ReleaseDC(hwnd_, dc);
    }
    dirty_ = false;
}

void Gui::drawHeader()
{
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float s = scale_;

    const float headerX = (float)(gearX_ + gearW_) + 14.0f * s;
    // one line, vertically centred: nothing is left hanging over the canvas edge
    drawText(g, L"Mel Scope", RectF(headerX, (float)gearY_ - 3.0f * s, 220.0f * s, (float)gearH_ + 8.0f * s),
             res_->title.get(), kText, StringAlignmentNear, StringAlignmentCenter);

    // settings button (the control panel lives behind this)
    {
        const RectF gr((float)gearX_, (float)gearY_, (float)gearW_, (float)gearH_);
        const bool over = hoverX_ >= gearX_ && hoverX_ <= gearX_ + gearW_ && hoverY_ >= gearY_
                          && hoverY_ <= gearY_ + gearH_;
        chromeButton(g, gr, upper(L"Settings").c_str(), res_->group.get(), over, showPanel_);
    }

    // help: reachable without a menu dive
    {
        const RectF hr((float)helpX_, (float)helpY_, (float)helpW_, (float)helpH_);
        const bool over = hoverX_ >= helpX_ && hoverX_ <= helpX_ + helpW_ && hoverY_ >= helpY_
                          && hoverY_ <= helpY_ + helpH_;
        chromeButton(g, hr, upper(L"Help").c_str(), res_->group.get(), over, showHelp_);
    }

    // range reset: Low cut -> minimum, High cut -> Nyquist of the current rate
    {
        const RectF rr((float)rangeX_, (float)rangeY_, (float)rangeW_, (float)rangeH_);
        const bool over = hoverX_ >= rangeX_ && hoverX_ <= rangeX_ + rangeW_ && hoverY_ >= rangeY_
                          && hoverY_ <= rangeY_ + rangeH_;
        chromeButton(g, rr, upper(L"Full range").c_str(), res_->group.get(), over, false);
    }

    // freeze toggle: reachable without opening the panel
    {
        const RectF fr((float)freezeX_, (float)freezeY_, (float)freezeW_, (float)freezeH_);
        const bool over = hoverX_ >= freezeX_ && hoverX_ <= freezeX_ + freezeW_ && hoverY_ >= freezeY_
                          && hoverY_ <= freezeY_ + freezeH_;
        const bool frozen = app_.getBool(mel::pFreeze);
        const ARGB fill = frozen ? kMark : (over ? kInk : kSurface);
        fillRound(g, fr, 0.0f, fill);
        strokeRound(g, fr, 0.0f, kInk);
        const ARGB ink = frozen ? kOnMark : (over ? kPaper : kInk);
        // small pause glyph
        const float barW = 2.0f * s, barH = 8.0f * s;
        const float bx = fr.X + 12.0f * s, by = fr.Y + (fr.Height - barH) * 0.5f;
        SolidBrush glyph(Color((ARGB)ink));
        g.FillRectangle(&glyph, bx, by, barW, barH);
        g.FillRectangle(&glyph, bx + barW * 2.0f, by, barW, barH);
        drawText(g, upper(frozen ? L"Frozen" : L"Freeze"),
                 RectF(fr.X + 26.0f * s, fr.Y, fr.Width - 30.0f * s, fr.Height), res_->group.get(), ink,
                 StringAlignmentCenter);
    }


    // ---- legend: what each ink and gradient means, over the plot so it needs no room
    if (app_.getBool(mel::pLegend)) {
        const float pad = 8.0f * s;
        const float rowH = 13.0f * s;
        const float swat = 9.0f * s;
        const float stripW = 54.0f * s;
        const float stripH = 5.0f * s;
        const bool refLive = app_.getBool(mel::pRefOn) && app_.engineRef.frames().count > 1;
        const int rows = refLive ? 2 : 1;
        const float boxW = 150.0f * s;
        const float boxH = pad + rowH * (float)rows + pad * 0.5f;
        const RectF box((float)plotX_ + pad, (float)(plotY_ + plotH_) - boxH - pad * 2.0f, boxW, boxH);
        fillRound(g, box, 4.0f * s, kSurface);
        strokeRound(g, box, 4.0f * s, kInk);

        const int cmRaw = app_.getInt(mel::pCmap);
        const int cm = (cmRaw < 0 || cmRaw >= mel::kNumMaps) ? 0 : cmRaw;
        const int lightCanvas = mel::kMapIsLight[cm] ? 1 : 0;
        unsigned char secInk[3];
        mel::secondaryTraceInk(cm, lightCanvas, app_.getInt(mel::pRefInk), secInk);

        for (int row = 0; row < rows; ++row) {
            const float y = box.Y + pad * 0.5f + rowH * (float)row;
            const unsigned char* ink = row == 0 ? mel::kMapLine[cm] : secInk;
            SolidBrush sw(Color((ARGB)(0xFF000000u | ((uint32_t)ink[0] << 16)
                                       | ((uint32_t)ink[1] << 8) | ink[2])));
            g.FillRectangle(&sw, box.X + pad * 0.5f, y, swat, swat);
            drawText(g, upper(row == 0 ? L"Input" : L"Reference"),
                     RectF(box.X + pad * 0.5f + swat + 6.0f * s, y - 1.0f * s, 62.0f * s, rowH),
                     res_->group.get(), kInk, StringAlignmentNear);
            // the gradient this input is drawn with, as a strip
            const float sx = box.X + box.Width - stripW - pad * 0.5f;
            const int strip = row == 0 ? cm : mel::referenceCmap(cm);
            for (int i = 0; i < 32; ++i) {
                const unsigned char* cc = mel::kMaps[strip][(i * 255) / 31];
                SolidBrush seg(Color((ARGB)(0xFF000000u | ((uint32_t)cc[0] << 16)
                                            | ((uint32_t)cc[1] << 8) | cc[2])));
                g.FillRectangle(&seg, sx + stripW * (float)i / 32.0f, y + (swat - stripH) * 0.5f,
                                stripW / 32.0f + 0.6f, stripH);
            }
        }
    }

    // status line: the range in ink, then the technical detail in muted, right
    // aligned and clear of the pills. When the window is narrow the tail items are
    // dropped one by one rather than letting the two strings collide.
    const bool toast = !statusMsg_.empty() && (double)GetTickCount64() / 1000.0 < statusUntil_;
    const float statusRight = (float)(helpX_ - 12.0f * s);
    const float statusY = (float)gearY_ - 1.0f * s;
    const RectF statusRect((float)plotX_, statusY, statusRight - (float)plotX_, (float)gearH_ + 4.0f * s);
    auto measure = [&](const std::wstring& text, Font* f) {
        RectF b;
        if (!text.empty()) g.MeasureString(text.c_str(), -1, f, PointF(0.0f, 0.0f), &b);
        return b.Width;
    };

    if (toast) {
        drawText(g, widen(statusMsg_.c_str()), statusRect, res_->tiny.get(), kText, StringAlignmentFar,
                 StringAlignmentCenter);
    } else {
        char rangeTxt[96];
        std::snprintf(rangeTxt, sizeof rangeTxt, "%s - %s", hzText(shownFmin_).c_str(),
                      hzText(shownFmax_).c_str());
        const std::wstring rangeText = widen(rangeTxt);

        RectF titleBox;
        g.MeasureString(L"Mel Scope", -1, res_->title.get(), PointF(0.0f, 0.0f), &titleBox);
        const float titleEnd = headerX + titleBox.Width + 26.0f * s;

        char hzPart[32], fftPart[32], hopPart[32], bandsPart[32], fpsPart[32];
        std::snprintf(hzPart, sizeof hzPart, "%.1f kHz", app_.sampleRate() / 1000.0);
        std::snprintf(fftPart, sizeof fftPart, "FFT %d", shownFft_);
        std::snprintf(hopPart, sizeof hopPart, "hop %d", shownHop_);
        std::snprintf(bandsPart, sizeof bandsPart, "%d bands", shownBands_);
        std::snprintf(fpsPart, sizeof fpsPart, "%.0f fps", fps_);
        const char* parts[5] = { hzPart, fftPart, hopPart, bandsPart, fpsPart };

        const float rangeW = measure(rangeText, res_->tiny.get());
        const float gap = 26.0f * s;
        int keep = 5;
        std::wstring tail;
        for (; keep >= 0; --keep) {
            std::string joined;
            for (int i = 0; i < keep; ++i) {
                if (i) joined += "  -  ";
                joined += parts[i];
            }
            tail = widen(joined.c_str());
            const float tailW = measure(tail, res_->tiny.get());
            const float need = rangeW + (keep > 0 ? gap + tailW : 0.0f);
            if (titleEnd + need <= statusRight) break;
        }

        const float tailW = measure(tail, res_->tiny.get());
        if (keep > 0) {
            drawText(g, tail, statusRect, res_->tiny.get(), kMuted, StringAlignmentFar,
                     StringAlignmentCenter);
            const float rangeRight = statusRight - tailW - gap;
            Pen divider(Color(kBorder), 1.0f);
            const float dx = rangeRight + gap * 0.5f;
            g.DrawLine(&divider, dx, statusY + 7.0f * s, dx, statusY + 20.0f * s);
            drawText(g, rangeText, RectF(rangeRight - rangeW, statusY, rangeW,
                                         (float)gearH_ + 4.0f * s),
                     res_->tiny.get(), kText, StringAlignmentFar, StringAlignmentCenter);
        } else {
            drawText(g, rangeText, statusRect, res_->tiny.get(), kText, StringAlignmentFar,
                     StringAlignmentCenter);
        }
    }
}

void Gui::drawPanel()
{
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float s = scale_;

    // the panel is a card: white, 1px ink edge, one hard offset shadow
    dropShadow(g, RectF((float)panelX_, (float)panelY_, (float)panelW_, (float)panelH_), s);
    fillRound(g, RectF((float)panelX_, (float)panelY_, (float)panelW_, (float)panelH_), 0.0f, kSurface);
    strokeRound(g, RectF((float)panelX_, (float)panelY_, (float)panelW_, (float)panelH_), 0.0f, kCardEdge);

    // rows live in their own band, above the button block
    g.SetClip(RectF((float)panelX_, (float)panelY_, (float)panelW_,
                    (float)(listBottom_ - panelY_)));
    const auto& defs = mel::paramDefs();
    const float innerX = (float)panelX_ + 12.0f * s;
    const float innerW = (float)panelW_ - 24.0f * s;

    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        if (r.pid < 0) {
            drawText(g, upper(r.title.c_str()), RectF(innerX, r.y + 4.0f * s, innerW, r.h - 4.0f * s),
                     res_->group.get(), kMuted, StringAlignmentNear, StringAlignmentCenter);
            Pen pen(Color(kBorder), 1.0f);
            const float y = r.y + r.h - 3.0f * s;
            g.DrawLine(&pen, innerX, y, innerX + innerW, y);
            continue;
        }
        const auto& def = defs[(size_t)r.pid];
        const bool blocked = rowBlocked(r.pid);
        const bool hovered = ((int)i == hoverRow_) && !blocked;
        const bool dragging = (r.pid == dragPid_) && !blocked;
        if (hovered || dragging)
            fillRound(g, RectF(innerX - 4.0f * s, r.y + 1.0f * s, innerW + 8.0f * s, r.h - 2.0f * s),
                      0.0f, kSurfaceMuted);

        const bool hasRail = def.choices.empty();
        const float textH = r.h - (hasRail ? 8.0f * s : 0.0f);
        drawText(g, widen(def.name), RectF(innerX, r.y, innerW * 0.62f, textH), res_->body.get(),
                 blocked ? kDisabled : (hovered || dragging ? kAccentInk : kText));

        char val[64];
        formatValue(r.pid, app_.get(r.pid), val, sizeof val);
        const float valueRight = innerX + innerW - (def.choices.empty() ? 0.0f : 12.0f * s);
        const bool isToggle = def.choices.size() == 2;
        const ARGB valueColour =
            blocked ? kDisabled
                    : (isToggle ? (app_.get(r.pid) >= 0.5f ? kSuccess : kMuted)
                                : (dragging ? kAccent : kText));
        // the value: an inline editor while typing, otherwise the formatted number
        if (r.pid == editPid_) {
            const RectF box(innerX + innerW * 0.45f, r.y + 1.0f * s, innerW * 0.55f - 2.0f * s,
                            textH - 2.0f * s);
            fillRound(g, box, 0.0f, kSurfaceMuted);
            strokeRound(g, box, 0.0f, kAccent);
            drawText(g, widen(editText_.c_str()) + L"|",
                     RectF(box.X + 4.0f * s, box.Y, box.Width - 8.0f * s, box.Height),
                     res_->value.get(), kText, StringAlignmentFar);
        } else {
            // hover cue on the value box, so it is clear the number is clickable
            const bool inValueBox = paramEditable(r.pid) && !blocked
                                    && hoverX_ >= (int)(innerX + innerW * 0.66f) && hoverX_ <= (int)valueRight
                                    && hoverY_ >= r.y && hoverY_ <= r.y + r.h;
            if (inValueBox) {
                const RectF box(innerX + innerW * 0.66f, r.y + 1.0f * s, innerW * 0.34f - 2.0f * s,
                                textH - 2.0f * s);
                fillRound(g, box, 0.0f, kSurfaceMuted);
                strokeRound(g, box, 0.0f, kBorder);
            }
            drawText(g, widen(val), RectF(innerX, r.y, valueRight - innerX, textH), res_->value.get(),
                     valueColour, StringAlignmentFar);
        }

        if (!def.choices.empty()) {
            // chevron
            const float cx = innerX + innerW - 6.0f * s, cy = r.y + r.h * 0.5f;
            Pen pen(Color(blocked ? kDisabled : kMuted), 1.2f * s);
            g.DrawLine(&pen, cx - 3.0f * s, cy - 1.5f * s, cx, cy + 1.5f * s);
            g.DrawLine(&pen, cx, cy + 1.5f * s, cx + 3.0f * s, cy - 1.5f * s);
        } else {
            // value track in its own strip at the foot of the row (a frozen,
            // unchangeable row keeps a dim rail)
            const float y = r.y + r.h - 2.0f * s;
            Pen bgPen(Color(blocked ? kDisabled : kTrackBg), 2.0f * s);
            g.DrawLine(&bgPen, innerX, y, innerX + innerW, y);
            const float n = (def.max > def.min) ? (app_.get(r.pid) - def.min) / (def.max - def.min) : 0.0f;
            const float x = innerX + std::max(0.0f, std::min(1.0f, n)) * innerW;
            if (blocked) continue;
            Pen fillPen(Color(kInk), 2.0f * s);
            // only draw the filled part when there is one: a zero-length line with a
            // 2px pen leaves a stray dot at the label
            if (x > innerX + 1.0f) g.DrawLine(&fillPen, innerX, y, x, y);
            // square handle, but only where the row is tall enough that it cannot
            // touch the label above it
            if (r.h >= 22.0f * s) {
                const RectF hr(x - 3.5f * s, y - 3.0f * s, 7.0f * s, 6.0f * s);
                fillRound(g, hr, 0.0f, dragging ? kMark : kInk);
                if (dragging) strokeRound(g, hr, 0.0f, kInk);
            }
        }
    }

    g.ResetClip();
    g.SetClip(RectF((float)panelX_, (float)panelY_, (float)panelW_, (float)panelH_));
    {
        const float viewH = (float)listBottom_ - (float)panelY_ - 12.0f * s;
        if (contentH_ > viewH + 0.5f) {
            // the list scrolls: a visible rule closes it off and a slim thumb shows
            // how much is left, so a clipped row never looks like a layout bug
            fillRound(g, RectF((float)panelX_ + 1.0f, (float)listBottom_,
                               (float)panelW_ - 2.0f, 1.0f),
                      0.0f, argb(0x0d, 0x0e, 0x1c, 46));
            const float trackTop = (float)panelY_ + 12.0f * s;
            const float trackH = (float)listBottom_ - trackTop - 4.0f * s;
            const float frac = mel::clampf((float)viewH / contentH_, 0.15f, 1.0f);
            const float thumbH = trackH * frac;
            const float denom = std::max(1.0f, contentH_ - viewH);
            const float off = mel::clampf(scrollY_ / denom, 0.0f, 1.0f);
            const float thumbY = trackTop + (trackH - thumbH) * off;
            fillRound(g, RectF((float)(panelX_ + panelW_) - 5.0f * s, thumbY, 2.0f * s, thumbH),
                      0.0f, argb(0x0d, 0x0e, 0x1c, 90));
        }
    }
    // buttons: reset/clear on top, explicit Save/Load below
    const char* labels[4] = { "Reset all", "Clear", "Save preset", "Load preset" };
    for (int b = 0; b < 4; ++b) {
        float bx = 0, by = 0, bw = 0, bh = 0;
        if (!buttonRect(b, bx, by, bw, bh)) continue;
        const RectF br(bx, by, bw, bh);
        const bool hover = (hoverX_ >= (int)br.X && hoverX_ <= (int)(br.X + br.Width)
                            && hoverY_ >= (int)br.Y && hoverY_ <= (int)(br.Y + br.Height));
        chromeButton(g, br, upper(labels[b]).c_str(), res_->group.get(), hover, false);
    }
}

void Gui::drawDropdown()
{
    if (!dropdown_.open) return;
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float s = scale_;
    const auto& def = mel::paramDefs()[(size_t)dropdown_.pid];
    const int n = (int)dropdown_.items.size();
    const RectF box(dropdown_.x, dropdown_.y, dropdown_.w, n * dropdown_.itemH + 12.0f * s);
    dropShadow(g, box, s);
    fillRound(g, box, 0.0f, kSurface);
    strokeRound(g, box, 0.0f, kCardEdge);

    for (int i = 0; i < n; ++i) {
        const float iy = dropdown_.y + 6.0f * s + i * dropdown_.itemH;
        const RectF ir(box.X + 5.0f * s, iy, box.Width - 10.0f * s, dropdown_.itemH);
        if (i == dropdown_.hover) fillRound(g, ir, 0.0f, kMark);
        const int choiceIndex = dropdown_.items[(size_t)i];
        const bool selected = ((int)app_.get(dropdown_.pid) == choiceIndex);
        drawText(g, widen(def.choices[(size_t)choiceIndex]), RectF(ir.X + 10.0f * s, iy, ir.Width - 30.0f * s,
                                                          dropdown_.itemH),
                 res_->body.get(), kInk);
        if (selected) {
            // a square register mark, like the system's current-page indicator
            fillRound(g, RectF(ir.X + ir.Width - 14.0f * s, iy + dropdown_.itemH * 0.5f - 2.5f * s,
                               5.0f * s, 5.0f * s),
                      0.0f, kInk);
        }
    }
}

// ------------------------------------------------------------------ messages
LRESULT Gui::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT: {
        HDC dc = (HDC)wp;
        if (dc && memDC_) BitBlt(dc, 0, 0, width_, height_, memDC_, 0, 0, SRCCOPY);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        if (dc && memDC_) BitBlt(dc, 0, 0, width_, height_, memDC_, 0, 0, SRCCOPY);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        if (LOWORD(lp) > 0 && HIWORD(lp) > 0) {
            width_ = LOWORD(lp);
            height_ = HIWORD(lp); // was LOWORD: the height followed the width
            app_.setEditorSize(width_, height_); // remembered for the next time the window opens
            wantW_ = width_;
            wantH_ = height_;
            ensureBuffer();
            layout();
            paint();
        }
        return 0;
    case WM_TIMER: {
        app_.applyIfNeeded();
        app_.maybeSaveSettings();
        if (scaleResizePending_ && GetTickCount64() >= scaleResizeAt_) {
            scaleResizePending_ = false;
            if (onScaleResize_) onScaleResize_();
        }
        const double now = app_.now();
        if (app_.frozen()) {
            // held image: repaint whenever the interface changed (clicks, hovers,
            // display parameters) instead of stopping the timer entirely
            if (dirty_) paint();
            return 0;
        }
        if (now != lastPaintTime_ || dirty_) {
            lastPaintTime_ = now;
            paint();
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        hoverX_ = x;
        hoverY_ = y;
        if (panDrag_) {
            panViewTo((float)y);
            return 0;
        }
        if (editPendingPid_ >= 0 && std::abs(x - editPendingX_) > 3) {
            // it was a drag after all: slide from where the value already is
            const int pid = editPendingPid_;
            editPendingPid_ = -1;
            beginDrag(pid, editPendingX_);
            updateDrag(x);
        } else if (dragPid_ >= 0) {
            updateDrag(x);
        } else if (dropdown_.open) {
            const int item = dropdownItemAt(x, y);
            if (item != dropdown_.hover) {
                dropdown_.hover = item;
                dirty_ = true;
            }
        } else {
            const int r = rowAt(x, y);
            if (r != hoverRow_) {
                hoverRow_ = r;
                dirty_ = true;
            }
            // the crosshair follows the pointer, so any move over the plot needs a
            // repaint - otherwise it freezes along with the image
            if (x >= plotX_ && x < plotX_ + plotW_ && y >= plotY_ && y < plotY_ + plotH_)
                dirty_ = true;
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
        }
        {
            const bool onPanel = showPanel_ && x >= panelX_ && x <= panelX_ + panelW_ && y >= panelY_
                                 && y <= panelY_ + panelH_;
            const bool pannable = !onPanel && bankFmax_ > bankFmin_
                                  && (viewFmin_ > bankFmin_ * 1.001f || viewFmax_ < bankFmax_ / 1.001f)
                                  && x >= plotX_ && x < plotX_ + plotW_ && y >= plotY_ && y < plotY_ + plotH_;
            const LPCWSTR cur = pannable
                                    ? (LPCWSTR)IDC_SIZENS
                                    : (LPCWSTR)((rowAt(x, y) >= 0 || dropdown_.open) ? IDC_HAND : IDC_CROSS);
            SetCursor(LoadCursorW(nullptr, cur));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hoverRow_ = -1;
        hoverX_ = hoverY_ = -1;
        dirty_ = true;
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (GetFocus() != hwnd_) SetFocus(hwnd_);
        if (editPid_ >= 0) commitEdit();
        if (x >= helpX_ && x <= helpX_ + helpW_ && y >= helpY_ && y <= helpY_ + helpH_) {
            showHelp_ = !showHelp_;
            if (showHelp_) showPanel_ = false;
            helpScroll_ = 0.0f;
            dirty_ = true;
            return 0;
        }
        if (showHelp_) {
            float cx = 0, cy = 0, cw = 0, ch = 0;
    helpCard(cx, cy, cw, ch);
    const RectF card(cx, cy, cw, ch);
            const bool inside = (float)x >= card.X && (float)x <= card.X + card.Width
                                && (float)y >= card.Y && (float)y <= card.Y + card.Height;
            if (!inside) {
                showHelp_ = false;
            } else {
                // foot button closes it
                const float s = scale_;
                const RectF btn(card.X + card.Width - 110.0f * s, card.Y + card.Height - 44.0f * s,
                                86.0f * s, 28.0f * s);
                if ((float)x >= btn.X && (float)x <= btn.X + btn.Width && (float)y >= btn.Y
                    && (float)y <= btn.Y + btn.Height) {
                    showHelp_ = false;
                }
            }
            dirty_ = true;
            return 0;
        }
        if (x >= rangeX_ && x <= rangeX_ + rangeW_ && y >= rangeY_ && y <= rangeY_ + rangeH_) {
            app_.resetRange();
            app_.applyIfNeeded();
            // Full range means the whole band, so the vertical zoom goes with it:
            // Low/High cut were being reset while the plot stayed zoomed in.
            viewFmin_ = app_.get(mel::pFmin);
            viewFmax_ = app_.get(mel::pFmax);
            reqFmin_ = viewFmin_;
            reqFmax_ = viewFmax_;
            keepView_ = false;
            viewReady_ = true;
            char msg[96];
            std::snprintf(msg, sizeof msg, "Range reset to %.0f Hz - %.0f Hz", (double)app_.get(mel::pFmin),
                          (double)app_.get(mel::pFmax));
            setStatus(msg);
            dirty_ = true;
            return 0;
        }
        if (x >= freezeX_ && x <= freezeX_ + freezeW_ && y >= freezeY_ && y <= freezeY_ + freezeH_) {
            app_.toggleFreeze();
            dirty_ = true;
            return 0;
        }
        if (x >= gearX_ && x <= gearX_ + gearW_ && y >= gearY_ && y <= gearY_ + gearH_) {
            showPanel_ = !showPanel_;
            dirty_ = true;
            return 0;
        }
        SetCapture(hwnd_);
        if (dropdown_.open) {
            const int item = dropdownItemAt(x, y);
            if (item >= 0) {
                if (item >= 0 && item < (int)dropdown_.items.size())
                    app_.set(dropdown_.pid, (float)dropdown_.items[(size_t)item]);
                closeDropdown();
            } else {
                closeDropdown();
            }
            ReleaseCapture();
            return 0;
        }
        // buttons
        if (showPanel_) {
            for (int b = 0; b < 4; ++b) {
                float bx = 0, by = 0, bw = 0, bh = 0;
                if (!buttonRect(b, bx, by, bw, bh)) continue;
                if ((float)x >= bx && (float)x <= bx + bw && (float)y >= by && (float)y <= by + bh) {
                    ReleaseCapture();
                    if (b == 0) {
                        app_.resetAll();
                        app_.clearHistory();
                        app_.applyIfNeeded();
                    } else if (b == 1) {
                        app_.clearHistory();
                    } else if (b == 2) {
                        savePresetDialog();
                    } else {
                        loadPresetDialog();
                    }
                    dirty_ = true;
                    return 0;
                }
            }
        }
        const int r = rowAt(x, y);
        if (r >= 0) {
            const int pid = rows_[(size_t)r].pid;
            if (rowBlocked(pid)) return 0;
            const auto& def = mel::paramDefs()[(size_t)pid];
            if (!def.choices.empty()) {
                openDropdown(pid);
                return 0;
            }
            // The value box must not swallow the slider. A press on it is remembered and
            // only resolved on release: moving starts a drag (from the value's current
            // position, so nothing jumps), releasing in place opens the editor.
            const float s = scale_;
            const float innerX = (float)panelX_ + 12.0f * s;
            const float innerW = (float)panelW_ - 24.0f * s;
            if (paramEditable(pid) && (float)x > innerX + innerW * 0.66f) {
                editPendingPid_ = pid;
                editPendingX_ = x;
            } else {
                beginDrag(pid, x);
            }
            return 0;
        }
        // Dragging over the plot pans the zoom window (the bar is only a hint).
        // The control panel floats over the plot, so it is excluded here - a click
        // on the panel must never be swallowed by a pan.
        const bool overPanel = showPanel_ && x >= panelX_ && x <= panelX_ + panelW_ && y >= panelY_
                               && y <= panelY_ + panelH_;
        if (!overPanel && bankFmax_ > bankFmin_
            && (viewFmin_ > bankFmin_ * 1.001f || viewFmax_ < bankFmax_ / 1.001f)
            && (float)x >= plotX_ && (float)x < plotX_ + plotW_ && (float)y >= plotY_
            && (float)y < plotY_ + plotH_) {
            panDrag_ = true;
            panStartY_ = (float)y;
            panStartLo_ = viewFmin_;
            panStartHi_ = viewFmax_;
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (editPendingPid_ >= 0) {
            const int pid = editPendingPid_;
            editPendingPid_ = -1;
            beginEdit(pid);
            return 0;
        }
        panDrag_ = false;
        if (dragPid_ >= 0) {
            dragPid_ = -1;
            dirty_ = true;
        }
        ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (bankFmax_ > bankFmin_ && (viewFmin_ > bankFmin_ + 1e-3f || viewFmax_ < bankFmax_ - 1e-3f)) {
            float bx = 0, by = 0, bw = 0, bh = 0;
            panBarRect(bx, by, bw, bh);
            if ((float)x >= bx - 5.0f * scale_ && (float)x <= bx + bw + 5.0f * scale_
                && (float)y >= by && (float)y <= by + bh) {
                viewFmin_ = bankFmin_;
                viewFmax_ = bankFmax_;
                dirty_ = true;
                return 0;
            }
        }
        const int r = rowAt(x, y);
        if (r >= 0) {
            if (rowBlocked(rows_[(size_t)r].pid)) return 0;
            app_.set(rows_[(size_t)r].pid, mel::paramDefs()[(size_t)rows_[(size_t)r].pid].def);
            dirty_ = true;
        } else if (x >= plotX_ && x < plotX_ + plotW_ && y >= plotY_ && y < plotY_ + plotH_) {
            app_.toggleFreeze();
            dirty_ = true;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        const WORD keyState = GET_KEYSTATE_WPARAM(wp);
        // Three sources, because none is reliable on its own inside a host: the
        // wheel message may carry no key state, GetKeyState only reflects keys for
        // the focused thread, and GetAsyncKeyState reads the physical keyboard no
        // matter who owns focus.
        const bool ctrlDown = (keyState & MK_CONTROL) != 0 || (GetKeyState(VK_CONTROL) & 0x8000) != 0
                              || (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftDown = (keyState & MK_SHIFT) != 0 || (GetKeyState(VK_SHIFT) & 0x8000) != 0
                               || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd_, &pt);
        if (showHelp_) {
            helpScroll_ -= (float)delta * 36.0f * scale_;
            helpScroll_ = mel::clampf(helpScroll_, 0.0f, std::max(0.0f, helpContent_ - helpView_));
            dirty_ = true;
            return 0;
        }
        if (showPanel_ && pt.x >= panelX_ && pt.x <= panelX_ + panelW_ && pt.y >= panelY_
            && pt.y <= panelY_ + panelH_) {
            scrollY_ -= (float)delta * 28.0f * scale_;
            layout();
            dirty_ = true;
            return 0;
        }
        // Wheel over the piano keyboard: frequency zoom with no modifier at all, so
        // it still works when the host swallows Ctrl (or holds Alt) itself.
        if (pt.x >= pad_ && pt.x < plotX_ && pt.y >= plotY_ && pt.y < plotY_ + plotH_) {
            zoomFrequency(pt.y, delta);
            return 0;
        }
        if (pt.x >= plotX_ && pt.x < plotX_ + plotW_ && pt.y >= plotY_ && pt.y < plotY_ + plotH_) {
            if (ctrlDown) {
                zoomFrequency(pt.y, delta);
                return 0;
            }
            if (shiftDown) {
                const auto& def = mel::paramDefs()[mel::pRange];
                float v = app_.get(mel::pRange) - (float)delta * 4.0f;
                app_.set(mel::pRange, std::max(def.min, std::min(def.max, v)));
            } else {
                const auto& def = mel::paramDefs()[mel::pSpan];
                float v = app_.get(mel::pSpan) * std::pow(1.18f, (float)-delta);
                app_.set(mel::pSpan, std::max(def.min, std::min(def.max, v)));
            }
            dirty_ = true;
        }
        return 0;
    }
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        // Swallow plain Alt so the window never enters the menu loop. Combinations
        // (Alt+F4 and friends) still fall through to DefWindowProc.
        if (wp == VK_MENU || wp == VK_LMENU || wp == VK_RMENU) return 0;
        break;
    case WM_SYSCHAR:
        return 0;
    case WM_CHAR:
        if (editPid_ >= 0) {
            const wchar_t c = (wchar_t)wp;
            if ((c >= L'0' && c <= L'9') || c == L'.' || c == L'-') {
                if (editFresh_) {
                    editText_.clear();
                    editFresh_ = false;
                }
                if (editText_.size() < 12) {
                    editText_.push_back((char)c);
                    dirty_ = true;
                }
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (editPid_ >= 0) {
            if (wp == VK_RETURN) {
                commitEdit();
                return 0;
            }
            if (wp == VK_ESCAPE) {
                cancelEdit();
                return 0;
            }
            if (wp == VK_BACK) {
                if (!editText_.empty()) editText_.pop_back();
                dirty_ = true;
                return 0;
            }
        }
        if (wp == VK_ESCAPE) {
            if (dropdown_.open) closeDropdown();
            else if (showHelp_) { showHelp_ = false; dirty_ = true; }
            else if (showPanel_) { showPanel_ = false; dirty_ = true; }
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd_, 1);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// --------------------------------------------------------------- piano ruler
void Gui::collectKeyGuides(std::vector<int>& rows, std::vector<int>& boosts)
{
    rows.clear();
    boosts.clear();
    const float strength = app_.get(mel::pKeyLines);
    if (strength <= 0.001f || plotH_ < 8 || shownBands_ <= 0) return;

    const mel::MelBank& bank = app_.engine.bank();
    const int n0 = (int)std::floor(hzToNote(shownFmin_));
    const int n1 = (int)std::ceil(hzToNote(shownFmax_));
    if (n1 < n0 || n1 - n0 > 400) return;

    const int base = (int)std::lround(255.0f * std::min(1.0f, strength));

    struct Cand {
        int row;
        int strength;
        int priority; // 2 = C (octave), 1 = white key, 0 = black key
    };
    std::vector<Cand> cands;
    cands.reserve((size_t)(n1 - n0 + 1));
    for (int n = n0; n <= n1; ++n) {
        const float fc = noteToHz((float)n);
        if (fc < shownFmin_ * 0.999f || fc > shownFmax_ * 1.001f) continue;
        const float y = mel::yForFrequency(fc, (float)plotH_, bank, curAxis_, shownFmin_, shownFmax_);
        const int row = (int)std::lround(y);
        if (row < 0 || row >= plotH_) continue;
        const int semitone = ((n % 12) + 12) % 12;
        const bool isC = semitone == 0;
        const float weight = isC ? 1.0f : (isBlackKey(n) ? 0.70f : 0.88f);
        cands.push_back(Cand{ row, (int)std::lround((float)base * weight), isC ? 2 : (isBlackKey(n) ? 0 : 1) });
    }
    if (cands.empty()) return;

    // A mel axis squeezes whole octaves into a few pixels at the bottom, so an
    // unconditional line per semitone turns into a smudge. Keep the strongest
    // line (octave > white > black) and drop anything closer than minGap.
    const int minGap = std::max(5, (int)std::lround(8.0f * scale_));
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.row > b.row;
    });
    std::vector<Cand> kept;
    kept.reserve(cands.size());
    for (const Cand& c : cands) {
        bool clash = false;
        for (const Cand& k : kept) {
            if (std::abs(k.row - c.row) < minGap) {
                clash = true;
                break;
            }
        }
        if (!clash) kept.push_back(c);
    }
    std::sort(kept.begin(), kept.end(), [](const Cand& a, const Cand& b) { return a.row < b.row; });
    rows.reserve(kept.size());
    boosts.reserve(kept.size());
    for (const Cand& c : kept) {
        rows.push_back(c.row);
        boosts.push_back(c.strength);
    }
}
void Gui::drawKeyboard()
{
    if (keysW_ < 8 || plotH_ < 8) return;
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    const float x0 = (float)pad_;
    const float kw = (float)keysW_;
    const float top = (float)plotY_;
    const float bottom = (float)(plotY_ + plotH_);
    const mel::MelBank& bank = app_.engine.bank();
    auto yFor = [&](float hz) {
        return top + mel::yForFrequency(hz, (float)plotH_, bank, curAxis_, shownFmin_, shownFmax_);
    };

    int n0 = (int)std::floor(hzToNote(shownFmin_)) - 1;
    int n1 = (int)std::ceil(hzToNote(shownFmax_)) + 1;
    if (n1 < n0) std::swap(n0, n1);
    if (n1 - n0 > 400) return; // sanity: never loop over an absurd range

    // a muted field behind the keys, so white keys read as keys on white chrome
    {
        SolidBrush field(Color((ARGB)kSurfaceMuted));
        g.FillRectangle(&field, x0, top, kw, bottom - top);
        Pen edge(Color((ARGB)kBorder), 1.0f);
        g.DrawLine(&edge, x0 + 0.5f, top, x0 + 0.5f, bottom);
    }

    g.SetClip(RectF(x0, top, kw, bottom - top));

    // One shared integer boundary per semitone. Rounding each key independently (as
    // before) made neighbouring keys 15 px and 16 px tall and left hairline gaps; a
    // single table means every boundary is the same pixel for both keys it separates.
    const int slots = n1 - n0 + 2;
    std::vector<float> edgeF((size_t)slots);
    std::vector<int> edgeI((size_t)slots);
    std::vector<char> isWhite((size_t)slots, 1);
    for (int k = 0; k < slots; ++k) {
        const float midHz = noteToHz((float)(n0 + k) + 0.5f); // boundary between k-1 and k
        float yy = yFor(midHz);
        yy = std::max(top, std::min(bottom, yy));
        edgeF[(size_t)k] = yy;
        edgeI[(size_t)k] = (int)std::lround(yy);
    }
    for (int n = n0; n <= n1; ++n) isWhite[(size_t)(n - n0)] = isBlackKey(n) ? 0 : 1;


    // white keys: from the previous white key's top edge to this key's own top edge,
    // so the black-key slots are covered by white rather than showing the field
    SolidBrush whiteKey(Color((ARGB)kKeyWhite));
    for (int n = n0; n <= n1; ++n) {
        if (isBlackKey(n)) continue;
        int prev = n - 1;
        while (isBlackKey(prev)) --prev;
        const int yBot = edgeI[(size_t)(prev + 1 - n0)]; // lower pitch edge
        const int yTop = edgeI[(size_t)(n + 1 - n0)];    // higher pitch edge
        if (yBot - yTop < 1) continue;
        g.FillRectangle(&whiteKey, x0, (float)yTop, kw, (float)(yBot - yTop));
    }

    // separators: one per semitone boundary, drawn once
    {
        Pen sep(Color(kBorder), 1.0f);
        for (int k = 1; k < slots - 1; ++k) {
            const int y = edgeI[(size_t)k];
            const int span = edgeI[(size_t)k - 1] - y; // one semitone above it
            if (span < 3) continue;                    // too thin to read as a line
            g.DrawLine(&sep, x0, (float)y - 0.5f, x0 + kw, (float)y - 0.5f);
        }
    }

    // black keys, shorter and at the left edge (the keys point right)
    SolidBrush blackKey(Color((ARGB)kKeyBlack));
    for (int n = n0; n <= n1; ++n) {
        if (!isBlackKey(n)) continue;
        const int yBot = edgeI[(size_t)(n - n0)];
        const int yTop = edgeI[(size_t)(n + 1 - n0)];
        if (yBot - yTop < 2) continue; // too thin to read as a black key
        // inset a little so a black key reads shorter than the white slot around it
        const int inset = std::max(0, (int)std::lround((float)(yBot - yTop) * 0.08f));
        const int a2 = yTop + inset, b2 = yBot - inset;
        if (b2 - a2 < 2) continue;
        g.FillRectangle(&blackKey, x0, (float)a2, kw * 0.62f, (float)(b2 - a2));
    }

    // octave labels on the C keys, only when there is room
    if (res_ && res_->tiny) {
        for (int n = n0; n <= n1; ++n) {
            if ((((n % 12) + 12) % 12) != 0) continue;
            const int yBot = edgeI[(size_t)(n - n0)];
            const int yTop = edgeI[(size_t)(n + 1 - n0)];
            if (yBot - yTop < 13.0f * scale_) continue;
            drawText(g, widen(noteName(n)),
                     RectF(x0 + kw * 0.30f, (float)yTop, kw * 0.66f, (float)(yBot - yTop)),
                     res_->tiny.get(), kMuted, StringAlignmentFar);
        }
    }
    g.ResetClip();

    Pen edge(Color(kBorder), 1.0f);
    g.DrawLine(&edge, x0 + kw + 0.5f, top, x0 + kw + 0.5f, bottom);
}
// --------------------------------------------------------------- presets
namespace {
std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(0, n - 1), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}
} // namespace

void Gui::setStatus(const std::string& text)
{
    statusMsg_ = text;
    statusUntil_ = (double)GetTickCount64() / 1000.0 + 15.0;
    dirty_ = true;
}

void Gui::savePresetDialog()
{
    wchar_t file[MAX_PATH] = L"mel-scope-prefs.ini";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Mel Scope settings (*.ini)\0*.ini\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"ini";
    ofn.lpstrTitle = L"Save Mel Scope settings";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    const std::string path = toUtf8(file);
    if (app_.writeParams(path.c_str())) setStatus("Settings saved to: " + path);
    else setStatus("Could not write: " + path);
}

void Gui::loadPresetDialog()
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Mel Scope settings (*.ini)\0*.ini\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Load Mel Scope settings";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::string path = toUtf8(file);
    if (app_.readParams(path.c_str())) {
        app_.applyIfNeeded();
        setStatus("Settings loaded from: " + path);
    } else {
        setStatus("Could not read: " + path);
    }
}


// Vertical zoom: narrow/widen the displayed frequency window around whatever the
// pointer is over, so that frequency stays under the cursor.
void Gui::clampView()
{
    // reqFmin_/reqFmax_ hold a range that has been asked for but not analysed yet,
    // so the view is not yanked back while the engine catches up.
    const float lo = std::min(bankFmin_, reqFmin_), hi = std::max(bankFmax_, reqFmax_);
    if (!(hi > lo)) return;
    viewFmin_ = mel::clampf(viewFmin_, lo, hi);
    viewFmax_ = mel::clampf(viewFmax_, lo, hi);
    app_.setViewRange(viewFmin_, viewFmax_);
    if (viewFmax_ - viewFmin_ < (hi - lo) * 0.002f) {
        viewFmin_ = lo;
        viewFmax_ = hi;
    }
}

void Gui::zoomFrequency(int mouseY, int wheelDelta)
{
    if (wheelDelta == 0 || plotH_ < 4) return;
    if (!(bankFmax_ > bankFmin_)) return;
    const float p = mel::clampf((float)(plotY_ + plotH_ - 1 - mouseY) / (float)(plotH_ - 1), 0.0f, 1.0f);
    const auto& defs = mel::paramDefs();
    const float wideHi = std::min(defs[mel::pFmax].max, (float)(app_.sampleRate() * 0.5));
    const mel::FrequencyRange r = mel::zoomFrequencyRange(viewFmin_, viewFmax_, curAxis_, p,
                                                          std::pow(1.3f, -(float)wheelDelta),
                                                          defs[mel::pFmin].min, wideHi);
    // Zooming out past what is analysed has to re-analyse: otherwise the view would
    // silently stop at the analysed edge and the wheel would appear dead.
    const float eps = (bankFmax_ - bankFmin_) * 1e-3f;
    if (r.fmin < bankFmin_ - eps || r.fmax > bankFmax_ + eps) {
        const float nfmin = std::max(defs[mel::pFmin].min, std::min(bankFmin_, r.fmin));
        const float nfmax = std::min(wideHi, std::max(bankFmax_, r.fmax));
        app_.set(mel::pFmin, nfmin);
        app_.set(mel::pFmax, nfmax);
        // Keep the requested window: paint() adopts the new analysed range without
        // throwing the zoom away, so consecutive wheel steps accumulate.
        reqFmin_ = std::min(reqFmin_, r.fmin);
        reqFmax_ = std::max(reqFmax_, r.fmax);
        viewFmin_ = r.fmin;
        viewFmax_ = r.fmax;
        keepView_ = true;
        dirty_ = true;
        return;
    }
    viewFmin_ = r.fmin;
    viewFmax_ = r.fmax;
    clampView();
    dirty_ = true;
}

// Vertical pan bar: shows where the zoom window sits inside the analysed range and
// lets you drag it. Only drawn when the view is actually zoomed in.
void Gui::panBarRect(float& x, float& y, float& w, float& h) const
{
    const float s = scale_;
    w = 6.0f * s;
    x = (float)(plotX_ + plotW_) - w - 2.0f * s;
    y = (float)plotY_ + 2.0f * s;
    h = (float)plotH_ - 4.0f * s;
}

void Gui::panViewTo(float mouseY)
{
    int axis = curAxis_;
    auto toAxis = [axis](float f) -> float {
        if (axis == 1) return std::log(std::max(1.0f, f));
        if (axis == 0) return (float)mel::hzToMelSlaney(std::max(1.0f, f));
        return f;
    };
    auto fromAxis = [axis](float u) -> float {
        if (axis == 1) return (float)std::exp(u);
        if (axis == 0) return (float)mel::melToHzSlaney(u);
        return u;
    };
    const float uBankLo = toAxis(bankFmin_), uBankHi = toAxis(bankFmax_);
    const float uSpan = toAxis(panStartHi_) - toAxis(panStartLo_);
    if (!(uSpan > 0.0f) || !(uBankHi > uBankLo)) return;
    float x = 0, y = 0, w = 0, h = 0;
    panBarRect(x, y, w, h);
    const float travel = std::max(1.0f, h - 2.0f * scale_);
    float lo = toAxis(panStartLo_) + (uBankHi - uBankLo) * ((mouseY - panStartY_) / travel);
    lo = mel::clampf(lo, uBankLo, std::max(uBankHi - uSpan, uBankLo));
    viewFmin_ = fromAxis(lo);
    viewFmax_ = fromAxis(lo + uSpan);
    clampView();
    dirty_ = true;
}

// ------------------------------------------------------------------- help
namespace {

struct HelpSection {
    const wchar_t* heading;
    const wchar_t* body;
};

// Kept as data so the card stays a simple scroller and the text stays editable.
const HelpSection kHelpSections[] = {
    { L"Mouse and keys",
      L"Wheel over the plot - zoom the time span.\n"
      L"Ctrl + wheel - zoom the frequency range around the pointer (the frequency under\n"
      L"the cursor stays under it). The same zoom works with no modifier when the wheel\n"
      L"is over the piano keyboard on the left.\n"
      L"Shift + wheel - change the spectrum range (how many dB the colours cover).\n"
      L"Drag on the plot - pan the zoomed range; the cursor becomes a resize arrow.\n"
      L"Drag a row - change its value (hold Shift for fine steps).\n"
      L"Double-click a row - reset that parameter to its default.\n"
      L"Double-click the plot - freeze / unfreeze.\n"
      L"Double-click the slim bar on the right - zoom the frequency range back out.\n"
      L"Wheel over the panel - scroll the parameter list.\n"
      L"Esc - close this help, then the menu, then the panel." },

    { L"The two displays",
      L"Harmonics is the view this is built around, and the intended one for a vocal or\n"
      L"any single mono source. It traces the spectral peaks and draws them as lines over\n"
      L"a dimmed mel spectrum, so each line follows a partial of the sound and vibrato,\n"
      L"glide and tuning read at a glance even when the spectrum itself is too dense to\n"
      L"look at.\n"
      L"\n"
      L"Spectrum is just a normal spectrogram - the plain mel image with no peak line -\n"
      L"for general use. It shows everything, including the noise between the partials\n"
      L"that the traced view deliberately ignores, which makes it the better choice for\n"
      L"unpitched material, noise, or whenever you want the raw picture rather than a\n"
      L"reading of the pitch." },

    { L"Analysis",
      L"FFT size - window length. Longer is sharper in frequency and smeared in time.\n"
      L"Below 2048 the low bands share FFT bins and the image turns blocky, so the list\n"
      L"starts there.\n"
      L"Overlap - how much consecutive windows overlap; more overlap means more columns\n"
      L"per second and a smoother time axis.\n"
      L"Mel bands - vertical resolution of the filterbank.\n"
      L"Window - the analysis window; Blackman-Harris has the lowest side lobes, Hann is\n"
      L"a good general default.\n"
      L"Input - which channel to analyse when the track is stereo.\n"
      L"Low cut / High cut - the range the filterbank covers. Changing them rebuilds the\n"
      L"analysis and clears the history; Ctrl + wheel only moves the view inside that\n"
      L"range and keeps the history." },

    { L"Range",
      L"Range - the dynamic range in dB below the ceiling that the colours span.\n"
      L"Ceiling - the level mapped to the top of the colour ramp (0 dB is a full scale\n"
      L"sine).\n"
      L"Auto level - tracks the loudest band and moves the ceiling with it, so quiet\n"
      L"material still fills the ramp." },

    { L"View",
      L"Time span - how many seconds of history the width covers.\n"
      L"Display - Harmonics or Spectrum, described above.\n"
      L"Theme - Light or Dark. The canvas follows the theme, and each theme has its own\n"
      L"set of gradients.\n"
      L"Frequency axis - Log, Mel or Linear spacing of the vertical axis.\n"
      L"Colormap - the gradient; the list offers only maps that belong to the theme.\n"
      L"Gamma / Tilt - mid-tone brightness, and a dB per octave slope across the image.\n"
      L"Glow - soft bloom over the wash; it never touches the peak lines.\n"
      L"Grain - fine, stable paper texture.\n"
      L"Smoothing - temporal averaging between frames.\n"
      L"MIDI notes - strength of the bands drawn behind the spectrum for notes received on\n"
      L"the MIDI input. Each band is one semitone tall at the played pitch, so it lines up\n"
      L"with the analyser; 0 turns them off.\n"
      L"Peak range - dB span of the peak traces, counted down from the ceiling. Keep it\n"
      L"narrower than Spectrum range: that one sets the spectrum image, this one sets the\n"
      L"lines, so quiet partials stay readable as lighter lines. Greyed out unless Display\n"
      L"is Harmonics, since a plain spectrum has no traces.\n"
      L"Key guides - strength of the piano-key guide lines across the plot.\n"
      L"UI scale - size of the text and controls, in percent of the host's scale. The\n"
      L"window opens at this size too, so raising it also asks the host for a bigger\n"
      L"editor. 115 % is the default." },

    { L"Freeze",
      L"Freeze holds the image where it is. Every display parameter keeps working on\n"
      L"the held image; the parameters that would rebuild the filterbank (FFT size,\n"
      L"overlap, bands, window, low/high cut, smoothing) are greyed out while frozen,\n"
      L"because applying them would discard what is being held. Unfreezing drops the\n"
      L"held history and starts streaming again." },

    { L"Saving settings",
      L"The panel has Save preset and Load preset: a file dialog, and the path used is\n"
      L"echoed in the header. Separately, every parameter is written automatically on\n"
      L"exit, so a new instance starts where you left off." },

    { L"About",
      L"Mel Scope - a mel spectrogram for inspecting voice. The peak tracer follows the\n"
      L"partials of a pitched sound, so the display keeps working when the spectrum\n"
      L"itself is too dense to read." },
};

} // namespace

void Gui::helpCard(float& x, float& y, float& w, float& h) const
{
    const float s = scale_;
    // never cover the header row: the sheet lives between it and the bottom edge
    const float top = (float)headerH_ + 8.0f * s;
    const float bottom = (float)height_ - 18.0f * s;
    w = std::min((float)width_ - 40.0f * s, 620.0f * s);
    h = std::min(bottom - top, 520.0f * s);
    x = ((float)width_ - w) * 0.5f;
    y = top + (bottom - top - h) * 0.5f;
}

void Gui::drawHelp()
{
    Graphics g(memDC_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float s = scale_;
    float hx = 0, hy = 0, hw = 0, hh = 0;
    helpCard(hx, hy, hw, hh);
    const RectF card(hx, hy, hw, hh);

    // dim the editor behind the sheet
    fillRound(g, RectF(0.0f, 0.0f, (float)width_, (float)height_), 0.0f, argb(0x0d, 0x0e, 0x1c, 96));
    dropShadow(g, card, s);
    fillRound(g, card, 0.0f, kSurface);
    strokeRound(g, card, 0.0f, kCardEdge);

    drawText(g, L"Mel Scope", RectF(card.X + 26.0f * s, card.Y + 16.0f * s, card.Width - 60.0f * s,
                                   26.0f * s),
             res_->title.get(), kText, StringAlignmentNear, StringAlignmentNear);
    drawText(g, upper(L"Help").c_str(),
             RectF(card.X + 26.0f * s, card.Y + 42.0f * s, card.Width - 60.0f * s, 16.0f * s),
             res_->group.get(), kMuted, StringAlignmentNear, StringAlignmentNear);
    {
        Pen rule(Color(kBorder), 1.0f);
        g.DrawLine(&rule, card.X + 26.0f * s, card.Y + 62.0f * s, card.X + card.Width - 26.0f * s,
                   card.Y + 62.0f * s);
    }

    const float bodyTop = card.Y + 74.0f * s;
    const float footTop = card.Y + card.Height - 52.0f * s;
    const float innerW = card.Width - 52.0f * s;

    g.SetClip(RectF(card.X + 1.0f, bodyTop, card.Width - 2.0f, footTop - bodyTop));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentNear);
    fmt.SetTrimming(StringTrimmingNone);

    float y = bodyTop - helpScroll_;
    for (const auto& sec : kHelpSections) {
        drawText(g, upper(sec.heading), RectF(card.X + 26.0f * s, y, innerW, 16.0f * s),
                 res_->group.get(), kMuted, StringAlignmentNear, StringAlignmentNear);
        y += 19.0f * s;
        const std::wstring body = sec.body;
        RectF measured;
        g.MeasureString(body.c_str(), -1, res_->body.get(), RectF(0.0f, 0.0f, innerW, 4000.0f * s), &fmt,
                        &measured);
        SolidBrush brush(Color((ARGB)kText));
        g.DrawString(body.c_str(), -1, res_->body.get(),
                     RectF(card.X + 26.0f * s, y, innerW, measured.Height + 4.0f * s), &fmt, &brush);
        y += measured.Height + 18.0f * s;
    }
    helpContent_ = (y + helpScroll_) - bodyTop;
    helpView_ = footTop - bodyTop;
    if (helpScroll_ > helpContent_ - helpView_)
        helpScroll_ = std::max(0.0f, helpContent_ - helpView_);
    g.ResetClip();

    if (helpContent_ > helpView_ + 1.0f) {
        const float trackTop = bodyTop + 4.0f * s;
        const float trackH = footTop - trackTop - 8.0f * s;
        const float frac = mel::clampf(helpView_ / helpContent_, 0.1f, 1.0f);
        const float thumbH = trackH * frac;
        const float off = mel::clampf(helpScroll_ / std::max(1.0f, helpContent_ - helpView_), 0.0f, 1.0f);
        fillRound(g, RectF(card.X + card.Width - 7.0f * s, trackTop + (trackH - thumbH) * off,
                           2.0f * s, thumbH),
                  0.0f, argb(0x0d, 0x0e, 0x1c, 100));
    }

    {
        Pen rule(Color(kBorder), 1.0f);
        g.DrawLine(&rule, card.X + 26.0f * s, footTop, card.X + card.Width - 26.0f * s, footTop);
    }
    drawText(g, upper(L"Wheel scrolls  -  Esc closes").c_str(),
             RectF(card.X + 26.0f * s, footTop + 8.0f * s, innerW - 110.0f * s, 26.0f * s),
             res_->tiny.get(), kMuted, StringAlignmentNear, StringAlignmentCenter);
    {
        const RectF btn(card.X + card.Width - 110.0f * s, card.Y + card.Height - 44.0f * s, 86.0f * s,
                        28.0f * s);
        const bool over = hoverX_ >= (int)btn.X && hoverX_ <= (int)(btn.X + btn.Width)
                          && hoverY_ >= (int)btn.Y && hoverY_ <= (int)(btn.Y + btn.Height);
        chromeButton(g, btn, upper(L"Close").c_str(), res_->group.get(), over, false);
    }
}

// ------------------------------------------------------- inline value editing
bool Gui::paramEditable(int pid) const
{
    const auto& def = mel::paramDefs()[(size_t)pid];
    return def.choices.empty(); // choices are picked from the menu, not typed
}

void Gui::beginEdit(int pid)
{
    if (!paramEditable(pid)) return;
    editPid_ = pid;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4g", (double)app_.get(pid));
    editText_ = buf;
    editFresh_ = true;  // first keystroke replaces the value
    dirty_ = true;
}

void Gui::commitEdit()
{
    if (editPid_ < 0) return;
    const auto& def = mel::paramDefs()[(size_t)editPid_];
    const double typed = std::atof(editText_.c_str());
    if (editText_.empty()) {
        editPid_ = -1;
        dirty_ = true;
        return;
    }
    // clamped to the parameter's own range, so a typo cannot land anywhere odd
    float v = (float)typed;
    if (v < def.min) v = def.min;
    if (v > def.max) v = def.max;
    app_.set(editPid_, v);
    editPid_ = -1;
    dirty_ = true;
}

void Gui::cancelEdit()
{
    if (editPid_ < 0) return;
    editPid_ = -1;
    dirty_ = true;
}

} // namespace melscope
