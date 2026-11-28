#include "pch.h"
#include "visual_alert.h"

#include "display_utils.h"
#include "services.h"
#include "ui_motion.h"
#include "widget_appearance.h"

#include <algorithm>
#include <cmath>

namespace ClassTimeApp {
namespace {

using ClockStyle = classtime::NotificationSettings::AlertClockStyle;
using ClockPosition = classtime::NotificationSettings::AlertClockPosition;
using winrt::Windows::UI::Color;

constexpr wchar_t kDropClockWindowClass[] = L"ClassTimeDropClockClass";
constexpr double kPi = 3.14159265358979323846;

constexpr double kDigitalWidthDip = 300.0;
constexpr double kDigitalHeightDip = 176.0;
constexpr double kMechanicalWidthDip = 256.0;
constexpr double kMechanicalHeightDip = 284.0;
constexpr double kMechanicalDialDip = 208.0;
constexpr double kClockStagePadDip = 52.0;

D2D1_COLOR_F D2DColor(Color color, float alphaScale = 1.0f) {
  return D2D1::ColorF(color.R / 255.0f, color.G / 255.0f, color.B / 255.0f,
                      (color.A / 255.0f) * alphaScale);
}

D2D1_COLOR_F D2DRgba(uint8_t r, uint8_t g, uint8_t b, float a = 1.0f) {
  return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

Color MixColor(Color a, Color b, double t) {
  const double u = std::clamp(t, 0.0, 1.0);
  const auto mix = [u](uint8_t ca, uint8_t cb) {
    return static_cast<uint8_t>(std::clamp<long>(
        std::lround(ca + (static_cast<double>(cb) - ca) * u), 0, 255));
  };
  return winrt::Windows::UI::ColorHelper::FromArgb(255, mix(a.R, b.R), mix(a.G, b.G), mix(a.B, b.B));
}

struct ClockTheme {
  D2D1_COLOR_F accent{};
  // Digital 3D
  D2D1_COLOR_F bodyTop{};
  D2D1_COLOR_F bodyMid{};
  D2D1_COLOR_F bodyBottom{};
  D2D1_COLOR_F tileTop{};
  D2D1_COLOR_F tileBottom{};
  D2D1_COLOR_F tileBorder{};
  D2D1_COLOR_F hinge{};
  D2D1_COLOR_F digit{};
  D2D1_COLOR_F rim{};
  D2D1_COLOR_F badgeBg{};
  D2D1_COLOR_F badgeText{};
  D2D1_COLOR_F title{};
  D2D1_COLOR_F footer{};
  D2D1_COLOR_F glossTop{};
  D2D1_COLOR_F glossMid{};
  // Mechanical 3D
  D2D1_COLOR_F bezel0{};
  D2D1_COLOR_F bezel1{};
  D2D1_COLOR_F bezel2{};
  D2D1_COLOR_F dial0{};
  D2D1_COLOR_F dial1{};
  D2D1_COLOR_F dial2{};
  D2D1_COLOR_F tickMinor{};
  D2D1_COLOR_F tickMajor{};
  D2D1_COLOR_F numeral{};
  D2D1_COLOR_F digitalSmall{};
  D2D1_COLOR_F handMain{};
  D2D1_COLOR_F plateBg{};
  D2D1_COLOR_F plateRim{};
  // 3D 立体侧壁基础色
  uint8_t sideR = 72;
  uint8_t sideG = 82;
  uint8_t sideB = 102;
};

ClockTheme ToClockTheme(const WidgetAppearance::ClockAppearance& app) {
  ClockTheme theme;
  theme.accent = D2DColor(app.accent);
  theme.bodyTop = D2DColor(app.bodyTop);
  theme.bodyMid = D2DColor(app.bodyMid);
  theme.bodyBottom = D2DColor(app.bodyBottom);
  theme.tileTop = D2DColor(app.tileTop);
  theme.tileBottom = D2DColor(app.tileBottom);
  theme.tileBorder = D2DColor(app.tileBorder);
  theme.hinge = D2DColor(app.hinge);
  theme.digit = D2DColor(app.digit);
  theme.rim = D2DColor(app.rim);
  theme.badgeBg = D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b,
                               app.lightBody ? 0.16f : 0.22f);
  theme.badgeText = D2DColor(app.badgeText);
  theme.title = D2DColor(app.title);
  theme.footer = D2DColor(app.footer);
  theme.glossTop = D2DRgba(255, 255, 255, app.glossAlphaTop);
  theme.glossMid = D2DRgba(255, 255, 255, app.glossAlphaMid);
  theme.bezel0 = D2DColor(app.bezel0);
  theme.bezel1 = D2DColor(app.bezel1);
  theme.bezel2 = D2DColor(app.bezel2);
  theme.dial0 = D2DColor(app.dial0);
  theme.dial1 = D2DColor(app.dial1);
  theme.dial2 = D2DColor(app.dial2);
  theme.tickMinor = D2DColor(app.tickMinor);
  theme.tickMajor = D2DColor(app.tickMajor);
  theme.numeral = D2DColor(app.numeral);
  theme.digitalSmall = D2DColor(app.digitalSmall);
  theme.handMain = D2DColor(app.handMain);
  theme.plateBg = D2DColor(app.plateBg);
  theme.plateRim = D2DColor(app.plateRim);
  theme.sideR = app.sideWall.R;
  theme.sideG = app.sideWall.G;
  theme.sideB = app.sideWall.B;
  return theme;
}

uint32_t PremultiplyColor(Color color, double alphaFactor) {
  const uint32_t alpha = static_cast<uint32_t>(
      std::clamp<long>(std::lround(color.A * alphaFactor), 0, 255));
  return (alpha << 24) | ((color.R * alpha / 255) << 16) |
         ((color.G * alpha / 255) << 8) | (color.B * alpha / 255);
}

double EaseOutBack(double t) {
  const double u = std::clamp(t, 0.0, 1.0) - 1.0;
  constexpr double c1 = 1.70158;
  constexpr double c3 = c1 + 1.0;
  return 1.0 + c3 * u * u * u + c1 * u * u;
}

double EaseOutCubic(double t) {
  const double u = 1.0 - std::clamp(t, 0.0, 1.0);
  return 1.0 - u * u * u;
}

double EaseInCubic(double t) {
  const double u = std::clamp(t, 0.0, 1.0);
  return u * u * u;
}

void RegisterClockClass() {
  static bool registered = false;
  if (registered) return;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &VisualAlert::ClockWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
  wc.lpszClassName = kDropClockWindowClass;
  if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = true;
  }
}

bool Create32BitDib(int width, int height, HDC* outDc, HBITMAP* outBmp, HGDIOBJ* outPrev,
                    void** outPixels) {
  if (width <= 0 || height <= 0) return false;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return false;
  void* pixels = nullptr;
  HBITMAP bmp = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!bmp) {
    DeleteDC(dc);
    return false;
  }
  HGDIOBJ prev = SelectObject(dc, bmp);
  if (!prev || prev == HGDI_ERROR) {
    DeleteObject(bmp);
    DeleteDC(dc);
    return false;
  }
  *outDc = dc;
  *outBmp = bmp;
  *outPrev = prev;
  *outPixels = pixels;
  return true;
}

void ReleaseDib(HDC* dc, HBITMAP* bmp, HGDIOBJ* prev, void** pixels) {
  if (*dc && *prev) SelectObject(*dc, *prev);
  if (*bmp) DeleteObject(*bmp);
  if (*dc) DeleteDC(*dc);
  *dc = nullptr;
  *bmp = nullptr;
  *prev = nullptr;
  if (pixels) *pixels = nullptr;
}

// 快速单精度解析有向距离（用于 3D 立体侧壁厚边挤压与软阴影）。
inline float RoundedRectSdfFast(float px, float py, float cx, float cy, float hw, float hh,
                                float r) {
  const float qx = std::fabs(px - cx) - std::max(0.0f, hw - r);
  const float qy = std::fabs(py - cy) - std::max(0.0f, hh - r);
  const float ox = std::max(qx, 0.0f);
  const float oy = std::max(qy, 0.0f);
  const float out = std::sqrt(ox * ox + oy * oy);
  const float in = std::min(std::max(qx, qy), 0.0f);
  return out + in - r;
}

inline float CardBodySdfFast(float px, float py, float width, float height, ClockStyle style) {
  if (!classtime::NotificationSettings::IsDial(style)) {
    return RoundedRectSdfFast(px, py, width * 0.5f, height * 0.5f, width * 0.5f, height * 0.5f,
                              22.0f);
  }
  const float dialCenterX = width * 0.5f;
  constexpr float dialCenterY = static_cast<float>(kMechanicalDialDip * 0.5 + 2.0);
  constexpr float dialRadius = static_cast<float>(kMechanicalDialDip * 0.5 - 2.0);
  const float dx = px - dialCenterX;
  const float dy = py - dialCenterY;
  const float circleDist = std::sqrt(dx * dx + dy * dy) - dialRadius;
  const float plateDist =
      RoundedRectSdfFast(px, py, width * 0.5f, height - 33.0f, 124.0f, 30.0f, 15.0f);
  return std::min(circleDist, plateDist);
}

struct Vec3 {
  float x = 0;
  float y = 0;
  float z = 0;
};

struct Mat3 {
  float m[3][3]{};
};

Mat3 MakeRotation(double rxRad, double ryRad) {
  const float cx = static_cast<float>(std::cos(rxRad));
  const float sx = static_cast<float>(std::sin(rxRad));
  const float cy = static_cast<float>(std::cos(ryRad));
  const float sy = static_cast<float>(std::sin(ryRad));
  Mat3 r{};
  r.m[0][0] = cy;
  r.m[0][1] = sy * sx;
  r.m[0][2] = sy * cx;
  r.m[1][0] = 0.0f;
  r.m[1][1] = cx;
  r.m[1][2] = -sx;
  r.m[2][0] = -sy;
  r.m[2][1] = cy * sx;
  r.m[2][2] = cy * cx;
  return r;
}

Vec3 TransformVec(const Mat3& r, float x, float y, float z) {
  return {
      r.m[0][0] * x + r.m[0][1] * y + r.m[0][2] * z,
      r.m[1][0] * x + r.m[1][1] * y + r.m[1][2] * z,
      r.m[2][0] * x + r.m[2][1] * y + r.m[2][2] * z,
  };
}

// 整数定点 SWAR 双线性采样（零 std::lround、零循环），大幅降低每帧 CPU 耗时。
inline uint32_t FastSampleBilinearPremul(const uint32_t* pixels, int w, int h, float x, float y) {
  const int ix = static_cast<int>(std::floor(x));
  const int iy = static_cast<int>(std::floor(y));
  if (ix < -1 || iy < -1 || ix >= w || iy >= h) return 0u;
  const uint32_t fx = static_cast<uint32_t>((x - static_cast<float>(ix)) * 256.0f + 0.5f);
  const uint32_t fy = static_cast<uint32_t>((y - static_cast<float>(iy)) * 256.0f + 0.5f);
  const uint32_t ifx = 256u - fx;
  const uint32_t ify = 256u - fy;

  uint32_t c00, c10, c01, c11;
  if (ix >= 0 && iy >= 0 && ix + 1 < w && iy + 1 < h) {
    const uint32_t* row0 = pixels + iy * w + ix;
    const uint32_t* row1 = row0 + w;
    c00 = row0[0];
    c10 = row0[1];
    c01 = row1[0];
    c11 = row1[1];
  } else {
    const auto fetch = [&](int px, int py) -> uint32_t {
      return (px >= 0 && py >= 0 && px < w && py < h) ? pixels[py * w + px] : 0u;
    };
    c00 = fetch(ix, iy);
    c10 = fetch(ix + 1, iy);
    c01 = fetch(ix, iy + 1);
    c11 = fetch(ix + 1, iy + 1);
  }
  if ((c00 | c10 | c01 | c11) == 0u) return 0u;

  const uint32_t w00 = (ifx * ify) >> 8;
  const uint32_t w10 = (fx * ify) >> 8;
  const uint32_t w01 = (ifx * fy) >> 8;
  const uint32_t w11 = 256u - w00 - w10 - w01;

  const uint32_t rb =
      ((c00 & 0x00FF00FFu) * w00 + (c10 & 0x00FF00FFu) * w10 +
       (c01 & 0x00FF00FFu) * w01 + (c11 & 0x00FF00FFu) * w11 + 0x00800080u) >> 8;
  const uint32_t ga =
      (((c00 >> 8) & 0x00FF00FFu) * w00 + ((c10 >> 8) & 0x00FF00FFu) * w10 +
       ((c01 >> 8) & 0x00FF00FFu) * w01 + ((c11 >> 8) & 0x00FF00FFu) * w11 + 0x00800080u) >> 8;
  return (rb & 0x00FF00FFu) | ((ga & 0x00FF00FFu) << 8);
}

inline uint32_t FastScalePremulOpacity(uint32_t bgra, uint32_t opacity256, uint32_t rgbMul256) {
  if (bgra == 0 || opacity256 == 0) return 0;
  const uint32_t a = std::min(255u, (((bgra >> 24) * opacity256 + 128u) >> 8));
  if (a == 0) return 0;
  const uint32_t r = std::min(a, ((((bgra >> 16) & 255u) * rgbMul256 + 128u) >> 8));
  const uint32_t g = std::min(a, ((((bgra >> 8) & 255u) * rgbMul256 + 128u) >> 8));
  const uint32_t b = std::min(a, (((bgra & 255u) * rgbMul256 + 128u) >> 8));
  return (a << 24) | (r << 16) | (g << 8) | b;
}

inline uint32_t FastBlendPremulOver(uint32_t fg, uint32_t bg) {
  const uint32_t fgA = fg >> 24;
  if (fgA == 0) return bg;
  if (fgA == 255 || bg == 0) return fg;
  const uint32_t invA = 255u - fgA;
  const uint32_t outB = std::min(255u, (fg & 255u) + (((bg & 255u) * invA + 127u) / 255u));
  const uint32_t outG = std::min(255u, ((fg >> 8) & 255u) + ((((bg >> 8) & 255u) * invA + 127u) / 255u));
  const uint32_t outR = std::min(255u, ((fg >> 16) & 255u) + ((((bg >> 16) & 255u) * invA + 127u) / 255u));
  const uint32_t outA = std::min(255u, fgA + (((bg >> 24) * invA + 127u) / 255u));
  return (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

void ConfigureFormat(IDWriteFactory* dwrite, IDWriteTextFormat* format,
                     DWRITE_TEXT_ALIGNMENT align) {
  if (!format) return;
  format->SetTextAlignment(align);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  DWRITE_TRIMMING trimOpt{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  winrt::com_ptr<IDWriteInlineObject> ellipsis;
  if (dwrite && SUCCEEDED(dwrite->CreateEllipsisTrimmingSign(format, ellipsis.put()))) {
    format->SetTrimming(&trimOpt, ellipsis.get());
  }
}

void DrawWithFormat(ID2D1DCRenderTarget* rt, IDWriteTextFormat* format, const std::wstring& text,
                    D2D1_RECT_F rect, D2D1_COLOR_F color,
                    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_CENTER) {
  if (!format) return;
  format->SetTextAlignment(align);
  winrt::com_ptr<ID2D1SolidColorBrush> brush;
  if (SUCCEEDED(rt->CreateSolidColorBrush(color, brush.put()))) {
    rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush.get());
  }
}

void DrawFlipTile(ID2D1DCRenderTarget* rt, IDWriteTextFormat* digitsFormat, float x, float y,
                  float w, float h, const std::wstring& oldDigits, const std::wstring& newDigits,
                  double flipProgress, const ClockTheme& theme) {
  const float radius = 12.0f;
  const float midY = y + h * 0.5f;

  winrt::com_ptr<ID2D1SolidColorBrush> topHalfBrush;
  winrt::com_ptr<ID2D1SolidColorBrush> botHalfBrush;
  winrt::com_ptr<ID2D1SolidColorBrush> borderBrush;
  winrt::com_ptr<ID2D1SolidColorBrush> hingeBrush;
  rt->CreateSolidColorBrush(theme.tileTop, topHalfBrush.put());
  rt->CreateSolidColorBrush(theme.tileBottom, botHalfBrush.put());
  rt->CreateSolidColorBrush(theme.tileBorder, borderBrush.put());
  rt->CreateSolidColorBrush(theme.hinge, hingeBrush.put());

  const D2D1_ROUNDED_RECT tileRect{D2D1::RectF(x, y, x + w, y + h), radius, radius};
  rt->FillRoundedRectangle(tileRect, botHalfBrush.get());
  rt->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, midY), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  rt->FillRoundedRectangle(tileRect, topHalfBrush.get());
  rt->PopAxisAlignedClip();

  const double p = std::clamp(flipProgress, 0.0, 1.0);
  const auto drawHalfText = [&](const std::wstring& digits, float topClip, float botClip,
                                float yScale, float brightness) {
    if (yScale <= 0.04f) return;
    rt->PushAxisAlignedClip(D2D1::RectF(x, topClip, x + w, botClip),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_MATRIX_3X2_F prev;
    rt->GetTransform(&prev);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(1.0f, yScale, D2D1::Point2F(x + w * 0.5f, midY)) *
                     prev);
    D2D1_COLOR_F col = theme.digit;
    col.a *= brightness;
    DrawWithFormat(rt, digitsFormat, digits, D2D1::RectF(x, y - 2.0f, x + w, y + h + 2.0f), col);
    rt->SetTransform(prev);
    rt->PopAxisAlignedClip();
  };

  if (p >= 0.999) {
    DrawWithFormat(rt, digitsFormat, newDigits, D2D1::RectF(x, y - 2.0f, x + w, y + h + 2.0f),
                   theme.digit);
  } else if (p < 0.5) {
    drawHalfText(newDigits, y, midY, 1.0f, 0.92f);
    drawHalfText(oldDigits, midY, y + h, 1.0f, 0.95f);
    const float foldScale = static_cast<float>(std::cos(p * kPi));
    const float flapBottom = midY - (midY - y) * foldScale;
    rt->PushAxisAlignedClip(D2D1::RectF(x, flapBottom, x + w, midY),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->FillRoundedRectangle(tileRect, topHalfBrush.get());
    rt->PopAxisAlignedClip();
    drawHalfText(oldDigits, flapBottom, midY, foldScale, 0.65f + 0.35f * foldScale);
  } else {
    drawHalfText(newDigits, y, midY, 1.0f, 0.98f);
    drawHalfText(oldDigits, midY, y + h, 1.0f, 0.65f);
    const float unfoldScale = static_cast<float>(std::sin((p - 0.5) * kPi));
    const float flapBottom = midY + (y + h - midY) * unfoldScale;
    rt->PushAxisAlignedClip(D2D1::RectF(x, midY, x + w, flapBottom),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->FillRoundedRectangle(tileRect, botHalfBrush.get());
    rt->PopAxisAlignedClip();
    drawHalfText(newDigits, midY, flapBottom, unfoldScale, 0.72f + 0.28f * unfoldScale);
  }

  rt->DrawRoundedRectangle(tileRect, borderBrush.get(), 1.0f);
  rt->DrawLine(D2D1::Point2F(x + 2.0f, midY), D2D1::Point2F(x + w - 2.0f, midY), hingeBrush.get(),
               1.6f);
  winrt::com_ptr<ID2D1SolidColorBrush> accentBrush;
  rt->CreateSolidColorBrush(D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b, 0.7f),
                            accentBrush.put());
  if (accentBrush) {
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 4.0f, midY), 1.8f, 3.2f), accentBrush.get());
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + w - 4.0f, midY), 1.8f, 3.2f), accentBrush.get());
  }
}

}  // namespace

VisualAlert::VisualAlert(winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

VisualAlert::~VisualAlert() { StopAll(); }

void VisualAlert::Trigger(const BoundaryAlert& alert) {
  const auto& config = Services::Instance().config();
  const auto& notify = config.notifications;
  const auto& pres = config.presentation;
  if (!notify.enabled || notify.muted) return;
  if (!notify.screenEdgeFlash && !notify.dropClockAlert) return;

  const HMONITOR monitor = DisplayUtils::FindMonitor(pres.monitorId);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return;
  if (pres.hideInFullscreen && DisplayUtils::IsMonitorInFullscreen(monitor)) return;

  const RECT area = pres.coverTaskbar ? info.rcMonitor : info.rcWork;
  const UINT dpi = GetDpiForSystem();
  const double dpiScale = dpi > 0 ? static_cast<double>(dpi) / 96.0 : 1.0;

  if (notify.screenEdgeFlash) StartFlash(alert, area, dpiScale);
  if (notify.dropClockAlert) StartClock(alert, area, dpiScale);
}

bool VisualAlert::TriggerPreview() {
  const auto& config = Services::Instance().config();
  const auto& notify = config.notifications;
  const auto& pres = config.presentation;
  if (!notify.screenEdgeFlash && !notify.dropClockAlert) return false;

  const HMONITOR monitor = DisplayUtils::FindMonitor(pres.monitorId);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return false;

  // 实时计算当前时刻对应的日程阶段，避免读取未刷新或滞后的缓存快照。
  const auto now = NowClock();
  const auto plan = Services::Instance().PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  BoundaryAlert alert;
  if (snapshot.current.has_value()) {
    const auto& seg = *snapshot.current;
    alert.accentKind = seg.kind;
    alert.clockTime = classtime::TimeOfDay{now.hour, now.minute};
    alert.rangeText = FormatClock(seg.start) + L"–" + FormatClock(seg.end);
    alert.stageBadge = seg.kind == classtime::SegmentKind::Focus
                           ? L"第 " + std::to_wstring(seg.focusOrdinal) + L" 节"
                           : SegmentKindLabel(seg.kind);
    alert.title = L"阶段到点预览";
    alert.body = ToWide(seg.label);
  } else if (snapshot.next.has_value()) {
    const auto& seg = *snapshot.next;
    alert.accentKind = seg.kind;
    alert.clockTime = seg.start;
    alert.rangeText = FormatClock(seg.start) + L"–" + FormatClock(seg.end);
    alert.stageBadge = seg.kind == classtime::SegmentKind::Focus ? L"即将上课" : SegmentKindLabel(seg.kind);
    alert.title = L"阶段到点预览";
    alert.body = ToWide(seg.label);
  } else {
    alert.accentKind = classtime::SegmentKind::Focus;
    alert.clockTime = classtime::TimeOfDay{now.hour, now.minute};
    alert.stageBadge = L"第 1 节";
    alert.title = L"开始啦";
    alert.body = L"到点时将以这种视觉效果提醒你";
    alert.rangeText = L"08:00–08:45";
  }

  const RECT area = pres.coverTaskbar ? info.rcMonitor : info.rcWork;
  const UINT dpi = GetDpiForSystem();
  const double dpiScale = dpi > 0 ? static_cast<double>(dpi) / 96.0 : 1.0;

  if (notify.screenEdgeFlash) StartFlash(alert, area, dpiScale);
  if (notify.dropClockAlert) StartClock(alert, area, dpiScale);
  return true;
}

void VisualAlert::StopAll() {
  StopFlash();
  StopClock();
}

// —— 效果 A：四边柔光呼吸闪光 ——

void VisualAlert::StartFlash(const BoundaryAlert& alert, const RECT& area, double dpiScale) {
  StopFlash();
  const int width = area.right - area.left;
  const int height = area.bottom - area.top;
  if (width < 120 || height < 120) return;

  const int glowDepth = std::clamp<int>(static_cast<int>(std::lround(76.0 * dpiScale)),
                                        48, std::min(width, height) / 4);
  const int rimPixels = std::max(2, static_cast<int>(std::lround(4.0 * dpiScale)));
  const RECT stripBounds[4] = {
      {area.left, area.top, area.right, area.top + glowDepth},
      {area.left, area.bottom - glowDepth, area.right, area.bottom},
      {area.left, area.top + glowDepth, area.left + glowDepth, area.bottom - glowDepth},
      {area.right - glowDepth, area.top + glowDepth, area.right, area.bottom - glowDepth},
  };

  const auto& config = Services::Instance().config();
  const auto palette = WidgetAppearance::ResolvePalette(config.presentation);
  const auto phaseAccent = WidgetAppearance::KindColor(palette, alert.accentKind);
  const auto glowColor =
      WidgetAppearance::ResolveClockAppearance(config.notifications, phaseAccent).flash;

  // 预计算距离查找表，避免每个像素重复 pow / lround。
  std::vector<uint32_t> lut(glowDepth, 0u);
  for (int dist = 0; dist < glowDepth; ++dist) {
    double alphaFactor = 0.0;
    if (dist < rimPixels) {
      alphaFactor = 0.94 - 0.12 * (static_cast<double>(dist) / rimPixels);
    } else {
      const double u = static_cast<double>(dist - rimPixels) /
                       std::max(1, glowDepth - rimPixels);
      alphaFactor = 0.80 * std::pow(1.0 - std::clamp(u, 0.0, 1.0), 2.35);
    }
    lut[dist] = PremultiplyColor(glowColor, alphaFactor);
  }

  for (size_t i = 0; i < 4; ++i) {
    auto& strip = flashStrips_[i];
    strip.bounds = stripBounds[i];
    const int sw = strip.bounds.right - strip.bounds.left;
    const int sh = strip.bounds.bottom - strip.bounds.top;
    if (sw <= 0 || sh <= 0) {
      StopFlash();
      return;
    }
    void* rawPixels = nullptr;
    if (!Create32BitDib(sw, sh, &strip.dc, &strip.bitmap, &strip.previousBitmap, &rawPixels)) {
      StopFlash();
      return;
    }
    auto* pixels = static_cast<uint32_t*>(rawPixels);
    for (int y = 0; y < sh; ++y) {
      const int sy = strip.bounds.top + y;
      const int distY = std::min(sy - area.top, area.bottom - 1 - sy);
      uint32_t* row = pixels + y * sw;
      for (int x = 0; x < sw; ++x) {
        const int sx = strip.bounds.left + x;
        const int distX = std::min(sx - area.left, area.right - 1 - sx);
        const int dist = std::min(distY, distX);
        row[x] = (dist >= 0 && dist < glowDepth) ? lut[dist] : 0u;
      }
    }

    strip.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"STATIC", L"ClassTime Flash Edge", WS_POPUP,
        strip.bounds.left, strip.bounds.top, sw, sh, nullptr, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    if (!strip.hwnd) {
      StopFlash();
      return;
    }
  }

  flashAnimated_ = Motion::Enabled();
  flashStart_ = std::chrono::steady_clock::now();
  if (!flashTimer_ && dispatcher_) {
    flashTimer_ = dispatcher_.CreateTimer();
    flashTimer_.IsRepeating(true);
    flashTimer_.Interval(std::chrono::milliseconds(16));
    flashTimer_.Tick([this](auto const&, auto const&) { TickFlash(); });
  }
  TickFlash();
  for (auto& strip : flashStrips_) {
    if (strip.hwnd) ShowWindow(strip.hwnd, SW_SHOWNOACTIVATE);
  }
  if (flashTimer_) flashTimer_.Start();
}

void VisualAlert::TickFlash() {
  const double elapsedMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - flashStart_)
                               .count();
  const double durationMs = flashAnimated_ ? 2600.0 : 1200.0;
  if (elapsedMs >= durationMs) {
    StopFlash();
    return;
  }

  BYTE alpha = 155;
  if (flashAnimated_) {
    const double t = std::clamp(elapsedMs / durationMs, 0.0, 1.0);
    const double wave = std::sin(3.0 * kPi * t);
    alpha = static_cast<BYTE>(std::clamp<long>(
        std::lround(255.0 * wave * wave * (1.0 - 0.18 * t)), 0, 255));
  }

  for (const auto& strip : flashStrips_) {
    if (!strip.hwnd || !strip.dc) continue;
    SIZE size{strip.bounds.right - strip.bounds.left, strip.bounds.bottom - strip.bounds.top};
    POINT origin{};
    POINT pos{strip.bounds.left, strip.bounds.top};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(strip.hwnd, nullptr, &pos, &size, strip.dc, &origin, 0, &blend, ULW_ALPHA);
  }
}

void VisualAlert::StopFlash() {
  if (flashTimer_) flashTimer_.Stop();
  for (auto& strip : flashStrips_) {
    if (strip.hwnd) DestroyWindow(strip.hwnd);
    ReleaseDib(&strip.dc, &strip.bitmap, &strip.previousBitmap, nullptr);
    strip = {};
  }
}

// —— 效果 B：掉落放大 3D 电子 / 机械时钟 ——

bool VisualAlert::EnsureClockResources(int pxWidth, int pxHeight, double dpiScale) {
  if (!d2dFactory_) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.put()))) {
      return false;
    }
  }
  if (!d2dTarget_) {
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(d2dFactory_->CreateDCRenderTarget(&props, d2dTarget_.put()))) return false;
  }
  if (!dwriteFactory_) {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwriteFactory_.put())))) {
      return false;
    }
  }

  if (!fmtDigitalDigits_) {
    dwriteFactory_->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 50.0f,
                                     L"zh-CN", fmtDigitalDigits_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtDigitalDigits_.get(), DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (!fmtDigitalBadge_) {
    dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f,
                                     L"zh-CN", fmtDigitalBadge_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtDigitalBadge_.get(), DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (!fmtDigitalTitle_) {
    dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f,
                                     L"zh-CN", fmtDigitalTitle_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtDigitalTitle_.get(), DWRITE_TEXT_ALIGNMENT_LEADING);
  }
  if (!fmtDigitalFooter_) {
    dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f,
                                     L"zh-CN", fmtDigitalFooter_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtDigitalFooter_.get(), DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (!fmtMechNumeral_) {
    dwriteFactory_->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f,
                                     L"zh-CN", fmtMechNumeral_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtMechNumeral_.get(), DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (!fmtMechSmall_) {
    dwriteFactory_->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f,
                                     L"zh-CN", fmtMechSmall_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtMechSmall_.get(), DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (!fmtMechTitle_) {
    dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f,
                                     L"zh-CN", fmtMechTitle_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtMechTitle_.get(), DWRITE_TEXT_ALIGNMENT_LEADING);
  }
  if (!fmtMechFooter_) {
    dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.2f,
                                     L"zh-CN", fmtMechFooter_.put());
    ConfigureFormat(dwriteFactory_.get(), fmtMechFooter_.get(), DWRITE_TEXT_ALIGNMENT_LEADING);
  }

  const int stagePadPx = static_cast<int>(std::lround(kClockStagePadDip * dpiScale));
  const int outW = pxWidth + 2 * stagePadPx;
  const int outH = pxHeight + 2 * stagePadPx;
  if (clockCanvasPxW_ != pxWidth || clockCanvasPxH_ != pxHeight || !clockCanvasDc_) {
    ReleaseDib(&clockCanvasDc_, &clockCanvasBmp_, &clockCanvasPrev_, &clockCanvasPixels_);
    if (!Create32BitDib(pxWidth, pxHeight, &clockCanvasDc_, &clockCanvasBmp_, &clockCanvasPrev_,
                        &clockCanvasPixels_)) {
      return false;
    }
    clockCanvasPxW_ = pxWidth;
    clockCanvasPxH_ = pxHeight;
    clockFaceDirty_ = true;
  }
  if (clockOutPxW_ != outW || clockOutPxH_ != outH || !clockOutDc_) {
    ReleaseDib(&clockOutDc_, &clockOutBmp_, &clockOutPrev_, &clockOutPixels_);
    if (!Create32BitDib(outW, outH, &clockOutDc_, &clockOutBmp_, &clockOutPrev_,
                        &clockOutPixels_)) {
      return false;
    }
    clockOutPxW_ = outW;
    clockOutPxH_ = outH;
  }
  return true;
}

void VisualAlert::ReleaseClockResources() {
  ReleaseDib(&clockCanvasDc_, &clockCanvasBmp_, &clockCanvasPrev_, &clockCanvasPixels_);
  ReleaseDib(&clockOutDc_, &clockOutBmp_, &clockOutPrev_, &clockOutPixels_);
  clockCanvasPxW_ = clockCanvasPxH_ = 0;
  clockOutPxW_ = clockOutPxH_ = 0;
  fmtDigitalDigits_ = nullptr;
  fmtDigitalBadge_ = nullptr;
  fmtDigitalTitle_ = nullptr;
  fmtDigitalFooter_ = nullptr;
  fmtMechNumeral_ = nullptr;
  fmtMechSmall_ = nullptr;
  fmtMechTitle_ = nullptr;
  fmtMechFooter_ = nullptr;
  d2dTarget_ = nullptr;
  dwriteFactory_ = nullptr;
  d2dFactory_ = nullptr;
}

void VisualAlert::StartClock(const BoundaryAlert& alert, const RECT& area, double dpiScale) {
  StopClock();
  RegisterClockClass();

  const auto& config = Services::Instance().config();
  const auto& notify = config.notifications;
  const auto palette = WidgetAppearance::ResolvePalette(config.presentation);
  clockAlert_ = alert;
  clockStyle_ = notify.dropClockStyle;
  clockPosition_ = notify.dropClockPosition;
  clockAppearance_ = WidgetAppearance::ResolveClockAppearance(
      notify, WidgetAppearance::KindColor(palette, alert.accentKind));
  clockFaceDirty_ = true;
  lastRenderedFlip_ = -1.0;
  lastRenderedSweep_ = -1.0;

  clockDpiScale_ = dpiScale;
  clockCardWidthDip_ =
      !notify.IsDial(clockStyle_) ? kDigitalWidthDip : kMechanicalWidthDip;
  clockCardHeightDip_ =
      !notify.IsDial(clockStyle_) ? kDigitalHeightDip : kMechanicalHeightDip;

  // 先在目标显示器创建无尺寸弹窗以获得该显示器的精确 DPI。
  clockHwnd_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
      kDropClockWindowClass, L"ClassTime Alert Clock", WS_POPUP,
      area.left, area.top, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (!clockHwnd_) return;
  if (const UINT winDpi = GetDpiForWindow(clockHwnd_); winDpi > 0) {
    clockDpiScale_ = static_cast<double>(winDpi) / 96.0;
  }

  clockDpiScale_ *= notify.dropClockScalePercent / 100.0;
  clockHoldDuration_ = std::chrono::seconds(notify.dropClockHoldSeconds);
  const int cardPxW = static_cast<int>(std::lround(clockCardWidthDip_ * clockDpiScale_));
  const int cardPxH = static_cast<int>(std::lround(clockCardHeightDip_ * clockDpiScale_));
  if (!EnsureClockResources(cardPxW, cardPxH, clockDpiScale_)) {
    StopClock();
    return;
  }

  const int marginPx = static_cast<int>(std::lround(26.0 * clockDpiScale_));
  const int padPx = static_cast<int>(std::lround(kClockStagePadDip * clockDpiScale_));
  const int winW = clockOutPxW_;
  const int winH = clockOutPxH_;
  int cardLeft = area.left + (area.right - area.left - cardPxW) / 2;
  if (clockPosition_ == ClockPosition::TopLeft) {
    cardLeft = area.left + marginPx;
  } else if (clockPosition_ == ClockPosition::TopRight) {
    cardLeft = area.right - cardPxW - marginPx;
  }
  const int cardTop = area.top + marginPx;
  clockAnchorRect_ = {cardLeft - padPx, cardTop - padPx, cardLeft + cardPxW + padPx,
                      cardTop + cardPxH + padPx};
  SetWindowPos(clockHwnd_, HWND_TOPMOST, clockAnchorRect_.left, clockAnchorRect_.top, winW, winH,
               SWP_NOACTIVATE);

  clockAnimated_ = Motion::Enabled();
  clockHovered_ = false;
  clockTrackingMouse_ = false;
  clockHoldElapsed_ = {};
  clockPhase_ = clockAnimated_ ? ClockPhase::Intro : ClockPhase::Hold;
  clockPhaseStart_ = std::chrono::steady_clock::now();

  if (!clockTimer_ && dispatcher_) {
    clockTimer_ = dispatcher_.CreateTimer();
    clockTimer_.IsRepeating(true);
    clockTimer_.Interval(std::chrono::milliseconds(16));
    clockTimer_.Tick([this](auto const&, auto const&) { TickClock(); });
  }
  TickClock();
  if (clockHwnd_) ShowWindow(clockHwnd_, SW_SHOWNOACTIVATE);
  if (clockTimer_) clockTimer_.Start();
}

void VisualAlert::RenderClockFrame(double rotXDeg, double rotYDeg, double scale, double dropDip,
                                   double flipProgress, double sweepProgress, double opacity) {
  if (!clockHwnd_ || !d2dTarget_ || !dwriteFactory_ || !clockCanvasDc_ || !clockOutDc_) return;

  const ClockTheme theme = ToClockTheme(clockAppearance_);
  const bool needRedrawFace =
      clockFaceDirty_ ||
      (clockStyle_ == ClockStyle::Digital3D && std::fabs(flipProgress - lastRenderedFlip_) > 1e-4) ||
      (classtime::NotificationSettings::IsDial(clockStyle_) &&
       std::fabs(sweepProgress - lastRenderedSweep_) > 1e-4);

  if (needRedrawFace) {
    const RECT bindRect{0, 0, clockCanvasPxW_, clockCanvasPxH_};
    if (FAILED(d2dTarget_->BindDC(clockCanvasDc_, &bindRect))) return;
    d2dTarget_->SetDpi(static_cast<float>(96.0 * clockDpiScale_),
                       static_cast<float>(96.0 * clockDpiScale_));

    d2dTarget_->BeginDraw();
    d2dTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    if (!classtime::NotificationSettings::IsDial(clockStyle_)) {
      const float w = static_cast<float>(kDigitalWidthDip);
      const float h = static_cast<float>(kDigitalHeightDip);
      D2D1_GRADIENT_STOP bodyStops[] = {
          {0.0f, theme.bodyTop},
          {0.55f, theme.bodyMid},
          {1.0f, theme.bodyBottom},
      };
      winrt::com_ptr<ID2D1GradientStopCollection> stopCol;
      winrt::com_ptr<ID2D1LinearGradientBrush> bodyBrush;
      if (SUCCEEDED(d2dTarget_->CreateGradientStopCollection(bodyStops, 3, stopCol.put()))) {
        d2dTarget_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, h}),
                                              stopCol.get(), bodyBrush.put());
      }
      const D2D1_ROUNDED_RECT bodyRect{D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f), 22.0f, 22.0f};
      if (bodyBrush) d2dTarget_->FillRoundedRectangle(bodyRect, bodyBrush.get());

      winrt::com_ptr<ID2D1SolidColorBrush> accentSolid;
      d2dTarget_->CreateSolidColorBrush(theme.accent, accentSolid.put());
      if (accentSolid) {
        d2dTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(84.0f, 7.0f, w - 84.0f, 11.5f), 2.2f, 2.2f),
            accentSolid.get());
      }

      D2D1_GRADIENT_STOP glossStops[] = {
          {0.0f, theme.glossTop},
          {0.5f, theme.glossMid},
          {1.0f, D2DRgba(255, 255, 255, 0.0f)},
      };
      winrt::com_ptr<ID2D1GradientStopCollection> glossCol;
      winrt::com_ptr<ID2D1LinearGradientBrush> glossBrush;
      if (SUCCEEDED(d2dTarget_->CreateGradientStopCollection(glossStops, 3, glossCol.put()))) {
        d2dTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties({0, 0}, {w * 0.55f, h * 0.65f}), glossCol.get(),
            glossBrush.put());
        if (glossBrush) {
          d2dTarget_->PushAxisAlignedClip(D2D1::RectF(2.0f, 2.0f, w - 2.0f, h * 0.55f),
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
          d2dTarget_->FillRoundedRectangle(bodyRect, glossBrush.get());
          d2dTarget_->PopAxisAlignedClip();
        }
      }

      winrt::com_ptr<ID2D1SolidColorBrush> rimBrush;
      d2dTarget_->CreateSolidColorBrush(theme.rim, rimBrush.put());
      if (rimBrush) d2dTarget_->DrawRoundedRectangle(bodyRect, rimBrush.get(), 1.2f);

      const int curMinutes = clockAlert_.clockTime.TotalMinutes();
      const int prevMinutes = (curMinutes + 1439) % 1440;
      wchar_t oldHour[8]{}, oldMin[8]{}, newHour[8]{}, newMin[8]{};
      swprintf_s(oldHour, L"%02d", prevMinutes / 60);
      swprintf_s(oldMin, L"%02d", prevMinutes % 60);
      swprintf_s(newHour, L"%02d", curMinutes / 60);
      swprintf_s(newMin, L"%02d", curMinutes % 60);
      const double hourFlip = std::wcscmp(oldHour, newHour) == 0 ? 1.0 : flipProgress;

      if (clockStyle_ == ClockStyle::Panel3D) {
        winrt::com_ptr<ID2D1SolidColorBrush> face;
        d2dTarget_->CreateSolidColorBrush(theme.tileBottom, face.put());
        const auto panel = D2D1::RoundedRect(D2D1::RectF(24, 22, w - 24, 106), 10, 10);
        if (face) d2dTarget_->FillRoundedRectangle(panel, face.get());
        if (rimBrush) d2dTarget_->DrawRoundedRectangle(panel, rimBrush.get(), 1.2f);
        DrawWithFormat(d2dTarget_.get(), fmtDigitalDigits_.get(),
                       std::wstring(newHour) + L":" + newMin, panel.rect, theme.digit);
      } else {
        DrawFlipTile(d2dTarget_.get(), fmtDigitalDigits_.get(), 26, 22, 108, 84, oldHour,
                     newHour, hourFlip, theme);
        DrawFlipTile(d2dTarget_.get(), fmtDigitalDigits_.get(), 166, 22, 108, 84, oldMin,
                     newMin, flipProgress, theme);
        if (accentSolid) {
          for (float y : {52.0f, 76.0f}) {
            d2dTarget_->FillEllipse(D2D1::Ellipse({150, y}, 4, 4), accentSolid.get());
          }
        }
      }

      if (!clockAlert_.stageBadge.empty()) {
        winrt::com_ptr<ID2D1SolidColorBrush> badgeBg;
        d2dTarget_->CreateSolidColorBrush(theme.badgeBg, badgeBg.put());
        if (badgeBg) {
          d2dTarget_->FillRoundedRectangle(
              D2D1::RoundedRect(D2D1::RectF(26.0f, 118.0f, 104.0f, 140.0f), 10.0f, 10.0f),
              badgeBg.get());
        }
        if (accentSolid) {
          d2dTarget_->DrawRoundedRectangle(
              D2D1::RoundedRect(D2D1::RectF(26.0f, 118.0f, 104.0f, 140.0f), 10.0f, 10.0f),
              accentSolid.get(), 1.0f);
        }
        DrawWithFormat(d2dTarget_.get(), fmtDigitalBadge_.get(), clockAlert_.stageBadge,
                       D2D1::RectF(28.0f, 118.0f, 102.0f, 140.0f), theme.badgeText);
      }

      const float titleLeft = clockAlert_.stageBadge.empty() ? 26.0f : 114.0f;
      DrawWithFormat(
          d2dTarget_.get(), fmtDigitalTitle_.get(), clockAlert_.title,
          D2D1::RectF(titleLeft, 116.0f, w - 26.0f, 142.0f), theme.title,
          clockAlert_.stageBadge.empty() ? DWRITE_TEXT_ALIGNMENT_CENTER
                                         : DWRITE_TEXT_ALIGNMENT_LEADING);

      const std::wstring footer =
          clockAlert_.body + (clockAlert_.rangeText.empty() ? L"" : L" · " + clockAlert_.rangeText);
      DrawWithFormat(d2dTarget_.get(), fmtDigitalFooter_.get(), footer,
                     D2D1::RectF(24.0f, 143.0f, w - 24.0f, 166.0f), theme.footer);
    } else {
      const float w = static_cast<float>(kMechanicalWidthDip);
      const float h = static_cast<float>(kMechanicalHeightDip);
      const float cx = w * 0.5f;
      const float cy = static_cast<float>(kMechanicalDialDip * 0.5 + 2.0);
      const float outerR = static_cast<float>(kMechanicalDialDip * 0.5 - 2.0);

      D2D1_GRADIENT_STOP bezelStops[] = {
          {0.0f, theme.bezel0},
          {0.45f, theme.bezel1},
          {1.0f, theme.bezel2},
      };
      winrt::com_ptr<ID2D1GradientStopCollection> bezelCol;
      winrt::com_ptr<ID2D1LinearGradientBrush> bezelBrush;
      if (SUCCEEDED(d2dTarget_->CreateGradientStopCollection(bezelStops, 3, bezelCol.put()))) {
        d2dTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties({cx - outerR, cy - outerR},
                                                {cx + outerR, cy + outerR}),
            bezelCol.get(), bezelBrush.put());
      }
      if (bezelBrush) d2dTarget_->FillEllipse(D2D1::Ellipse({cx, cy}, outerR, outerR), bezelBrush.get());

      winrt::com_ptr<ID2D1SolidColorBrush> accentBrush;
      d2dTarget_->CreateSolidColorBrush(theme.accent, accentBrush.put());
      if (accentBrush) {
        d2dTarget_->DrawEllipse(D2D1::Ellipse({cx, cy}, outerR - 5.5f, outerR - 5.5f),
                                accentBrush.get(), 2.2f);
      }

      const float dialR = outerR - 9.0f;
      D2D1_GRADIENT_STOP dialStops[] = {
          {0.0f, theme.dial0},
          {0.7f, theme.dial1},
          {1.0f, theme.dial2},
      };
      winrt::com_ptr<ID2D1GradientStopCollection> dialCol;
      winrt::com_ptr<ID2D1RadialGradientBrush> dialBrush;
      if (SUCCEEDED(d2dTarget_->CreateGradientStopCollection(dialStops, 3, dialCol.put()))) {
        d2dTarget_->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties({cx - 14.0f, cy - 18.0f}, {0, 0}, dialR, dialR),
            dialCol.get(), dialBrush.put());
      }
      if (dialBrush) d2dTarget_->FillEllipse(D2D1::Ellipse({cx, cy}, dialR, dialR), dialBrush.get());

      winrt::com_ptr<ID2D1SolidColorBrush> tickMinorBrush;
      winrt::com_ptr<ID2D1SolidColorBrush> tickMajorBrush;
      d2dTarget_->CreateSolidColorBrush(theme.tickMinor, tickMinorBrush.put());
      d2dTarget_->CreateSolidColorBrush(theme.tickMajor, tickMajorBrush.put());
      for (int i = 0; i < 60; ++i) {
        const double angle = (i * 6.0 - 90.0) * kPi / 180.0;
        const bool major = (i % 5 == 0);
        const float rOuter = dialR - 5.0f;
        const float rInner = dialR - (major ? 14.5f : 9.5f);
        const float c = static_cast<float>(std::cos(angle));
        const float s = static_cast<float>(std::sin(angle));
        auto* brush = major ? tickMajorBrush.get() : tickMinorBrush.get();
        if (brush) {
          d2dTarget_->DrawLine({cx + rInner * c, cy + rInner * s},
                               {cx + rOuter * c, cy + rOuter * s}, brush, major ? 2.4f : 1.0f);
        }
      }

      const struct {
        const wchar_t* text;
        float x;
        float y;
      } numerals[] = {
          {clockStyle_ == ClockStyle::Roman3D ? L"XII" : L"12", cx - 14.0f, cy - dialR + 17.0f},
          {clockStyle_ == ClockStyle::Roman3D ? L"III" : L"3", cx + dialR - 34.0f, cy - 10.0f},
          {clockStyle_ == ClockStyle::Roman3D ? L"VI" : L"6", cx - 14.0f, cy + dialR - 36.0f},
          {clockStyle_ == ClockStyle::Roman3D ? L"IX" : L"9", cx - dialR + 14.0f, cy - 10.0f},
      };
      for (const auto& num : numerals) {
        DrawWithFormat(d2dTarget_.get(), fmtMechNumeral_.get(), num.text,
                       D2D1::RectF(num.x, num.y, num.x + 28.0f, num.y + 20.0f), theme.numeral);
      }

      wchar_t digitalSmall[8]{};
      swprintf_s(digitalSmall, L"%02d:%02d", clockAlert_.clockTime.hour,
                 clockAlert_.clockTime.minute);
      DrawWithFormat(d2dTarget_.get(), fmtMechSmall_.get(), digitalSmall,
                     D2D1::RectF(cx - 28.0f, cy + 20.0f, cx + 28.0f, cy + 36.0f),
                     theme.digitalSmall);

      const double targetMinutes = clockAlert_.clockTime.minute;
      const double sweepOffsetMin = -7.0 * (1.0 - EaseOutBack(std::clamp(sweepProgress, 0.0, 1.0)));
      const double minuteAngleDeg = (targetMinutes + sweepOffsetMin) * 6.0 - 90.0;
      const double hourAngleDeg =
          ((clockAlert_.clockTime.hour % 12) + (targetMinutes + sweepOffsetMin) / 60.0) * 30.0 - 90.0;
      const double secondAngleDeg =
          (-42.0 * (1.0 - EaseOutBack(std::clamp(sweepProgress, 0.0, 1.0)))) - 90.0;

      winrt::com_ptr<ID2D1SolidColorBrush> shadowBrush;
      d2dTarget_->CreateSolidColorBrush(D2DRgba(0, 0, 0, 0.40f), shadowBrush.put());
      const auto drawHand = [&](double deg, float length, float tail, float width,
                                D2D1_COLOR_F color) {
        const double rad = deg * kPi / 180.0;
        const float c = static_cast<float>(std::cos(rad));
        const float s = static_cast<float>(std::sin(rad));
        winrt::com_ptr<ID2D1SolidColorBrush> handBrush;
        d2dTarget_->CreateSolidColorBrush(color, handBrush.put());
        if (shadowBrush) {
          d2dTarget_->DrawLine({cx - tail * c + 1.5f, cy - tail * s + 2.5f},
                               {cx + length * c + 1.5f, cy + length * s + 2.5f},
                               shadowBrush.get(), width);
        }
        if (handBrush) {
          d2dTarget_->DrawLine({cx - tail * c, cy - tail * s},
                               {cx + length * c, cy + length * s}, handBrush.get(), width);
        }
      };
      drawHand(hourAngleDeg, dialR * 0.50f, 10.0f, 4.4f, theme.handMain);
      drawHand(minuteAngleDeg, dialR * 0.74f, 12.0f, 3.0f, theme.handMain);
      drawHand(secondAngleDeg, dialR * 0.80f, 18.0f, 1.7f, theme.accent);

      if (accentBrush) d2dTarget_->FillEllipse(D2D1::Ellipse({cx, cy}, 5.5f, 5.5f), accentBrush.get());
      if (tickMajorBrush) d2dTarget_->FillEllipse(D2D1::Ellipse({cx, cy}, 2.4f, 2.4f), tickMajorBrush.get());

      D2D1_GRADIENT_STOP glassStops[] = {
          {0.0f, D2DRgba(255, 255, 255, 0.22f)},
          {0.55f, D2DRgba(255, 255, 255, 0.03f)},
          {1.0f, D2DRgba(255, 255, 255, 0.0f)},
      };
      winrt::com_ptr<ID2D1GradientStopCollection> glassCol;
      winrt::com_ptr<ID2D1LinearGradientBrush> glassBrush;
      if (SUCCEEDED(d2dTarget_->CreateGradientStopCollection(glassStops, 3, glassCol.put()))) {
        d2dTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties({cx - dialR, cy - dialR}, {cx, cy}), glassCol.get(),
            glassBrush.put());
        if (glassBrush) {
          d2dTarget_->PushAxisAlignedClip(
              D2D1::RectF(cx - dialR, cy - dialR, cx + dialR, cy + 4.0f),
              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
          d2dTarget_->FillEllipse(D2D1::Ellipse({cx, cy}, dialR - 1.0f, dialR - 1.0f),
                                  glassBrush.get());
          d2dTarget_->PopAxisAlignedClip();
        }
      }

      const D2D1_ROUNDED_RECT plateRect{
          D2D1::RectF(4.0f, h - 63.0f, w - 4.0f, h - 3.0f), 15.0f, 15.0f};
      winrt::com_ptr<ID2D1SolidColorBrush> plateBrush;
      winrt::com_ptr<ID2D1SolidColorBrush> plateRim;
      d2dTarget_->CreateSolidColorBrush(theme.plateBg, plateBrush.put());
      d2dTarget_->CreateSolidColorBrush(theme.plateRim, plateRim.put());
      if (plateBrush) d2dTarget_->FillRoundedRectangle(plateRect, plateBrush.get());
      if (plateRim) d2dTarget_->DrawRoundedRectangle(plateRect, plateRim.get(), 1.0f);
      if (accentBrush) {
        d2dTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(14.0f, h - 49.0f, 18.0f, h - 17.0f), 2.0f, 2.0f),
            accentBrush.get());
      }

      const std::wstring heading = clockAlert_.stageBadge.empty()
                                       ? clockAlert_.title
                                       : clockAlert_.stageBadge + L" · " + clockAlert_.title;
      DrawWithFormat(d2dTarget_.get(), fmtMechTitle_.get(), heading,
                     D2D1::RectF(26.0f, h - 57.0f, w - 12.0f, h - 33.0f), theme.title,
                     DWRITE_TEXT_ALIGNMENT_LEADING);
      const std::wstring footer =
          clockAlert_.body + (clockAlert_.rangeText.empty() ? L"" : L" · " + clockAlert_.rangeText);
      DrawWithFormat(d2dTarget_.get(), fmtMechFooter_.get(), footer,
                     D2D1::RectF(26.0f, h - 34.0f, w - 12.0f, h - 10.0f), theme.footer,
                     DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (FAILED(d2dTarget_->EndDraw())) return;
    clockFaceDirty_ = false;
    lastRenderedFlip_ = flipProgress;
    lastRenderedSweep_ = sweepProgress;
  }

  // —— 增量式 3×3 透视逆投影 + 4 层局部侧壁挤压 + 快速整数像素合成 ——
  const int outW = clockOutPxW_;
  const int outH = clockOutPxH_;
  auto* outPixels = static_cast<uint32_t*>(clockOutPixels_);
  std::fill_n(outPixels, static_cast<size_t>(outW) * outH, 0u);

  const auto* frontPixels = static_cast<const uint32_t*>(clockCanvasPixels_);
  const float dpiF = static_cast<float>(clockDpiScale_);
  const float focal = 780.0f * dpiF;
  const float safeScale = static_cast<float>(std::max(0.25, scale));
  const float invScale = 1.0f / safeScale;
  const float invScaleDpi = 1.0f / (safeScale * dpiF);
  const float originX = outW * 0.5f;
  const float originY = outH * 0.5f + static_cast<float>(dropDip) * dpiF;
  const Mat3 rot = MakeRotation(rotXDeg * kPi / 180.0, rotYDeg * kPi / 180.0);

  const Vec3 normal = TransformVec(rot, 0.0f, 0.0f, -1.0f);
  const float frontLight =
      std::clamp(0.86f + 0.18f * (-normal.z) - 0.08f * normal.y, 0.72f, 1.08f);
  const uint32_t opacity256 =
      static_cast<uint32_t>(std::clamp(static_cast<int>(opacity * 256.0 + 0.5), 0, 256));
  const uint32_t rgbMul256 =
      static_cast<uint32_t>(std::clamp(static_cast<int>(opacity * frontLight * 256.0 + 0.5), 0, 276));

  const float depthPx = 14.0f * dpiF;
  const float halfW = clockCanvasPxW_ * 0.5f;
  const float halfH = clockCanvasPxH_ * 0.5f;
  const float cardWDip = static_cast<float>(clockCardWidthDip_);
  const float cardHDip = static_cast<float>(clockCardHeightDip_);
  const float halfWDip = cardWDip * 0.5f;
  const float halfHDip = cardHDip * 0.5f;

  const float boundMargin = std::round(42.0f * dpiF);
  const int minX = std::max(0, static_cast<int>(std::floor(originX - halfW * safeScale - boundMargin)));
  const int maxX = std::min(outW, static_cast<int>(std::ceil(originX + halfW * safeScale + boundMargin)));
  const int minY = std::max(0, static_cast<int>(std::floor(originY - halfH * safeScale - boundMargin)));
  const int maxY = std::min(outH, static_cast<int>(std::ceil(originY + halfH * safeScale + boundMargin)));

  // 预计算增量步进系数（每像素仅 3 次加法 + 1 次浮点除法）
  const float invFocal = 1.0f / focal;
  const float nux = rot.m[0][0] * invFocal;
  const float nuy = rot.m[1][0] * invFocal;
  const float nvx = rot.m[0][1] * invFocal;
  const float nvy = rot.m[1][1] * invFocal;
  const float wx = rot.m[0][2] * invFocal;
  const float wy = rot.m[1][2] * invFocal;

  const float dx0 = (minX + 0.5f - originX) * invFocal;
  const float dy0 = (minY + 0.5f - originY) * invFocal;
  float nuRow = rot.m[0][0] * dx0 + rot.m[1][0] * dy0 + rot.m[2][0];
  float nvRow = rot.m[0][1] * dx0 + rot.m[1][1] * dy0 + rot.m[2][1];
  float dwRow = rot.m[0][2] * dx0 + rot.m[1][2] * dy0 + rot.m[2][2];

  const float fR20 = focal * rot.m[2][0];
  const float fR21 = focal * rot.m[2][1];
  const float k0 = focal * rot.m[2][2];

  float kExt[4];
  float shadeTopVal[4];
  float shadeBotVal[4];
  for (int s = 0; s < 4; ++s) {
    const float frac = (s + 1) * 0.25f;
    kExt[s] = depthPx * frac + k0;
    const float baseShade = 1.0f - 0.55f * frac;
    shadeTopVal[s] = std::clamp(baseShade * 1.15f, 0.35f, 1.05f);
    shadeBotVal[s] = std::clamp(baseShade * 0.85f, 0.35f, 1.05f);
  }
  const float kShad = depthPx + k0;
  const float shadowOffD = (12.0f * dpiF) * invFocal;
  const float sOffNu = rot.m[1][0] * shadowOffD;
  const float sOffNv = rot.m[1][1] * shadowOffD;
  const float sOffDw = rot.m[1][2] * shadowOffD;

  const float maxPxX = static_cast<float>(clockCanvasPxW_);
  const float maxPxY = static_cast<float>(clockCanvasPxH_);
  const float fOpacity = static_cast<float>(opacity);
  const int sideBaseR = theme.sideR;
  const int sideBaseG = theme.sideG;
  const int sideBaseB = theme.sideB;

  for (int py = minY; py < maxY; ++py) {
    float nu = nuRow;
    float nv = nvRow;
    float dw = dwRow;
    uint32_t* outRow = outPixels + py * outW;

    for (int px = minX; px < maxX; ++px, nu += nux, nv += nvx, dw += wx) {
      if (dw <= 0.12f) continue;
      const float invDw = 1.0f / dw;
      const float u = nu * invDw;
      const float v = nv * invDw;
      const float sx = (k0 * u - fR20) * invScale + halfW;
      const float sy = (k0 * v - fR21) * invScale + halfH;

      uint32_t accum = 0;
      if (sx > -1.0f && sx < maxPxX && sy > -1.0f && sy < maxPxY) {
        const uint32_t sample = FastSampleBilinearPremul(frontPixels, clockCanvasPxW_, clockCanvasPxH_, sx, sy);
        if ((sample >> 24) > 0) {
          accum = FastScalePremulOpacity(sample, opacity256, rgbMul256);
        }
      }

      if ((accum >> 24) < 252) {
        // 仅当像素可能落在前后卡片投影交错区内时才步进 4 层厚边。
        const float dipX0 = (k0 * u - fR20) * invScaleDpi + halfWDip;
        const float dipY0 = (k0 * v - fR21) * invScaleDpi + halfHDip;
        const float dipX3 = (kExt[3] * u - fR20) * invScaleDpi + halfWDip;
        const float dipY3 = (kExt[3] * v - fR21) * invScaleDpi + halfHDip;
        const bool nearBodyX = !(dipX0 < -2.0f && dipX3 < -2.0f) &&
                               !(dipX0 > cardWDip + 2.0f && dipX3 > cardWDip + 2.0f);
        const bool nearBodyY = !(dipY0 < -2.0f && dipY3 < -2.0f) &&
                               !(dipY0 > cardHDip + 2.0f && dipY3 > cardHDip + 2.0f);

        if (nearBodyX && nearBodyY) {
          uint32_t sideColor = 0;
          for (int s = 0; s < 4; ++s) {
            const float dipX = (kExt[s] * u - fR20) * invScaleDpi + halfWDip;
            const float dipY = (kExt[s] * v - fR21) * invScaleDpi + halfHDip;
            const float sdf = CardBodySdfFast(dipX, dipY, cardWDip, cardHDip, clockStyle_);
            if (sdf <= 0.4f) {
              const float val = dipY < cardHDip * 0.45f ? shadeTopVal[s] : shadeBotVal[s];
              const uint32_t r = static_cast<uint32_t>(std::min(255, static_cast<int>(sideBaseR * val + 0.5f)));
              const uint32_t g = static_cast<uint32_t>(std::min(255, static_cast<int>(sideBaseG * val + 0.5f)));
              const uint32_t b = static_cast<uint32_t>(std::min(255, static_cast<int>(sideBaseB * val + 0.5f)));
              const float edgeCov = std::clamp((0.4f - sdf) / 1.1f, 0.0f, 1.0f);
              const uint32_t a = static_cast<uint32_t>(255.0f * fOpacity * edgeCov + 0.5f);
              sideColor = (a << 24) | (((r * a) / 255u) << 16) | (((g * a) / 255u) << 8) |
                          ((b * a) / 255u);
              break;
            }
          }
          if ((sideColor >> 24) > 0) accum = FastBlendPremulOver(accum, sideColor);
        }

        if ((accum >> 24) < 252) {
          const float sDw = dw - sOffDw;
          if (sDw > 0.12f) {
            const float invSDw = 1.0f / sDw;
            const float dipX = (kShad * ((nu - sOffNu) * invSDw) - fR20) * invScaleDpi + halfWDip;
            const float dipY = (kShad * ((nv - sOffNv) * invSDw) - fR21) * invScaleDpi + halfHDip;
            if (dipX >= -22.0f && dipX <= cardWDip + 22.0f &&
                dipY >= -22.0f && dipY <= cardHDip + 22.0f) {
              const float sdf = CardBodySdfFast(dipX, dipY, cardWDip, cardHDip, clockStyle_);
              if (sdf < 20.0f) {
                const float su = std::clamp(1.0f - std::max(0.0f, sdf) / 20.0f, 0.0f, 1.0f);
                const uint32_t sa = static_cast<uint32_t>(96.0f * su * su * fOpacity + 0.5f);
                if (sa > 0) accum = FastBlendPremulOver(accum, sa << 24);
              }
            }
          }
        }
      }

      outRow[px] = accum;
    }

    nuRow += nuy;
    nvRow += nvy;
    dwRow += wy;
  }

  SIZE size{outW, outH};
  POINT origin{};
  POINT pos{clockAnchorRect_.left, clockAnchorRect_.top};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(clockHwnd_, nullptr, &pos, &size, clockOutDc_, &origin, 0, &blend, ULW_ALPHA);
}

void VisualAlert::TickClock() {
  if (!clockHwnd_ || clockPhase_ == ClockPhase::Idle) return;
  const auto now = std::chrono::steady_clock::now();
  const double elapsedMs =
      std::chrono::duration<double, std::milli>(now - clockPhaseStart_).count();

  const double baseRotY = clockPosition_ == ClockPosition::TopLeft   ? 14.0
                          : clockPosition_ == ClockPosition::TopRight ? -14.0
                                                                      : 0.0;
  double rotX = -4.0;
  double rotY = baseRotY * 0.35;
  double scale = 1.0;
  double dropDip = 0.0;
  double flipProgress = 1.0;
  double sweepProgress = 1.0;
  double opacity = 1.0;

  if (!clockAnimated_) {
    rotX = 0.0;
    rotY = 0.0;
    const auto dt = now - clockPhaseStart_;
    clockPhaseStart_ = now;
    if (!clockHovered_) {
      clockHoldElapsed_ += dt;
      if (clockHoldElapsed_ >= clockHoldDuration_) {
        StopClock();
        return;
      }
    }
    if (clockFaceDirty_) {
      RenderClockFrame(rotX, rotY, scale, dropDip, flipProgress, sweepProgress, opacity);
    }
    return;
  }

  if (clockPhase_ == ClockPhase::Intro) {
    constexpr double kIntroMs = 980.0;
    const double t = std::clamp(elapsedMs / kIntroMs, 0.0, 1.0);
    const double dropT = EaseOutBack(std::clamp(elapsedMs / 680.0, 0.0, 1.0));
    const double tiltT = EaseOutCubic(std::clamp(elapsedMs / 760.0, 0.0, 1.0));
    dropDip = -88.0 * (1.0 - dropT);
    scale = 0.48 + 0.52 * dropT;
    rotX = -46.0 + (42.0 * tiltT) + 6.0 * std::sin(kPi * std::clamp(elapsedMs / 700.0, 0.0, 1.0));
    rotY = baseRotY * (1.45 - 1.10 * tiltT);
    opacity = std::clamp(elapsedMs / 180.0, 0.0, 1.0);
    flipProgress = std::clamp((elapsedMs - 340.0) / 560.0, 0.0, 1.0);
    sweepProgress = std::clamp((elapsedMs - 240.0) / 720.0, 0.0, 1.0);
    retractStartRotX_ = rotX;
    retractStartRotY_ = rotY;
    retractStartScale_ = scale;
    retractStartDropDip_ = dropDip;
    if (t >= 1.0) {
      clockPhase_ = ClockPhase::Hold;
      clockPhaseStart_ = now;
      clockHoldElapsed_ = {};
    }
  } else if (clockPhase_ == ClockPhase::Hold) {
    if (!clockHovered_) {
      clockHoldElapsed_ += now - clockPhaseStart_;
    }
    clockPhaseStart_ = now;
    const double totalHoldMs =
        std::chrono::duration<double, std::milli>(clockHoldElapsed_).count();
    const double sway = std::sin(totalHoldMs * 0.0018);
    rotX = -4.0 + 2.5 * sway;
    rotY = baseRotY * 0.35 + 4.5 * std::cos(totalHoldMs * 0.0014);
    retractStartRotX_ = rotX;
    retractStartRotY_ = rotY;
    retractStartScale_ = scale;
    retractStartDropDip_ = dropDip;
    if (!clockHovered_ && clockHoldElapsed_ >= clockHoldDuration_) {
      BeginClockRetract();
      return;
    }
  } else if (clockPhase_ == ClockPhase::Retract) {
    constexpr double kRetractMs = 520.0;
    const double t = EaseInCubic(std::clamp(elapsedMs / kRetractMs, 0.0, 1.0));
    rotX = retractStartRotX_ - 34.0 * t;
    rotY = retractStartRotY_ + baseRotY * 0.65 * t;
    scale = retractStartScale_ * (1.0 - 0.45 * t);
    dropDip = retractStartDropDip_ - 76.0 * t;
    opacity = 1.0 - t;
    if (elapsedMs >= kRetractMs) {
      StopClock();
      return;
    }
  }

  RenderClockFrame(rotX, rotY, scale, dropDip, flipProgress, sweepProgress, opacity);
}

void VisualAlert::BeginClockRetract() {
  if (clockPhase_ == ClockPhase::Idle || clockPhase_ == ClockPhase::Retract) return;
  if (!clockAnimated_) {
    StopClock();
    return;
  }
  clockPhase_ = ClockPhase::Retract;
  clockPhaseStart_ = std::chrono::steady_clock::now();
}

void VisualAlert::StopClock() {
  if (clockTimer_) clockTimer_.Stop();
  clockPhase_ = ClockPhase::Idle;
  clockHovered_ = false;
  clockTrackingMouse_ = false;
  clockFaceDirty_ = true;
  if (clockHwnd_) {
    SetWindowLongPtrW(clockHwnd_, GWLP_USERDATA, 0);
    DestroyWindow(clockHwnd_);
    clockHwnd_ = nullptr;
  }
  ReleaseClockResources();
}

LRESULT CALLBACK VisualAlert::ClockWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }
  auto* self = reinterpret_cast<VisualAlert*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
      if (self) {
        self->clockHovered_ = true;
        if (!self->clockTrackingMouse_) {
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
          TrackMouseEvent(&tme);
          self->clockTrackingMouse_ = true;
        }
      }
      return 0;
    case WM_MOUSELEAVE:
      if (self) {
        self->clockHovered_ = false;
        self->clockTrackingMouse_ = false;
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
      if (self) self->BeginClockRetract();
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace ClassTimeApp
