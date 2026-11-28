#include "pch.h"
#include "overlay_window.h"
#include "display_utils.h"
#include "services.h"
#include "ui_motion.h"
#include "widget_appearance.h"
#include "widget_state.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::UI::ViewManagement;

namespace ClassTimeApp {
namespace {

using View = classtime::PresentationSettings::View;
using WidgetStyle = classtime::WidgetStyle;

// 长条沿贴边方向的整体长度可由用户指定；默认保留原有的横、竖尺寸。
constexpr double kBarInsetDip = 17;
constexpr double kSurfaceStrokeDip = 1;
constexpr Thickness kSurfaceBorder{kSurfaceStrokeDip, kSurfaceStrokeDip,
                                   kSurfaceStrokeDip, kSurfaceStrokeDip};
constexpr double kDetailWidth = 280.0;
constexpr double kDetailTrackWidth = 194.0;
constexpr int kSnapInsetDip = 12;
constexpr double kHandleWidthDip = 84;
constexpr double kHandleHeightDip = 12;
constexpr double kHandleTrackLengthDip = 64;
constexpr double kHandleTrackThicknessDip = 4;

using WidgetAppearance::KindColor;
using WidgetAppearance::Palette;
using WidgetAppearance::ResolvePalette;
using WidgetAppearance::WithAlpha;

bool IsSideEdge(classtime::PresentationSettings::Edge edge) {
  return edge == classtime::PresentationSettings::Edge::Left ||
         edge == classtime::PresentationSettings::Edge::Right;
}

POINT EdgeShift(classtime::PresentationSettings::Edge edge, int distance) {
  switch (edge) {
    case classtime::PresentationSettings::Edge::Top: return {0, -distance};
    case classtime::PresentationSettings::Edge::Left: return {-distance, 0};
    case classtime::PresentationSettings::Edge::Right: return {distance, 0};
    default: return {0, distance};
  }
}

HRGN RoundedWindowRegion(const RECT& bounds, const std::array<int, 4>& radii) {
  const auto rounded = [&](int r) {
    return r > 0 ? CreateRoundRectRgn(0, 0, bounds.right + 1, bounds.bottom + 1, 2 * r, 2 * r)
                 : CreateRectRgnIndirect(&bounds);
  };
  const int radius = *std::max_element(radii.begin(), radii.end());
  HRGN region = rounded(radius);
  if (!region) return nullptr;
  const RECT corners[] = {
      {0, 0, radius, radius},
      {bounds.right - radius, 0, bounds.right, radius},
      {bounds.right - radius, bounds.bottom - radius, bounds.right, bounds.bottom},
      {0, bounds.bottom - radius, radius, bounds.bottom},
  };
  // Replace only the corners whose radius differs from the main capsule.
  for (size_t i = 0; i < radii.size(); ++i) {
    if (radii[i] == radius) continue;
    HRGN corner = CreateRectRgnIndirect(&corners[i]);
    HRGN smaller = rounded(radii[i]);
    const bool ok = corner && smaller &&
                    CombineRgn(smaller, smaller, corner, RGN_AND) != ERROR &&
                    CombineRgn(region, region, smaller, RGN_OR) != ERROR;
    if (corner) DeleteObject(corner);
    if (smaller) DeleteObject(smaller);
    if (!ok) {
      DeleteObject(region);
      return nullptr;
    }
  }
  return region;
}

uint32_t PremultipliedBgra(Windows::UI::Color color) {
  const uint32_t alpha = color.A;
  return (alpha << 24) | ((color.R * alpha / 255) << 16) |
         ((color.G * alpha / 255) << 8) | (color.B * alpha / 255);
}

uint32_t BlendEdgeColors(uint32_t fill, uint32_t track, double coverage) {
  uint32_t result = 0;
  for (int shift : {0, 8, 16, 24}) {
    const auto from = static_cast<double>((track >> shift) & 255);
    const auto to = static_cast<double>((fill >> shift) & 255);
    const auto value = static_cast<uint32_t>(std::lround(from + (to - from) * coverage));
    result |= value << shift;
  }
  return result;
}

// 提亮并补足预乘 BGRA 像素的原色饱和度/不透明度，保持色带连贯不“断层”。
uint32_t EnergizeEdgePixel(uint32_t bgra, double boost) {
  if (boost <= 0.0) return bgra;
  const double weight = std::clamp(boost, 0.0, 1.0);
  const uint32_t alpha = (bgra >> 24) & 255;
  if (alpha == 0) return bgra;
  const double inv = 255.0 / static_cast<double>(alpha);
  const double gain = 1.0 + 0.24 * weight;
  const uint32_t r = std::min<uint32_t>(255, static_cast<uint32_t>(std::lround(((bgra >> 16) & 255) * inv * gain)));
  const uint32_t g = std::min<uint32_t>(255, static_cast<uint32_t>(std::lround(((bgra >> 8) & 255) * inv * gain)));
  const uint32_t b = std::min<uint32_t>(255, static_cast<uint32_t>(std::lround((bgra & 255) * inv * gain)));
  const uint32_t outAlpha = static_cast<uint32_t>(std::lround(alpha + (255.0 - alpha) * weight));
  return (outAlpha << 24) | ((r * outAlpha / 255) << 16) |
         ((g * outAlpha / 255) << 8) | (b * outAlpha / 255);
}

// 预乘 BGRA 按 Porter-Duff OVER 叠加目标不透明色（白光或前沿暗对比边）。
uint32_t CompositeOpaqueOver(uint32_t bgra, uint8_t targetR, uint8_t targetG, uint8_t targetB,
                             double intensity) {
  if (intensity <= 0.0) return bgra;
  const double weight = std::clamp(intensity, 0.0, 1.0);
  const auto mix = [weight](uint32_t from, uint8_t to) {
    return static_cast<uint32_t>(std::lround(from + (static_cast<double>(to) - from) * weight));
  };
  const uint32_t b = mix(bgra & 255, targetB);
  const uint32_t g = mix((bgra >> 8) & 255, targetG);
  const uint32_t r = mix((bgra >> 16) & 255, targetR);
  const uint32_t a = mix((bgra >> 24) & 255, 255);
  return (a << 24) | (r << 16) | (g << 8) | b;
}

void RoundProgressTip(uint32_t* pixels, SIZE size, bool horizontal, bool reverse,
                      int filledPixels, uint32_t fill, uint32_t track) {
  const int length = horizontal ? size.cx : size.cy;
  if (filledPixels <= 0 || filledPixels >= length) return;
  const int thickness = horizontal ? size.cy : size.cx;
  const double radius = std::min(thickness / 2.0, static_cast<double>(filledPixels));
  const double start = filledPixels - radius;
  for (int cross = 0; cross < thickness; ++cross) {
    const double normalized = (cross + 0.5) / (thickness / 2.0) - 1;
    const double tip = start + radius * std::sqrt(1 - normalized * normalized);
    for (int along = static_cast<int>(std::floor(start)); along < filledPixels; ++along) {
      const int position = reverse ? length - 1 - along : along;
      auto& pixel = horizontal ? pixels[cross * size.cx + position]
                               : pixels[position * size.cx + cross];
      pixel = BlendEdgeColors(fill, track, std::clamp(tip - along, 0.0, 1.0));
    }
  }
}

void ApplyStripHighlight(uint32_t* pixels, SIZE size, bool horizontal, bool reverse,
                         bool enabled, int headPixel, int sideStart, int perimeter,
                         int thickness, double phase) {
  if (!enabled || headPixel < 0 || perimeter <= 0 || thickness <= 0) return;
  const int length = horizontal ? size.cx : size.cy;
  const int stripThickness = horizontal ? size.cy : size.cx;
  const auto spec = BuildScreenEdgeHighlightSpec(perimeter, thickness);
  const int from = std::clamp(
      static_cast<int>(std::floor(headPixel - spec.tailLength - sideStart)), 0, length);
  const int to = std::clamp(
      static_cast<int>(std::ceil(headPixel + spec.leadLength - sideStart)), 0, length);
  for (int along = from; along < to; ++along) {
    const double position = sideStart + along + 0.5;
    const auto sample = SampleScreenEdgeHighlight(
        position, static_cast<double>(headPixel), static_cast<double>(perimeter),
        static_cast<double>(thickness), phase);
    if (sample.boost <= 0.0 && sample.core <= 0.0 && sample.lip <= 0.0) continue;
    const double delta = static_cast<double>(headPixel) - position;
    const bool nearTip = delta >= 0.0 && delta <= std::max(2.5, thickness * 0.35);
    const int pos = reverse ? length - 1 - along : along;
    for (int cross = 0; cross < stripThickness; ++cross) {
      auto& pixel = horizontal ? pixels[cross * size.cx + pos]
                               : pixels[pos * size.cx + cross];
      if (sample.lip > 0.0) {
        pixel = CompositeOpaqueOver(pixel, 18, 24, 38, sample.lip);
        continue;
      }
      if (sample.boost > 0.0) {
        pixel = EnergizeEdgePixel(pixel, sample.boost);
      }
      if (sample.core > 0.0) {
        // 横截面呈管状高光：条芯白亮，外缘保留彩色，避免看起来像“断层”。
        const double crossNorm =
            std::fabs((cross + 0.5) / (stripThickness * 0.5) - 1.0);
        const double edgeFalloff = stripThickness <= 5 ? 0.55 : 0.80;
        double crossMask = std::clamp(1.0 - edgeFalloff * crossNorm * crossNorm, 0.20, 1.0);
        if (nearTip) crossMask = std::max(crossMask, 0.72);
        pixel = CompositeOpaqueOver(pixel, 255, 255, 255, sample.core * crossMask);
      }
    }
  }
}

void MaskScreenEdgeBands(uint32_t* pixels, const SIZE& size, const RECT& bounds, const RECT& frame,
                         int thickness, WidgetStyle::ScreenEdgeStyle style) {
  const auto rect = [](const RECT& r) {
    return ScreenEdgeRect{static_cast<double>(r.left), static_cast<double>(r.top),
                          static_cast<double>(r.right - r.left),
                          static_cast<double>(r.bottom - r.top)};
  };
  const auto frameRect = rect(frame);
  const auto stripRect = rect(bounds);
  for (const auto& band : ScreenEdgeBands(style)) {
    if (band.opacity == 1) continue;
    const auto start = std::lround(band.start * thickness);
    const auto end = std::lround(band.end * thickness);
    if (end <= start) continue;
    const auto opacity = static_cast<uint32_t>(std::lround(band.opacity * 255));
    for (const auto& part : ScreenEdgeBandRects(frameRect, start, end)) {
      const auto clip = IntersectScreenEdgeRects(part, stripRect);
      if (clip.width <= 0 || clip.height <= 0) continue;
      const int x = static_cast<int>(clip.x - stripRect.x);
      const int y = static_cast<int>(clip.y - stripRect.y);
      const int width = static_cast<int>(clip.width), height = static_cast<int>(clip.height);
      for (int row = 0; row < height; ++row) {
        auto* begin = pixels + (y + row) * size.cx + x;
        if (opacity == 0) {
          std::fill_n(begin, width, 0U);
          continue;
        }
        for (int i = 0; i < width; ++i) {
          auto& pixel = begin[i];
          const uint32_t rb = ((pixel & 0x00ff00ff) * opacity >> 8) & 0x00ff00ff;
          const uint32_t ag = (((pixel >> 8) & 0x00ff00ff) * opacity) & 0xff00ff00;
          pixel = rb | ag;
        }
      }
    }
  }
}

bool RenderEdgeStrip(HWND hwnd, const RECT& bounds, const RECT& frame, bool horizontal, bool reverse,
                     int filledPixels, uint32_t fill, uint32_t track,
                     const std::vector<uint32_t>& timeline,
                     bool highlightEnabled, int highlightHead, int highlightSideStart,
                     int highlightPerimeter, int highlightThickness, double highlightPhase,
                     WidgetStyle::ScreenEdgeStyle style, int alpha) {
  SIZE size{bounds.right - bounds.left, bounds.bottom - bounds.top};
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = size.cx;
  info.bmiHeader.biHeight = -size.cy;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return false;
  void* bitmapData = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bitmapData, nullptr, 0);
  bool rendered = false;
  if (bitmap) {
    const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    if (previousBitmap && previousBitmap != HGDI_ERROR) {
      auto* pixels = static_cast<uint32_t*>(bitmapData);
      const int width = size.cx;
      const int length = horizontal ? size.cx : size.cy;
      if (timeline.size() == static_cast<size_t>(length)) {
        for (int y = 0; y < size.cy; ++y) {
          if (horizontal) {
            if (reverse) std::reverse_copy(timeline.begin(), timeline.end(), pixels);
            else std::copy(timeline.begin(), timeline.end(), pixels);
          } else {
            const int along = reverse ? size.cy - 1 - y : y;
            std::fill_n(pixels, width, timeline[along]);
          }
          pixels += width;
        }
      } else if (horizontal) {
        const int first = reverse ? width - filledPixels : filledPixels;
        for (int y = 0; y < size.cy; ++y) {
          std::fill_n(pixels, first, reverse ? track : fill);
          std::fill_n(pixels + first, width - first, reverse ? fill : track);
          pixels += width;
        }
      } else {
        const int first = reverse ? size.cy - filledPixels : filledPixels;
        std::fill_n(pixels, width * first, reverse ? track : fill);
        std::fill_n(pixels + width * first, width * (size.cy - first), reverse ? fill : track);
      }
      if (timeline.empty()) {
        RoundProgressTip(static_cast<uint32_t*>(bitmapData), size, horizontal, reverse,
                         filledPixels, fill, track);
      }
      ApplyStripHighlight(static_cast<uint32_t*>(bitmapData), size, horizontal, reverse,
                          highlightEnabled, highlightHead, highlightSideStart,
                          highlightPerimeter, highlightThickness, highlightPhase);
      if (style != WidgetStyle::ScreenEdgeStyle::Solid) {
        MaskScreenEdgeBands(static_cast<uint32_t*>(bitmapData), size, bounds, frame,
                            horizontal ? size.cy : size.cx, style);
      }
      POINT origin{};
      POINT position{bounds.left, bounds.top};
      // SourceConstantAlpha 与逐像素透明度相乘：淡入淡出只需重提交，不必改像素。
      BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(alpha, 0, 255)),
                          AC_SRC_ALPHA};
      rendered = UpdateLayeredWindow(hwnd, nullptr, &position, &size, dc, &origin, 0, &blend,
                                     ULW_ALPHA) != FALSE;
      SelectObject(dc, previousBitmap);
    }
    DeleteObject(bitmap);
  }
  DeleteDC(dc);
  return rendered;
}

TextBlock MakeText(double size, Windows::UI::Color color, bool semibold = false) {
  TextBlock block;
  block.FontSize(size);
  block.Foreground(SolidColorBrush(color));
  if (semibold) block.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  return block;
}

std::wstring FormatElapsed(int seconds) {
  wchar_t time[16];
  swprintf_s(time, L"%02d:%02d", seconds / 60, seconds % 60);
  return time;
}

POINT ClampToWorkArea(POINT point, const RECT& work, int width, int height) {
  point.x = std::clamp(point.x, work.left, std::max(work.left, work.right - width));
  point.y = std::clamp(point.y, work.top, std::max(work.top, work.bottom - height));
  return point;
}

bool IsTaskbarAboveWindow(HWND hwnd) {
  if (!hwnd || !IsWindowVisible(hwnd)) return false;
  for (HWND cur = GetWindow(hwnd, GW_HWNDPREV); cur != nullptr;
       cur = GetWindow(cur, GW_HWNDPREV)) {
    if (!IsWindowVisible(cur)) continue;
    wchar_t className[64]{};
    if (GetClassNameW(cur, className, 64) <= 0) continue;
    if (wcscmp(className, L"Shell_TrayWnd") == 0 ||
        wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

OverlayWindow* OverlayWindow::activeInstance_ = nullptr;

OverlayWindow::OverlayWindow() { CreateNativeWindow(); }

OverlayWindow::~OverlayWindow() {
  try {
    Close();
  } catch (...) {
  }
}

void OverlayWindow::CreateNativeWindow() {
  window_ = Window();
  window_.Title(L"ClassTime Overlay");

  // 先配置无边框 presenter，再挂内容并激活，
  // 避免 SetBorderAndTitleBar 重建 HWND 时把已挂载的 XAML 岛拆掉。
  auto appWindow = window_.AppWindow();
  if (auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.SetBorderAndTitleBar(false, false);
    presenter.IsResizable(false);
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);
  }

  root_ = Grid();
  root_.UseLayoutRounding(true);
  root_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
  window_.Content(root_);

  BuildBar();
  BuildRing();
  BuildDetailCard();
  const auto palette = ResolvePalette(Services::Instance().config().presentation);
  edgeHandle_ = Border();
  edgeHandle_.Background(WidgetAppearance::Surface(palette.background));
  edgeHandle_.CornerRadius({6, 6, 6, 6});
  edgeHandle_.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 28)));
  edgeHandle_.BorderThickness(kSurfaceBorder);
  edgeTrack_ = Border();
  const double trackRadius = kHandleTrackThicknessDip / 2;
  edgeTrack_.CornerRadius({trackRadius, trackRadius, trackRadius, trackRadius});
  edgeTrack_.HorizontalAlignment(HorizontalAlignment::Center);
  edgeTrack_.VerticalAlignment(VerticalAlignment::Center);
  edgeTrack_.Background(SolidColorBrush(WithAlpha(palette.foreground, 55)));
  edgeFill_ = Shapes::Rectangle();
  edgeFillBrush_ = SolidColorBrush(palette.accent);
  edgeFill_.Fill(edgeFillBrush_);
  edgeFill_.HorizontalAlignment(HorizontalAlignment::Left);
  edgeFill_.VerticalAlignment(VerticalAlignment::Top);
  edgeTrack_.Child(edgeFill_);
  edgeHandle_.Child(edgeTrack_);
  edgeHandle_.Visibility(Visibility::Collapsed);
  root_.Children().Append(edgeHandle_);
  BindDrag();

  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  if (hwnd_ != nullptr) {
    // The presenter can leave a dialog frame around the XAML content.
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE,
                      (exStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)) |
                          WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  }
  window_.SizeChanged([this](auto const&, auto const&) { UpdateWindowShape(); });

  activeInstance_ = this;
  foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                    &OverlayWindow::OnForegroundChanged, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  ApplyPresentation();
}

void OverlayWindow::BuildBar() {
  const Palette palette = ResolvePalette(Services::Instance().config().presentation);

  barRoot_ = Border();
  barRoot_.CornerRadius(WidgetAppearance::DockCorners(16, edge_, flushWithEdge_));
  barRoot_.Background(WidgetAppearance::Surface(palette.background));
  barRoot_.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 26)));
  barRoot_.BorderThickness(kSurfaceBorder);
  barRoot_.Padding(Thickness{kBarInsetDip, 0, kBarInsetDip, 0});

  segmentPanel_ = StackPanel();
  segmentPanel_.Orientation(Orientation::Horizontal);
  segmentPanel_.VerticalAlignment(VerticalAlignment::Center);
  barRoot_.Child(segmentPanel_);
  root_.Children().Append(barRoot_);
  ApplyBarSurface(palette);
}

void OverlayWindow::ApplyBarSurface(const Palette& palette) {
  const auto surface = WidgetAppearance::SurfaceColor(palette, style_.colors.barBackground);
  const auto ink = WidgetAppearance::SurfaceInk(palette, surface, style_.colors.inkColor);
  if (barSurface_ != surface) {
    barSurface_ = surface;
    barRoot_.Background(WidgetAppearance::Surface(surface));
  }
  if (barInk_ != ink || !barPalette_ || barPalette_->highContrast != palette.highContrast) {
    barInk_ = ink;
    barRoot_.BorderBrush(SolidColorBrush(WithAlpha(ink, palette.highContrast ? 255 : 50)));
  }
}

void OverlayWindow::BuildRing() {
  ringRoot_ = Grid();
  ringRoot_.Children().Append(ringFace_.Element());
  ringRoot_.PointerMoved([this](auto const&, auto const&) {
    if (!ringDetailId_.empty()) ShowDetail(ringRoot_, ringDetailId_);
  });
  root_.Children().Append(ringRoot_);
  ringRoot_.Visibility(Visibility::Collapsed);
}

void OverlayWindow::BuildDetailCard() {
  const auto palette = ResolvePalette(Services::Instance().config().presentation);
  const auto secondary = WithAlpha(palette.foreground, 175);

  detailCard_ = Border();
  detailCard_.Width(kDetailWidth);
  detailCard_.Padding(Thickness{14, 12, 14, 12});
  detailCard_.Background(WidgetAppearance::Surface(palette.background));
  detailCard_.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 30)));
  detailCard_.BorderThickness(Thickness{1, 1, 1, 1});
  detailCard_.CornerRadius(CornerRadius{19, 19, 19, 19});

  StackPanel content;
  content.Spacing(7);

  StackPanel heading;
  heading.Orientation(Orientation::Horizontal);
  heading.Spacing(8);
  detailMarker_ = Border();
  detailMarker_.Width(4);
  detailMarker_.Height(14);
  detailMarker_.CornerRadius(CornerRadius{2, 2, 2, 2});
  detailMarker_.VerticalAlignment(VerticalAlignment::Center);
  heading.Children().Append(detailMarker_);
  detailTitle_ = MakeText(15, palette.foreground, true);
  heading.Children().Append(detailTitle_);
  content.Children().Append(heading);

  detailRange_ = MakeText(12, secondary);
  detailRange_.TextTrimming(TextTrimming::CharacterEllipsis);
  content.Children().Append(detailRange_);

  Grid meter;
  meter.ColumnSpacing(8);
  ColumnDefinition trackColumn;
  trackColumn.Width(GridLengthHelper::FromValueAndType(kDetailTrackWidth, GridUnitType::Pixel));
  meter.ColumnDefinitions().Append(trackColumn);
  ColumnDefinition percentColumn;
  percentColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  meter.ColumnDefinitions().Append(percentColumn);

  Border track;
  track.Height(7);
  track.VerticalAlignment(VerticalAlignment::Center);
  track.CornerRadius(CornerRadius{3.5, 3.5, 3.5, 3.5});
  track.Background(SolidColorBrush(WithAlpha(palette.foreground, 30)));
  detailFill_ = Border();
  detailFill_.HorizontalAlignment(HorizontalAlignment::Left);
  detailFill_.CornerRadius(track.CornerRadius());
  track.Child(detailFill_);
  meter.Children().Append(track);

  detailPercent_ = MakeText(13, palette.foreground, true);
  detailPercent_.HorizontalAlignment(HorizontalAlignment::Right);
  Grid::SetColumn(detailPercent_, 1);
  meter.Children().Append(detailPercent_);
  content.Children().Append(meter);

  detailTiming_ = MakeText(12, secondary);
  content.Children().Append(detailTiming_);
  detailNext_ = MakeText(12, secondary);
  detailNext_.TextTrimming(TextTrimming::CharacterEllipsis);
  content.Children().Append(detailNext_);
  detailMuted_ = MakeText(12, secondary);
  detailMuted_.Text(L"提醒已暂停");
  content.Children().Append(detailMuted_);
  detailCard_.Child(content);

  detailPopup_ = Primitives::Popup();
  detailPopup_.ShouldConstrainToRootBounds(false);
  detailPopup_.IsHitTestVisible(false);
  detailPopup_.Child(detailCard_);
  root_.Children().Append(detailPopup_);
}

void OverlayWindow::ShowDetail(const FrameworkElement& anchor, const std::string& segmentId) {
  if (dragPointer_ || snapFrame_.value || !visible_ || edgeClosing_) return;
  if (detailAnchor_ == anchor && detailSegmentId_ == segmentId) return;

  const bool reopening = detailPopup_.IsOpen() && !detailAnchor_;
  detailAnchor_ = anchor;
  detailSegmentId_ = segmentId;
  RefreshDetail();
  if (!detailAnchor_) return;
  // 在同一条悬浮条上扫过相邻课块时，只更新内容，保持弹卡位置。
  if (detailPopup_.IsOpen()) {
    if (reopening) AnimateDetail(true);
    return;
  }

  const auto anchorCenter = anchor.TransformToVisual(root_).TransformPoint(
      {static_cast<float>(anchor.ActualWidth() / 2), static_cast<float>(anchor.ActualHeight() / 2)});
  POINT origin{};
  RECT windowRect{};
  MONITORINFO info{sizeof(info)};
  if (!ClientToScreen(hwnd_, &origin) || !GetWindowRect(hwnd_, &windowRect) ||
      !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
    HideDetail();
    return;
  }

  const double scale = root_.XamlRoot().RasterizationScale();
  detailCard_.Measure({static_cast<float>(kDetailWidth), 1000});
  const int cardWidth = static_cast<int>(std::ceil(kDetailWidth * scale));
  const int cardHeight = static_cast<int>(std::ceil(detailCard_.DesiredSize().Height * scale));
  const int gap = static_cast<int>(std::lround(8 * scale));
  const int anchorX = origin.x + std::lround(anchorCenter.X * scale);
  const int anchorY = origin.y + std::lround(anchorCenter.Y * scale);
  POINT preferred{};
  double originX = 0.5;
  double originY = 0.5;
  switch (edge_) {
    case classtime::PresentationSettings::Edge::Top:
      preferred = {anchorX - cardWidth / 2, windowRect.top - gap - cardHeight};
      originY = 1.0;
      break;
    case classtime::PresentationSettings::Edge::Left:
      preferred = {windowRect.left - gap - cardWidth, anchorY - cardHeight / 2};
      originX = 1.0;
      break;
    case classtime::PresentationSettings::Edge::Right:
      preferred = {windowRect.right + gap, anchorY - cardHeight / 2};
      originX = 0.0;
      break;
    default:
      preferred = {anchorX - cardWidth / 2, windowRect.bottom + gap};
      originY = 0.0;
      break;
  }
  const POINT position = ClampToWorkArea(preferred, info.rcWork, cardWidth, cardHeight);
  detailPopup_.HorizontalOffset((position.x - origin.x) / scale);
  detailPopup_.VerticalOffset((position.y - origin.y) / scale);
  if (!Motion::Enabled()) {
    detailCard_.Opacity(1);
    detailCard_.RenderTransform(nullptr);
    detailPopup_.IsOpen(true);
    return;
  }
  detailCard_.RenderTransformOrigin({static_cast<float>(originX), static_cast<float>(originY)});
  ScaleTransform scaleTransform;
  constexpr double kInitialScale = 0.975;
  scaleTransform.ScaleX(kInitialScale);
  scaleTransform.ScaleY(kInitialScale);
  detailCard_.RenderTransform(scaleTransform);
  detailCard_.Opacity(0);
  detailPopup_.IsOpen(true);
  AnimateDetail(true);
}

void OverlayWindow::RefreshDetail() {
  if (!detailAnchor_) return;
  const auto& services = Services::Instance();
  const auto& segments = services.effectiveSegments();
  const auto selected = std::find_if(segments.begin(), segments.end(), [this](const auto& segment) {
    return segment.id == detailSegmentId_;
  });
  if (selected == segments.end()) {
    HideDetail();
    return;
  }

  const auto now = NowClock();
  const int duration = selected->DurationMinutes() * 60;
  const int elapsed = std::clamp((now.MinutesSinceMidnight() - selected->start.TotalMinutes()) * 60 +
                                     now.second, 0, duration);
  const double progress = static_cast<double>(elapsed) / duration;
  const bool focus = selected->kind == classtime::SegmentKind::Focus;
  std::wstring title = SegmentKindLabel(selected->kind);
  if (focus) {
    const auto ordinal = std::count_if(segments.begin(), selected + 1, [](const auto& segment) {
      return segment.kind == classtime::SegmentKind::Focus;
    });
    title = L"第 " + std::to_wstring(ordinal) + L" 节";
  }
  title += elapsed == duration ? L" · 已结束"
           : now.MinutesSinceMidnight() >= selected->start.TotalMinutes() ? L" · 进行中"
                                                                         : L" · 尚未开始";
  detailTitle_.Text(title);
  const auto label = ToWide(selected->label);
  const auto range = FormatRange(*selected);
  const bool genericLabel = label == SegmentKindLabel(selected->kind) ||
                            title.compare(0, label.size(), label) == 0;
  detailRange_.Text(genericLabel ? range : label + L"  " + range);

  const auto accent = KindColor(
      ResolvePalette(services.config().presentation), selected->kind);
  detailMarker_.Background(SolidColorBrush(accent));
  detailFill_.Background(SolidColorBrush(accent));
  detailFill_.Width(kDetailTrackWidth * progress);
  detailPercent_.Text(FormatPercent(progress));
  detailTiming_.Text(L"已用 " + FormatElapsed(elapsed) + L" / " + FormatElapsed(duration) +
                     L"    剩余 " + FormatElapsed(duration - elapsed));

  const auto next = selected + 1;
  detailNext_.Text(next == segments.end() ? L"这是今天最后一段"
                                         : L"下一段  " + FormatClock(next->start) + L"  " +
                                               ToWide(next->label));
  detailMuted_.Visibility(services.config().notifications.muted ? Visibility::Visible
                                                                  : Visibility::Collapsed);
}

void OverlayWindow::HideDetail(bool animate) {
  const bool wasAnchored = detailAnchor_ != nullptr;
  detailAnchor_ = nullptr;
  detailSegmentId_.clear();
  if (animate && detailPopup_.IsOpen() && Motion::Enabled()) {
    if (wasAnchored) AnimateDetail(false);
    return;
  }
  if (detailPopup_) detailPopup_.IsOpen(false);
  Motion::Stop(detailMotion_);
}

void OverlayWindow::AnimateDetail(bool show) {
  using namespace Media::Animation;
  const double opacity = detailCard_.Opacity();
  auto scale = detailCard_.RenderTransform().try_as<ScaleTransform>();
  const double fromScale = scale ? scale.ScaleX() : 1.0;
  Motion::Stop(detailMotion_);
  if (!Motion::Enabled()) {
    detailCard_.Opacity(1);
    detailCard_.RenderTransform(nullptr);
    if (!show) detailPopup_.IsOpen(false);
    return;
  }

  scale = ScaleTransform();
  scale.ScaleX(fromScale);
  scale.ScaleY(fromScale);
  detailCard_.RenderTransform(scale);
  detailCard_.Opacity(opacity);
  detailMotion_ = Storyboard();
  const int duration = show ? 260 : 210;
  SineEase fade;
  fade.EasingMode(EasingMode::EaseInOut);
  Motion::Animate(detailMotion_, detailCard_, L"Opacity", opacity, show ? 1 : 0, duration, fade);
  const double toScale = show ? 1 : 0.975;
  BackEase movement;
  movement.EasingMode(show ? EasingMode::EaseOut : EasingMode::EaseIn);
  movement.Amplitude(show ? 0.25 : 0);
  Motion::Animate(detailMotion_, scale, L"ScaleX", fromScale, toScale, duration, movement);
  Motion::Animate(detailMotion_, scale, L"ScaleY", fromScale, toScale, duration, movement);
  if (!show) {
    detailMotion_.Completed([this](auto const& sender, auto const&) {
      if (sender == detailMotion_) HideDetail();
    });
  }
  detailMotion_.Begin();
}

void OverlayWindow::BindDrag() {
  using Input::PointerRoutedEventArgs;
  root_.PointerEntered([this](auto const&, auto const&) {
    if (edgeTimer_) edgeTimer_.Stop();
    if ((edgeCollapsed_ || edgeClosing_) && visible_) SetEdgeCollapsed(false, true);
  });
  root_.PointerExited([this](auto const&, auto const&) {
    HideDetail(true);
    QueueEdgeCollapse();
  });
  root_.PointerPressed([this](auto const&, const PointerRoutedEventArgs& args) {
    if (edgeCollapsed_) {
      SetEdgeCollapsed(false, true);
      args.Handled(true);
      return;
    }
    if (edgeClosing_) SetEdgeCollapsed(false, true);
    if (dragPointer_ ||
        args.Pointer().PointerDeviceType() != Microsoft::UI::Input::PointerDeviceType::Mouse ||
        !args.GetCurrentPoint(root_).Properties().IsLeftButtonPressed()) return;

    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const ULONGLONG tick = GetTickCount64();
    const bool withinSequence =
        lastPressTick_ != 0 && tick - lastPressTick_ <= GetDoubleClickTime() &&
        std::abs(cursor.x - lastPressPoint_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
        std::abs(cursor.y - lastPressPoint_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
    pressCount_ = withinSequence ? std::min(pressCount_ + 1, 3) : 1;
    lastPressTick_ = tick;
    lastPressPoint_ = cursor;
    if (pressCount_ >= 3) {
      // 连续三击悬浮条/圆环：沿当前边回到居中。
      pressCount_ = 0;
      lastPressTick_ = 0;
      CenterOverlay();
      args.Handled(true);
      return;
    }

    HideDetail();
    SettleMotion();
    StopSnap();
    RECT bounds{};
    if (!GetWindowRect(hwnd_, &bounds) || !root_.CapturePointer(args.Pointer())) return;
    dragStart_ = cursor;
    dragOrigin_ = {bounds.left, bounds.top};
    dragPointer_ = args.Pointer().PointerId();
    args.Handled(true);
  });
  root_.PointerMoved([this](auto const&, const PointerRoutedEventArgs& args) {
    if ((edgeCollapsed_ || edgeClosing_) && visible_) {
      SetEdgeCollapsed(false, true);
      return;
    }
    if (!dragPointer_ || args.Pointer().PointerId() != *dragPointer_) return;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const long dx = cursor.x - dragStart_.x;
    const long dy = cursor.y - dragStart_.y;
    if (!dragging_ && std::max(std::abs(dx), std::abs(dy)) < 4 * dpiScale_) return;
    dragging_ = true;
    pressCount_ = 0;
    lastPressTick_ = 0;
    SetWindowPos(hwnd_, nullptr, dragOrigin_.x + dx, dragOrigin_.y + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    args.Handled(true);
  });
  root_.PointerReleased([this](auto const&, const PointerRoutedEventArgs& args) {
    if (!dragPointer_ || args.Pointer().PointerId() != *dragPointer_) return;
    FinishDrag(true);
    args.Handled(true);
  });
  const auto cancelDrag = [this](auto const&, const PointerRoutedEventArgs& args) {
    if (dragPointer_ && args.Pointer().PointerId() == *dragPointer_) FinishDrag(false);
  };
  root_.PointerCaptureLost(cancelDrag);
  root_.PointerCanceled(cancelDrag);
}

void OverlayWindow::CenterOverlay() {
  if (hwnd_ == nullptr || view_ == View::ScreenEdge) return;
  SettleMotion();
  auto& services = Services::Instance();
  auto presentation = services.config().presentation;
  if (presentation.offsetX == 0 && presentation.offsetY == 0) return;
  presentation.offsetX = 0;
  presentation.offsetY = 0;
  if (!services.UpdatePresentation(presentation)) {
    QueueEdgeCollapse();
    return;
  }
  offsetX_ = 0;
  offsetY_ = 0;

  RECT bounds{};
  MONITORINFO info{sizeof(info)};
  if (!GetWindowRect(hwnd_, &bounds) ||
      !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
    LayoutWindow();
    return;
  }
  const RECT work = TargetBounds(info);
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  POINT target{bounds.left, bounds.top};
  if (IsSideEdge(edge_)) {
    target.y = work.top + (work.bottom - work.top - height) / 2;
  } else {
    target.x = work.left + (work.right - work.left - width) / 2;
  }
  SnapTo(ClampToWorkArea(target, work, width, height), work);
}

void OverlayWindow::FinishDrag(bool savePosition) {
  dragPointer_.reset();
  root_.ReleasePointerCaptures();
  const bool moved = std::exchange(dragging_, false);
  if (!moved) {
    QueueEdgeCollapse();
    return;
  }
  if (!savePosition) {
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }

  RECT bounds{};
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  const HMONITOR currentMonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
  if (!GetWindowRect(hwnd_, &bounds) || !GetMonitorInfoW(currentMonitor, &info)) {
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }
  const HMONITOR previousMonitor = DisplayUtils::FindMonitor(monitorId_);
  const bool movedMonitor = currentMonitor != previousMonitor;
  dpiScale_ = root_.XamlRoot().RasterizationScale();
  const RECT work = TargetBounds(info);
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  const int inset = (autoHideAtEdge_ || flushWithEdge_)
                        ? 0
                        : static_cast<int>(std::lround(kSnapInsetDip * dpiScale_));
  const int workWidth = work.right - work.left;
  const int workHeight = work.bottom - work.top;
  const int workCenterX = work.left + workWidth / 2;
  const int workCenterY = work.top + workHeight / 2;
  const int oldCenterX = bounds.left + width / 2;
  const int oldCenterY = bounds.top + height / 2;

  POINT cursor{oldCenterX, oldCenterY};
  GetCursorPos(&cursor);
  const auto edgeScore = [](long boundsDistance, long cursorDistance) {
    return std::pair<long, long>{std::max(0L, boundsDistance), std::max(0L, cursorDistance)};
  };

  // 取最近的一条边；拖出屏幕边界时按指针所在边缘平局决胜，避免长条滑过边角误判。
  auto targetEdge = classtime::PresentationSettings::Edge::Bottom;
  auto nearest = edgeScore(work.bottom - bounds.bottom, work.bottom - cursor.y);
  if (const auto score = edgeScore(bounds.top - work.top, cursor.y - work.top); score < nearest) {
    nearest = score;
    targetEdge = classtime::PresentationSettings::Edge::Top;
  }
  if (const auto score = edgeScore(bounds.left - work.left, cursor.x - work.left); score < nearest) {
    nearest = score;
    targetEdge = classtime::PresentationSettings::Edge::Left;
  }
  if (const auto score = edgeScore(work.right - bounds.right, work.right - cursor.x);
      score < nearest) {
    targetEdge = classtime::PresentationSettings::Edge::Right;
  }

  const bool wasSide = IsSideEdge(edge_);
  const bool nowSide = IsSideEdge(targetEdge);
  const bool edgeChanged = targetEdge != edge_;
  const Layer outgoing = CurrentLayer();

  auto& services = Services::Instance();
  auto presentation = services.config().presentation;
  presentation.edge = targetEdge;
  presentation.monitorId = ToUtf8(info.szDevice);
  // 横向贴边用 offsetX 记水平位置，纵向贴边用 offsetY 记垂直位置；
  // 另一轴归零，切回原来的方向时仍然居中。
  if (nowSide) {
    presentation.offsetX = 0;
    presentation.offsetY =
        static_cast<int>(std::lround((oldCenterY - workCenterY) / dpiScale_));
  } else {
    presentation.offsetY = 0;
    presentation.offsetX =
        static_cast<int>(std::lround((oldCenterX - workCenterX) / dpiScale_));
  }
  if (!services.UpdatePresentation(presentation)) {
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }
  edge_ = presentation.edge;
  monitorId_ = presentation.monitorId;
  offsetX_ = presentation.offsetX;
  offsetY_ = presentation.offsetY;

  // 横排改竖排（或跨屏/缩放变化）时同步更新样式、尺寸、圆角区域与分节排列。
  if (wasSide != nowSide || movedMonitor) {
    const RECT target = ComputeLayoutRect();
    if (!movedMonitor && Motion::Enabled() && IsWindowVisible(hwnd_) && !IsRectEmpty(&target)) {
      AnimateWindowRect(target, outgoing);
      return;
    }
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }

  // 同方向换边（底↔顶 / 左↔右）时贴边圆角方向与 Win32 裁剪区域也必须立即同步。
  if (edgeChanged) {
    ApplyLayoutStyle();
    UpdateWindowShape();
  }

  POINT target{};
  if (nowSide) {
    target.x = targetEdge == classtime::PresentationSettings::Edge::Left
                   ? work.left + inset
                   : work.right - width - inset;
    target.y = workCenterY + std::lround(offsetY_ * dpiScale_) - height / 2;
  } else {
    target.y = targetEdge == classtime::PresentationSettings::Edge::Top
                   ? work.top + inset
                   : work.bottom - height - inset;
    target.x = workCenterX + std::lround(offsetX_ * dpiScale_) - width / 2;
  }
  SnapTo(ClampToWorkArea(target, work, width, height), work);
}

void OverlayWindow::SnapTo(POINT target, RECT workArea) {
  RECT bounds{};
  if (!GetWindowRect(hwnd_, &bounds)) return;
  const auto move = [this](POINT point) {
    SetWindowPos(hwnd_, coverTaskbar_ ? HWND_TOPMOST : nullptr, point.x, point.y, 0, 0,
                 SWP_NOSIZE | (coverTaskbar_ ? 0 : SWP_NOZORDER) | SWP_NOACTIVATE);
  };
  StopWindowMotion();
  StopSnap();
  if (!Motion::Enabled() || (target.x == bounds.left && target.y == bounds.top)) {
    move(target);
    QueueEdgeCollapse();
    return;
  }

  // 吸附期间跟随 XAML 合成帧，结束后立即取消订阅。
  const auto started = std::chrono::steady_clock::now();
  const bool horizontal = IsSideEdge(edge_);
  const double distance = horizontal ? target.x - bounds.left : target.y - bounds.top;
  const double overshoot = std::min(8.0 * dpiScale_, 0.035 * std::abs(distance));
  const double direction = distance >= 0 ? 1.0 : -1.0;
  lastSnapPosition_ = {bounds.left, bounds.top};
  snapFrame_ = CompositionTarget::Rendering(
      [this, move, target, bounds, workArea, started, overshoot, direction,
       horizontal](auto const&, auto const&) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
                         .count() / 0.40;
    if (t >= 1.0) {
      StopSnap();
      move(target);
      QueueEdgeCollapse();
      return;
    }
    const double reach = std::min(t / 0.80, 1.0);
    const double travel = reach * reach * (3 - 2 * reach);
    const double rebound = std::max((t - 0.68) / 0.32, 0.0);
    const double wave = std::sin(3.141592653589793 * rebound);
    const double recoil = overshoot * wave * wave * (1 - rebound);
    double x = bounds.left + (target.x - bounds.left) * travel;
    double y = bounds.top + (target.y - bounds.top) * travel;
    if (horizontal) {
      x += direction * recoil;
    } else {
      y += direction * recoil;
    }
    const POINT next = ClampToWorkArea({std::lround(x), std::lround(y)}, workArea,
                                      bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (next.x != lastSnapPosition_.x || next.y != lastSnapPosition_.y) {
      move(next);
      lastSnapPosition_ = next;
    }
  });
}

void OverlayWindow::StopSnap() {
  if (auto token = std::exchange(snapFrame_, {}); token.value) {
    CompositionTarget::Rendering(token);
  }
}

void OverlayWindow::StopWindowMotion() {
  if (auto token = std::exchange(windowMotionFrame_, {}); token.value) {
    CompositionTarget::Rendering(token);
  }
}

void OverlayWindow::ApplyWindowRect(const RECT& rect) {
  if (hwnd_ == nullptr) return;
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);
  UpdateWindowShape();
}

void OverlayWindow::AnimateWindowRect(const RECT& target, Layer outgoing) {
  RECT from{};
  if (!GetWindowRect(hwnd_, &from)) {
    FinishMorph();
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }
  const int oldWidth = from.right - from.left;
  const int oldHeight = from.bottom - from.top;
  const int width = target.right - target.left;
  const int height = target.bottom - target.top;
  const bool changingShape = outgoing != Layer::None || oldWidth != width || oldHeight != height;
  const double dx = (target.left + width / 2.0) - (from.left + oldWidth / 2.0);
  const double dy = (target.top + height / 2.0) - (from.top + oldHeight / 2.0);
  const double distance = std::max(std::abs(dx), std::abs(dy));
  const auto duration = std::chrono::milliseconds(
      static_cast<int>(std::clamp(330.0 + distance / 4, 330.0, 540.0)));
  const double recoil = std::min(0.012, 8 * dpiScale_ / std::max(1.0, distance));
  const double outgoingOpacity = root_.Opacity();
  MONITORINFO monitor{sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
    FinishMorph();
    LayoutWindow();
    ApplyState();
    QueueEdgeCollapse();
    return;
  }
  morphOutgoing_ = outgoing;
  UpdateVisibleLayer();
  root_.IsHitTestVisible(false);

  // Keep the old HWND size while it leaves. Swap contents and native shape at
  // the same moving center, then let the new silhouette finish the glide.
  windowMotionFrame_ = CompositionTarget::Rendering(
      [this, from, target, monitor, oldWidth, oldHeight, width, height, dx, dy,
       duration, changingShape, outgoingOpacity, recoil, swapped = false,
       started = std::chrono::steady_clock::time_point{}, last = from](
          auto const&, auto const&) mutable {
    const auto now = std::chrono::steady_clock::now();
    if (started.time_since_epoch().count() == 0) started = now;
    const double t = std::chrono::duration<double>(now - started) /
                     std::chrono::duration<double>(duration);
    if (t >= 1.0) {
      FinishMorph();
      ApplyLayoutStyle();
      ApplyWindowRect(target);
      ApplyState();
      QueueEdgeCollapse();
      return;
    }
    const bool swapNow = changingShape && !swapped && t >= 0.43;
    const bool useNewSize = !changingShape || swapped || swapNow;
    const double travel = Motion::Glide(t, recoil);
    const int frameWidth = useNewSize ? width : oldWidth;
    const int frameHeight = useNewSize ? height : oldHeight;
    const POINT position = ClampToWorkArea(
        {std::lround(from.left + oldWidth / 2.0 + dx * travel - frameWidth / 2.0),
         std::lround(from.top + oldHeight / 2.0 + dy * travel - frameHeight / 2.0)},
        TargetBounds(monitor), frameWidth, frameHeight);
    const RECT rect{position.x, position.y, position.x + frameWidth, position.y + frameHeight};
    if (swapNow) {
      swapped = true;
      root_.Opacity(0);
      morphOutgoing_ = Layer::None;
      UpdateVisibleLayer();
      ApplyLayoutStyle();
      ApplyWindowRect(rect);
      ApplyState();
    } else if (!EqualRect(&rect, &last)) {
      SetWindowPos(hwnd_, nullptr, rect.left, rect.top, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    last = rect;
    if (changingShape) {
      root_.Opacity(swapped ? Motion::SmoothStep((t - 0.43) / 0.38)
                            : outgoingOpacity * (1.0 - Motion::SmoothStep((t - 0.15) / 0.28)));
    } else if (outgoingOpacity < 1.0) {
      root_.Opacity(Motion::Mix(outgoingOpacity, 1.0, Motion::SmoothStep(t * 2)));
    }
  });
}

void OverlayWindow::AnimateScreenEdge(bool entering) {
  StopWindowMotion();
  double widgetFrom = IsWindowVisible(hwnd_) ? root_.Opacity() : 0.0;
  if (entering) {
    if (screenEdgeStrips_.front().hwnd == nullptr) stripAlpha_ = 0;
    RefreshScreenEdge();
  } else {
    const auto outgoing = CurrentLayer();
    morphOutgoing_ = Layer::None;
    edgeCollapsed_ = autoHideAtEdge_;
    if (outgoing != CurrentLayer()) widgetFrom = 0;
    UpdateVisibleLayer();
    LayoutWindow();
    root_.Opacity(widgetFrom);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ApplyState();
  }
  root_.IsHitTestVisible(false);
  const int edgeFrom = stripAlpha_;
  windowMotionFrame_ = CompositionTarget::Rendering(
      [this, entering, widgetFrom, edgeFrom, started = std::chrono::steady_clock::time_point{}](
          auto const&, auto const&) mutable {
    const auto now = std::chrono::steady_clock::now();
    if (started.time_since_epoch().count() == 0) started = now;
    const double t = std::chrono::duration<double>(now - started).count() / 0.34;
    const double fade = Motion::SmoothStep(t);
    root_.Opacity(Motion::Mix(widgetFrom, entering ? 0.0 : 1.0, fade));
    stripAlpha_ = static_cast<int>(std::lround(Motion::Mix(edgeFrom, entering ? 255 : 0, fade)));
    RenderStrips(stripAlpha_);
    if (t < 1) return;
    if (entering) {
      ShowWindow(hwnd_, SW_HIDE);
    } else {
      HideScreenEdge();
    }
    FinishMorph();
    if (!entering) QueueEdgeCollapse();
  });
}

void OverlayWindow::QueueEdgeCollapse() {
  if (!visible_ || view_ == View::ScreenEdge || !autoHideAtEdge_ || edgeCollapsed_ || edgeClosing_ ||
      dragPointer_ || snapFrame_.value || windowMotionFrame_.value) return;
  if (!edgeTimer_) {
    edgeTimer_ = window_.DispatcherQueue().CreateTimer();
    edgeTimer_.IsRepeating(false);
    edgeTimer_.Interval(std::chrono::milliseconds(430));
    edgeTimer_.Tick([this](auto const&, auto const&) {
      RECT bounds{};
      POINT cursor{};
      if (visible_ && autoHideAtEdge_ && !dragPointer_ && !snapFrame_.value &&
          !windowMotionFrame_.value &&
          GetWindowRect(hwnd_, &bounds) &&
          GetCursorPos(&cursor) && !PtInRect(&bounds, cursor)) {
        SetEdgeCollapsed(true, true);
      }
    });
  }
  edgeTimer_.Stop();
  edgeTimer_.Start();
}

void OverlayWindow::SetEdgeCollapsed(bool collapsed, bool animate) {
  if (edgeTimer_) edgeTimer_.Stop();
  SettleMotion();
  const bool useMotion = animate && Motion::Enabled();
  if (!collapsed && edgeClosing_) {
    const auto from = Motion::ReadPose(root_, EdgeContent());
    ResetEdgeMotion();
    if (useMotion) {
      edgeMotion_ = Motion::Slide(root_, EdgeContent(), from, {}, 210);
      edgeMotion_.Begin();
    }
    return;
  }
  if (edgeCollapsed_ == collapsed || (edgeClosing_ && useMotion)) return;
  if (collapsed && useMotion) {
    const auto from = Motion::ReadPose(root_, EdgeContent());
    Motion::Stop(edgeMotion_);
    HideDetail(true);
    edgeClosing_ = true;
    const POINT shift = EdgeShift(edge_, 6);
    edgeMotion_ = Motion::Slide(root_, EdgeContent(), from,
                               {0, static_cast<double>(shift.y), static_cast<double>(shift.x)},
                               210);
    edgeMotion_.Completed([this](auto const& sender, auto const&) {
      if (sender != edgeMotion_) return;
      POINT cursor{};
      if (GetCursorPos(&cursor) && GetAncestor(WindowFromPoint(cursor), GA_ROOT) == hwnd_) {
        SetEdgeCollapsed(false, true);
        return;
      }
      ApplyEdgeCollapsed(true, true);
    });
    edgeMotion_.Begin();
    return;
  }
  ApplyEdgeCollapsed(collapsed, useMotion);
}

void OverlayWindow::ApplyEdgeCollapsed(bool collapsed, bool fade) {
  ResetEdgeMotion();
  edgeCollapsed_ = collapsed;
  HideDetail();
  if (fade) {
    const POINT shift = EdgeShift(edge_, collapsed ? 0 : 5);
    edgeMotion_ = Motion::Slide(root_, EdgeContent(), {0, static_cast<double>(shift.y),
                                                       static_cast<double>(shift.x)},
                               {}, collapsed ? 155 : 240);
  }
  UpdateVisibleLayer();
  LayoutWindow();
  ApplyState();
  if (edgeMotion_) edgeMotion_.Begin();
}

void OverlayWindow::ResetEdgeMotion() {
  edgeClosing_ = false;
  Motion::Stop(edgeMotion_);
  root_.Opacity(1);
  barRoot_.RenderTransform(nullptr);
  ringRoot_.RenderTransform(nullptr);
  edgeHandle_.RenderTransform(nullptr);
}

FrameworkElement OverlayWindow::EdgeContent() const {
  if (edgeCollapsed_) return edgeHandle_;
  if (view_ == View::Ring) return ringRoot_;
  return barRoot_;
}

OverlayWindow::Layer OverlayWindow::CurrentLayer() const {
  if (morphOutgoing_ != Layer::None) return morphOutgoing_;
  if (view_ == View::ScreenEdge) return Layer::None;
  if (edgeCollapsed_) return Layer::Handle;
  return view_ == View::Ring ? Layer::Ring : Layer::Bar;
}

void OverlayWindow::UpdateVisibleLayer() {
  const Layer layer = CurrentLayer();
  barRoot_.Visibility(layer == Layer::Bar ? Visibility::Visible : Visibility::Collapsed);
  ringRoot_.Visibility(layer == Layer::Ring ? Visibility::Visible : Visibility::Collapsed);
  edgeHandle_.Visibility(layer == Layer::Handle ? Visibility::Visible : Visibility::Collapsed);
}

void OverlayWindow::FinishMorph() {
  StopWindowMotion();
  root_.Opacity(1);
  root_.IsHitTestVisible(true);
  morphOutgoing_ = Layer::None;
  UpdateVisibleLayer();
}

void OverlayWindow::SettleMotion() {
  if (!windowMotionFrame_.value) return;
  HideScreenEdge();
  FinishMorph();
  LayoutWindow();
  ApplyState();
}

void OverlayWindow::ApplyPresentation() {
  const classtime::PresentationSettings presentation =
      Services::Instance().config().presentation;
  const View previousView = view_;
  const Layer outgoing = CurrentLayer();
  const bool viewChanged = presentation.view != view_;
  const bool wasScreenEdge = previousView == View::ScreenEdge;
  const bool autoHideChanged = presentation.autoHideAtEdge != autoHideAtEdge_;
  const bool flushChanged = presentation.flushWithEdge != flushWithEdge_;
  const bool coverTaskbarChanged = presentation.coverTaskbar != coverTaskbar_;
  const bool edgeChanged = presentation.edge != edge_;
  const bool offsetChanged = presentation.offsetX != offsetX_ || presentation.offsetY != offsetY_;
  const bool monitorChanged = presentation.monitorId != monitorId_;
  const auto& style = presentation.style;
  const bool barSizeChanged = presentation.view == View::Bar &&
      (style.barLengthDip != style_.barLengthDip || style.barThicknessDip != style_.barThicknessDip ||
       style.barCornerDip != style_.barCornerDip);
  const bool ringSizeChanged = presentation.view == View::Ring &&
      (style.ringDiameterDip != style_.ringDiameterDip || style.ringStyle != style_.ringStyle);
  const bool layoutChanged = viewChanged || monitorChanged || edgeChanged || offsetChanged ||
                             flushChanged || coverTaskbarChanged || barSizeChanged || ringSizeChanged;
  const bool orientationChanged = IsSideEdge(edge_) != IsSideEdge(presentation.edge);

  view_ = presentation.view;
  allDay_ = presentation.scope == classtime::PresentationSettings::Scope::AllDay;
  monitorId_ = presentation.monitorId;
  edge_ = presentation.edge;
  offsetX_ = presentation.offsetX;
  offsetY_ = presentation.offsetY;
  flushWithEdge_ = presentation.flushWithEdge;
  hideInFullscreen_ = presentation.hideInFullscreen;
  coverTaskbar_ = presentation.coverTaskbar;
  if (!(style == style_)) visualSignature_.clear();
  style_ = style;

  autoHideAtEdge_ = presentation.autoHideAtEdge;
  UpdateFullscreenSuppression();
  if (fullscreenSuppressed_) {
    if (autoHideChanged) edgeCollapsed_ = view_ != View::ScreenEdge && autoHideAtEdge_;
    UpdateVisibleLayer();
    if (visible_) StartTimer();
    return;
  }

  const bool canAnimate = visible_ && !IsIconic(hwnd_) && !dragPointer_ &&
                          !monitorChanged && Motion::Enabled();
  if (view_ == View::ScreenEdge || wasScreenEdge) {
    if (viewChanged) {
      if (dragPointer_) FinishDrag(false);
      StopSnap();
      if (edgeTimer_) edgeTimer_.Stop();
      if (edgeMotion_ || edgeClosing_) ResetEdgeMotion();
      HideDetail();
      if (canAnimate) {
        if (!wasScreenEdge) morphOutgoing_ = outgoing;
        AnimateScreenEdge(!wasScreenEdge);
      } else {
        FinishMorph();
        HideScreenEdge();
        edgeCollapsed_ = !wasScreenEdge ? false : autoHideAtEdge_;
        UpdateVisibleLayer();
        if (wasScreenEdge) {
          LayoutWindow();
          if (visible_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        } else {
          ShowWindow(hwnd_, SW_HIDE);
        }
      }
    }
    if (visible_) {
      StartTimer();
      ApplyState();
    }
    return;
  }
  if (autoHideChanged) {
    if (visible_ && (edgeCollapsed_ != autoHideAtEdge_ || edgeClosing_)) {
      StopSnap();
      SetEdgeCollapsed(autoHideAtEdge_, true);
      return;
    }
    edgeCollapsed_ = autoHideAtEdge_;
  }

  if (layoutChanged || autoHideChanged) {
    HideDetail();
    if (edgeTimer_) edgeTimer_.Stop();
    if (edgeMotion_ || edgeClosing_) ResetEdgeMotion();
    StopSnap();
    StopWindowMotion();
    HideScreenEdge();
    if (canAnimate && IsWindowVisible(hwnd_)) {
      const RECT target = ComputeLayoutRect();
      if (!IsRectEmpty(&target)) {
        const Layer previous = viewChanged || orientationChanged || morphOutgoing_ != Layer::None
                                   ? outgoing : Layer::None;
        if (previous == Layer::None) ApplyLayoutStyle();
        AnimateWindowRect(target, previous);
        return;
      }
    }
    FinishMorph();
    LayoutWindow();
    QueueEdgeCollapse();
  }
  ApplyState();
}

RECT OverlayWindow::TargetBounds(const MONITORINFO& info) const {
  return coverTaskbar_ ? info.rcMonitor : info.rcWork;
}

void OverlayWindow::UpdateFullscreenSuppression() {
  const bool shouldSuppress =
      visible_ && hideInFullscreen_ && !dragPointer_ && !dragging_ &&
      DisplayUtils::IsMonitorInFullscreen(DisplayUtils::FindMonitor(monitorId_));
  if (shouldSuppress == fullscreenSuppressed_) return;
  fullscreenSuppressed_ = shouldSuppress;
  if (fullscreenSuppressed_) {
    HideDetail();
    StopSnap();
    if (edgeTimer_) edgeTimer_.Stop();
    if (edgeMotion_ || edgeClosing_) ResetEdgeMotion();
    FinishMorph();
    HideScreenEdge();
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
    return;
  }
  if (!visible_) return;
  if (view_ == View::ScreenEdge) {
    RefreshScreenEdge();
  } else if (hwnd_ != nullptr) {
    LayoutWindow();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    EnsureTopmostAboveTaskbar();
    ApplyState();
    QueueEdgeCollapse();
  }
}

void OverlayWindow::EnsureTopmostAboveTaskbar() {
  if (!coverTaskbar_ || !visible_ || fullscreenSuppressed_) return;
  if (view_ == View::ScreenEdge) {
    for (const auto& strip : screenEdgeStrips_) {
      if (strip.hwnd && IsTaskbarAboveWindow(strip.hwnd)) {
        SetWindowPos(strip.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
    }
    return;
  }
  if (hwnd_ && !dragPointer_ && !dragging_ && IsTaskbarAboveWindow(hwnd_)) {
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void CALLBACK OverlayWindow::OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG,
                                                 DWORD, DWORD) {
  try {
    OverlayWindow* instance = activeInstance_;
    if (instance == nullptr || !instance->visible_ || !instance->window_) return;
    auto alive = instance->alive_;
    instance->window_.DispatcherQueue().TryEnqueue([instance, alive] {
      if (!*alive || !instance->visible_) return;
      instance->UpdateFullscreenSuppression();
      instance->EnsureTopmostAboveTaskbar();
    });
  } catch (...) {
  }
}

void OverlayWindow::LayoutWindow() {
  const RECT target = ComputeLayoutRect();
  if (IsRectEmpty(&target)) return;
  ApplyLayoutStyle();
  ApplyWindowRect(target);
}

// 先计算目标矩形，等可见内容落位时再同步样式。
RECT OverlayWindow::ComputeLayoutRect() {
  if (hwnd_ == nullptr || view_ == View::ScreenEdge) return {};
  HMONITOR monitor = DisplayUtils::FindMonitor(monitorId_);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return {};
  const RECT work = TargetBounds(info);
  layoutWorkArea_ = work;
  if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) != monitor) {
    SetWindowPos(hwnd_, nullptr, work.left, work.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  dpiScale_ = static_cast<double>(GetDpiForWindow(hwnd_)) / 96.0;
  if (dpiScale_ <= 0.0) dpiScale_ = 1.0;

  const bool ring = view_ == View::Ring;
  const bool side = IsSideEdge(edge_);
  const int workWidth = work.right - work.left;
  const int workHeight = work.bottom - work.top;

  // 长条整体长度可自定义：横向控制宽度，竖向（左右贴边）控制高度并受工作区约束。
  const int barLength = style_.EffectiveBarLength(side);
  double widthDip = barLength;
  double heightDip = style_.EffectiveBarThickness(side);
  if (ring) {
    widthDip = heightDip = style_.ringDiameterDip;
  } else if (side) {
    widthDip = style_.EffectiveBarThickness(side);
    const double available = std::max(80.0, workHeight / dpiScale_ - 2.0 * kSnapInsetDip);
    heightDip = std::min(static_cast<double>(barLength), available);
  }
  const int fullWidth = std::min(static_cast<int>(std::lround(widthDip * dpiScale_)), workWidth);
  const int fullHeight = std::min<int>(static_cast<int>(std::lround(heightDip * dpiScale_)), workHeight);
  const int square = ring ? std::min(fullWidth, fullHeight) : 0;
  windowWidth_ = ring ? square : fullWidth;
  windowHeight_ = ring ? square : fullHeight;

  if (edgeCollapsed_) {
    const int handleW = static_cast<int>(std::lround(kHandleWidthDip * dpiScale_));
    const int handleH = static_cast<int>(std::lround(kHandleHeightDip * dpiScale_));
    if (side) {
      windowWidth_ = std::min(windowWidth_, handleH);
      windowHeight_ = std::min(windowHeight_, handleW);
    } else {
      windowWidth_ = std::min(windowWidth_, handleW);
      windowHeight_ = std::min(windowHeight_, handleH);
    }
  }

  const int inset = (autoHideAtEdge_ || flushWithEdge_)
                        ? 0
                        : static_cast<int>(std::lround(kSnapInsetDip * dpiScale_));
  const int workCenterX = work.left + workWidth / 2;
  const int workCenterY = work.top + workHeight / 2;
  int x = 0;
  int y = 0;
  if (side) {
    x = edge_ == classtime::PresentationSettings::Edge::Left
            ? work.left + inset
            : work.right - windowWidth_ - inset;
    y = workCenterY + std::lround(offsetY_ * dpiScale_) - windowHeight_ / 2;
  } else {
    y = edge_ == classtime::PresentationSettings::Edge::Top
            ? work.top + inset
            : work.bottom - windowHeight_ - inset;
    x = workCenterX + std::lround(offsetX_ * dpiScale_) - windowWidth_ / 2;
  }
  const POINT position = ClampToWorkArea({x, y}, work, windowWidth_, windowHeight_);
  return RECT{position.x, position.y, position.x + windowWidth_, position.y + windowHeight_};
}

void OverlayWindow::ApplyLayoutStyle() {
  const bool side = IsSideEdge(edge_);
  // 左右贴边时，分节长条转为竖向排列；圆环不受影响。
  segmentPanel_.Orientation(side ? Orientation::Vertical : Orientation::Horizontal);
  barRoot_.Padding(side ? Thickness{0, kBarInsetDip, 0, kBarInsetDip}
                        : Thickness{kBarInsetDip, 0, kBarInsetDip, 0});
  edgeTrack_.Width(side ? kHandleTrackThicknessDip : kHandleTrackLengthDip);
  edgeTrack_.Height(side ? kHandleTrackLengthDip : kHandleTrackThicknessDip);
  const double radius = style_.EffectiveBarCorner(side);
  barRoot_.CornerRadius(WidgetAppearance::DockCorners(
      radius, edge_, flushWithEdge_, std::min(4.0, radius)));
}

void OverlayWindow::UpdateWindowShape() {
  if (!hwnd_) return;
  const Layer layer = CurrentLayer();
  if (layer == Layer::None) return;

  RECT window{};
  if (!GetWindowRect(hwnd_, &window)) return;
  const int width = window.right - window.left;
  const int height = window.bottom - window.top;
  const int diameter = std::min(width, height);
  if (diameter <= 0) return;
  const double scale = GetDpiForWindow(hwnd_) / 96.0;
  RECT bounds{0, 0, width, height};
  std::array<int, 4> radii{};
  const bool circle = layer == Layer::Ring && style_.ringStyle != WidgetStyle::RingStyle::RoundedSquare;
  if (layer == Layer::Ring) {
    const int inset = std::min<int>(diameter / 8, std::lround(2.0 * scale));
    bounds.left = (width - diameter) / 2 + inset;
    bounds.top = (height - diameter) / 2 + inset;
    bounds.right = bounds.left + diameter - 2 * inset;
    bounds.bottom = bounds.top + diameter - 2 * inset;
    if (!circle) {
      const int radius = std::lround(diameter * WidgetAppearance::kSquareRingCorner /
                                     WidgetAppearance::kRingFaceSize);
      radii.fill(radius);
    }
  } else {
    const auto corners = layer == Layer::Handle ? edgeHandle_.CornerRadius() : barRoot_.CornerRadius();
    const auto pixelRadius = [&](double dip) {
      return std::min(diameter / 2, static_cast<int>(std::lround(dip * scale)));
    };
    radii = {pixelRadius(corners.TopLeft), pixelRadius(corners.TopRight),
             pixelRadius(corners.BottomRight), pixelRadius(corners.BottomLeft)};
  }
  if (layer == windowRegionLayer_ && circle == windowRegionCircle_ &&
      EqualRect(&bounds, &windowRegionBounds_) && radii == windowRegionRadii_) return;

  HRGN region = circle
                    ? CreateEllipticRgnIndirect(&bounds)
                    : RoundedWindowRegion(bounds, radii);
  if (!region) return;
  if (SetWindowRgn(hwnd_, region, IsWindowVisible(hwnd_))) {
    windowRegionLayer_ = layer;
    windowRegionCircle_ = circle;
    windowRegionBounds_ = bounds;
    windowRegionRadii_ = radii;
  } else {
    DeleteObject(region);
  }
}

void OverlayWindow::StartTimer() {
  if (timer_ != nullptr) return;
  timer_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  timer_.Interval(std::chrono::seconds(1));
  timer_.Tick([this](auto const&, auto const&) { ApplyState(); });
  timer_.Start();
}

void OverlayWindow::StopTimer() {
  if (timer_ == nullptr) return;
  timer_.Stop();
  timer_ = nullptr;
}

void OverlayWindow::ShowOverlay() {
  if (hwnd_ == nullptr) return;
  visible_ = true;
  ApplyPresentation();
  StartTimer();
  UpdateFullscreenSuppression();
  if (fullscreenSuppressed_ || view_ == View::ScreenEdge) return;
  LayoutWindow();
  // Show(false) initializes the XAML window without taking keyboard focus.
  window_.AppWindow().Show(false);
  EnsureTopmostAboveTaskbar();
  ApplyState();
}

void OverlayWindow::HideOverlay() {
  visible_ = false;
  fullscreenSuppressed_ = false;
  HideDetail();
  if (dragPointer_) FinishDrag(false);
  StopSnap();
  FinishMorph();
  if (edgeTimer_) edgeTimer_.Stop();
  ResetEdgeMotion();
  edgeCollapsed_ = view_ != View::ScreenEdge && autoHideAtEdge_;
  UpdateVisibleLayer();
  StopTimer();
  HideScreenEdge();
  if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::Close() {
  if (alive_) *alive_ = false;
  if (activeInstance_ == this) activeInstance_ = nullptr;
  if (foregroundHook_ != nullptr) {
    UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
  }
  if (window_ != nullptr) {
    HideOverlay();
    window_.Close();
    window_ = nullptr;
    hwnd_ = nullptr;
  }
}

void OverlayWindow::ApplyState() {
  if (window_ == nullptr || hwnd_ == nullptr || morphOutgoing_ != Layer::None) return;
  if (visible_) {
    UpdateFullscreenSuppression();
    if (fullscreenSuppressed_) return;
  }
  // 复用可见浮窗原有的绘制刷新，显示器拔插或主屏变化后重新落到目标区域。
  // 拖动和位置动画期间不干预，提醒调度仍独立于此处。
  if (visible_ && view_ != View::ScreenEdge && !dragPointer_ && !dragging_ &&
      !snapFrame_.value && !windowMotionFrame_.value && !edgeMotion_ && !edgeClosing_) {
    const HMONITOR target = DisplayUtils::FindMonitor(monitorId_);
    MONITORINFO info{sizeof(info)};
    RECT bounds{};
    RECT intersection{};
    if (target && GetMonitorInfoW(target, &info) && GetWindowRect(hwnd_, &bounds)) {
      const RECT area = TargetBounds(info);
      const UINT dpi = GetDpiForWindow(hwnd_);
      if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) != target ||
          !EqualRect(&area, &layoutWorkArea_) || (dpi != 0 && dpi / 96.0 != dpiScale_) ||
          !IntersectRect(&intersection, &bounds, &area)) {
        HideDetail();
        LayoutWindow();
      }
    }
    EnsureTopmostAboveTaskbar();
  }
  if (edgeCollapsed_ && view_ != View::ScreenEdge) {
    RefreshEdgeHandle();
    return;
  }
  switch (view_) {
    case View::ScreenEdge:
      if (visible_) RefreshScreenEdge();
      return;
    case View::Ring: RefreshRing(); break;
    case View::Bar: RefreshBar(); break;
  }
  RefreshDetail();
}

void OverlayWindow::RefreshEdgeHandle() {
  const auto now = NowClock();
  const auto plan = Services::Instance().PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  const double filled = snapshot.current ? kHandleTrackLengthDip * snapshot.progress : 0.0;
  const auto& presentation = Services::Instance().config().presentation;
  const auto palette = ResolvePalette(presentation);
  const auto& colors = presentation.style.colors;
  const auto surface = WidgetAppearance::SurfaceColor(
      palette, view_ == View::Ring ? colors.ringBackground : colors.barBackground);
  const auto ink = WidgetAppearance::SurfaceInk(palette, surface, colors.inkColor);
  const auto updateBrush = [](const Border& border, const DependencyProperty& property,
                              Windows::UI::Color color) {
    if (const auto brush = border.GetValue(property).try_as<SolidColorBrush>()) {
      if (brush.Color() != color) brush.Color(color);
    }
  };
  updateBrush(edgeHandle_, Border::BackgroundProperty(), surface);
  updateBrush(edgeHandle_, Border::BorderBrushProperty(), WithAlpha(ink, palette.highContrast ? 255 : 40));
  updateBrush(edgeTrack_, Border::BackgroundProperty(), WidgetAppearance::TrackColor(palette, ink, 55));
  if (snapshot.current) {
    const auto fill = KindColor(palette, snapshot.current->kind);
    if (edgeFillBrush_.Color() != fill) edgeFillBrush_.Color(fill);
  }
  const bool side = IsSideEdge(edge_);
  edgeFill_.Width(side ? kHandleTrackThicknessDip : filled);
  edgeFill_.Height(side ? filled : kHandleTrackThicknessDip);
  const double radius = std::min(filled, kHandleTrackThicknessDip) / 2;
  edgeFill_.RadiusX(radius);
  edgeFill_.RadiusY(radius);
}

void OverlayWindow::RefreshScreenEdge() {
  if (fullscreenSuppressed_) {
    HideScreenEdge();
    return;
  }
  // 复用可见浮窗的刷新节奏，重新读取目标区域以适应分辨率或任务栏变化。
  const HMONITOR monitor = DisplayUtils::FindMonitor(monitorId_);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) {
    HideScreenEdge();
    return;
  }
  RECT work = TargetBounds(info);
  const int shortSide = std::min(work.right - work.left, work.bottom - work.top);
  if (shortSide < 3) {
    HideScreenEdge();
    return;
  }
  if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) != monitor) {
    SetWindowPos(hwnd_, nullptr, work.left, work.top, 1, 1,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  const auto& services = Services::Instance();
  const auto& presentation = services.config().presentation;
  const auto& style = presentation.style;
  const double dpi = GetDpiForWindow(hwnd_) / 96.0;
  const int inset = std::min<int>(std::lround(style.screenEdgeInsetDip * dpi), shortSide / 4);
  InflateRect(&work, -inset, -inset);
  const int width = work.right - work.left;
  const int height = work.bottom - work.top;
  const int thickness = std::clamp<int>(
      std::lround(style.screenEdgeThickness * dpi),
      1, std::min(width, height) / 3);

  // 左上角出发：上、右、下、左。每个像素只属于一个窗口，半透明转角不会重叠。
  const RECT bounds[] = {
      {work.left, work.top, work.right, work.top + thickness},
      {work.right - thickness, work.top + thickness, work.right, work.bottom},
      {work.left, work.bottom - thickness, work.right - thickness, work.bottom},
      {work.left, work.top + thickness, work.left + thickness, work.bottom - thickness},
  };
  const int lengths[] = {width, height - thickness, width - thickness, height - 2 * thickness};
  const int perimeter = 2 * (width + height - 2 * thickness);

  const auto now = NowClock();
  const auto plan = services.PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  const auto palette = ResolvePalette(presentation);
  auto accent = palette.accent;
  // 单色样式跟随当前状态；分色样式下各时段分别取色。
  if (snapshot.current) accent = KindColor(palette, snapshot.current->kind);
  double progress = 0;
  if (!plan.segments.empty()) {
    if (allDay_) {
      int firstMinute = plan.segments.front().start.TotalMinutes();
      int lastMinute = plan.segments.front().end.TotalMinutes();
      for (const auto& segment : plan.segments) {
        firstMinute = std::min(firstMinute, segment.start.TotalMinutes());
        lastMinute = std::max(lastMinute, segment.end.TotalMinutes());
      }
      const int elapsed = (now.hour * 60 + now.minute - firstMinute) * 60 + now.second;
      if (lastMinute > firstMinute) {
        progress = std::clamp(static_cast<double>(elapsed) / ((lastMinute - firstMinute) * 60),
                              0.0, 1.0);
      }
    } else {
      progress = snapshot.progress;
    }
  }
  const auto fill = PremultipliedBgra(accent);
  const auto track = PremultipliedBgra(WidgetAppearance::EdgeTrackColor(palette));
  std::array<std::vector<uint32_t>, 4> timelines;
  if (allDay_ && style.screenEdgeColors == WidgetStyle::ScreenEdgeColors::Schedule) {
    const auto runs = BuildScreenEdgeRuns(plan.segments, now);
    if (!runs.empty()) {
      for (size_t i = 0; i < timelines.size(); ++i) timelines[i].assign(lengths[i], track);
      const std::array<double, 4> sideLengths = {
          static_cast<double>(lengths[0]), static_cast<double>(lengths[1]),
          static_cast<double>(lengths[2]), static_cast<double>(lengths[3])};
      for (const auto& run : ProjectScreenEdgeRuns(runs, sideLengths)) {
        auto& timeline = timelines[run.side];
        const int begin = std::clamp(static_cast<int>(std::lround(run.begin)), 0, lengths[run.side]);
        const int end = std::clamp(static_cast<int>(std::lround(run.end)), 0, lengths[run.side]);
        const auto color = WidgetAppearance::ScreenEdgeRunColor(palette, run.kind, run.elapsed);
        std::fill(timeline.begin() + begin, timeline.begin() + end, PremultipliedBgra(color));
      }
      if (style.screenEdgeGradient && !palette.highContrast) {
        // 仅在颜色交界处采样渐变，转角两侧沿同一条时间线连续过渡。
        const auto stops = WidgetAppearance::ScreenEdgeColorStops(runs, palette);
        int sideStart = 0;
        for (size_t side = 0; side < timelines.size(); ++side) {
          auto& timeline = timelines[side];
          for (size_t i = 1; i + 1 < stops.size(); i += 2) {
            const int begin = std::clamp(static_cast<int>(std::floor(
                stops[i].position * perimeter - sideStart)), 0, lengths[side]);
            const int end = std::clamp(static_cast<int>(std::ceil(
                stops[i + 1].position * perimeter - sideStart)), 0, lengths[side]);
            for (int pixel = begin; pixel < end; ++pixel) {
              const double position = (sideStart + pixel + 0.5) / perimeter;
              timeline[pixel] = PremultipliedBgra(
                  WidgetAppearance::ScreenEdgeColorAt(stops, position));
            }
          }
          sideStart += lengths[side];
        }
      }
    }
  }
  const bool showHighlight =
      style.screenEdgeHighlight && !palette.highContrast && !plan.segments.empty() &&
      progress > 0.0001 && progress < 0.9999;
  const bool animateHighlight = showHighlight && Motion::Enabled();
  const double phase = animateHighlight ? CurrentHighlightPhase() : 0.65;
  highlightPhase_ = phase;
  const int highlightHead = showHighlight ? static_cast<int>(std::lround(progress * perimeter)) : -1;
  const int alpha = stripAlpha_;
  const bool frameChanged = !EqualRect(&work, &screenEdgeFrame_);
  screenEdgeFrame_ = work;
  int remaining = static_cast<int>(std::lround(progress * perimeter));
  int sideStart = 0;
  for (size_t i = 0; i < screenEdgeStrips_.size(); ++i) {
    auto& strip = screenEdgeStrips_[i];
    if (!strip.hwnd) {
      strip.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                       WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                   L"STATIC", L"ClassTime Screen Edge", WS_POPUP,
                                   bounds[i].left, bounds[i].top, 0, 0, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    }
    const int pixels = std::clamp(remaining, 0, lengths[i]);
    remaining -= pixels;
    const StripHighlight highlight{showHighlight, highlightHead, sideStart, perimeter, thickness,
                                   phase};
    sideStart += lengths[i];
    if (pixels == strip.filledPixels && fill == strip.fill && track == strip.track &&
        timelines[i] == strip.timeline && strip.highlight == highlight &&
        style.screenEdgeStyle == strip.style && alpha == strip.alpha && !frameChanged &&
        EqualRect(&bounds[i], &strip.bounds)) continue;
    if (!strip.hwnd || !RenderEdgeStrip(strip.hwnd, bounds[i], screenEdgeFrame_, i % 2 == 0, i >= 2,
                                        pixels, fill, track, timelines[i],
                                        highlight.enabled, highlight.headPixel,
                                        highlight.sideStart, highlight.perimeter,
                                        highlight.thickness, highlight.phase, style.screenEdgeStyle, alpha)) {
      HideScreenEdge();
      return;
    }
    strip.bounds = bounds[i];
    strip.filledPixels = pixels;
    strip.fill = fill;
    strip.track = track;
    strip.timeline = std::move(timelines[i]);
    strip.highlight = highlight;
    strip.style = style.screenEdgeStyle;
    strip.alpha = alpha;
    if (!IsWindowVisible(strip.hwnd)) ShowWindow(strip.hwnd, SW_SHOWNOACTIVATE);
  }
  UpdateHighlightTimer(animateHighlight);
  EnsureTopmostAboveTaskbar();
}

void OverlayWindow::HideScreenEdge() {
  UpdateHighlightTimer(false);
  stripAlpha_ = 255;
  for (auto& strip : screenEdgeStrips_) {
    if (strip.hwnd) DestroyWindow(strip.hwnd);
    strip = {};
  }
}

void OverlayWindow::RenderStrips(int alpha) {
  for (size_t i = 0; i < screenEdgeStrips_.size(); ++i) {
    auto& strip = screenEdgeStrips_[i];
    if (!strip.hwnd || strip.filledPixels < 0 || alpha == strip.alpha) continue;
    if (RenderEdgeStrip(strip.hwnd, strip.bounds, screenEdgeFrame_, i % 2 == 0, i >= 2,
                        strip.filledPixels,
                        strip.fill, strip.track, strip.timeline,
                        strip.highlight.enabled, strip.highlight.headPixel,
                        strip.highlight.sideStart, strip.highlight.perimeter,
                        strip.highlight.thickness, strip.highlight.phase, strip.style, alpha)) {
      strip.alpha = alpha;
    }
  }
}

double OverlayWindow::CurrentHighlightPhase() {
  constexpr double kCycleMs = 2350.0;
  const auto now = std::chrono::steady_clock::now();
  if (highlightEpoch_.time_since_epoch().count() == 0) highlightEpoch_ = now;
  const double elapsedMs =
      std::chrono::duration<double, std::milli>(now - highlightEpoch_).count();
  return std::fmod(elapsedMs, kCycleMs) / kCycleMs;
}

void OverlayWindow::UpdateHighlightTimer(bool active) {
  if (!active) {
    if (highlightTimer_ != nullptr) highlightTimer_.Stop();
    highlightEpoch_ = {};
    return;
  }
  if (highlightTimer_ == nullptr && window_ != nullptr) {
    highlightTimer_ = window_.DispatcherQueue().CreateTimer();
    highlightTimer_.IsRepeating(true);
    highlightTimer_.Interval(std::chrono::milliseconds(70));
    highlightTimer_.Tick([this](auto const&, auto const&) { TickScreenEdgeHighlight(); });
  }
  if (highlightTimer_ != nullptr && !highlightTimer_.IsRunning()) {
    highlightEpoch_ = std::chrono::steady_clock::now();
    highlightTimer_.Start();
  }
}

void OverlayWindow::TickScreenEdgeHighlight() {
  if (!visible_ || fullscreenSuppressed_ || view_ != View::ScreenEdge || !Motion::Enabled()) {
    UpdateHighlightTimer(false);
    return;
  }
  if (windowMotionFrame_.value) return;
  highlightPhase_ = CurrentHighlightPhase();
  for (size_t i = 0; i < screenEdgeStrips_.size(); ++i) {
    auto& strip = screenEdgeStrips_[i];
    if (!strip.hwnd || !strip.highlight.enabled || strip.highlight.headPixel < 0 ||
        strip.highlight.perimeter <= 0 || strip.highlight.thickness <= 0) continue;
    const bool horizontal = i % 2 == 0;
    const int length = horizontal ? (strip.bounds.right - strip.bounds.left)
                                  : (strip.bounds.bottom - strip.bounds.top);
    const auto spec = BuildScreenEdgeHighlightSpec(strip.highlight.perimeter,
                                                   strip.highlight.thickness);
    const double hlBegin = strip.highlight.headPixel - spec.tailLength;
    const double hlEnd = strip.highlight.headPixel + spec.leadLength;
    const double sideBegin = static_cast<double>(strip.highlight.sideStart);
    const double sideEnd = sideBegin + length;
    // 只重绘真正与流光/高光区间相交的边（通常仅 1 条），避免无谓重绘其余三边。
    if (hlEnd <= sideBegin || hlBegin >= sideEnd) continue;
    strip.highlight.phase = highlightPhase_;
    RenderEdgeStrip(strip.hwnd, strip.bounds, screenEdgeFrame_, horizontal, i >= 2,
                    strip.filledPixels,
                    strip.fill, strip.track, strip.timeline,
                    strip.highlight.enabled, strip.highlight.headPixel,
                    strip.highlight.sideStart, strip.highlight.perimeter,
                    strip.highlight.thickness, strip.highlight.phase, strip.style, strip.alpha);
  }
}

void OverlayWindow::RefreshBar() {
  const auto& services = Services::Instance();
  const auto now = NowClock();
  const auto plan = services.PlanFor(now);
  const auto state = BuildWidgetState(plan.segments, now, services.config().presentation.scope);
  const auto& snapshot = state.snapshot;
  const auto& presentation = services.config().presentation;
  const auto palette = ResolvePalette(presentation);
  ApplyBarSurface(palette);
  if (!barPalette_ || !(*barPalette_ == palette)) {
    barPalette_ = palette;
    visualSignature_.clear();
  }

  RECT client{};
  if (!GetClientRect(hwnd_, &client)) return;
  const int clientWidth = client.right - client.left;
  const int clientHeight = client.bottom - client.top;
  const bool vertical = IsSideEdge(edge_);
  const auto& visible = state.visibleSegments;
  std::string signature = std::string(vertical ? "v:" : "h:") + std::to_string(clientWidth) +
                          ":" + std::to_string(clientHeight) + ":" + std::to_string(dpiScale_);
  for (const auto& segment : visible) {
    signature += ":" + segment.id + ":" + std::to_string(segment.DurationMinutes()) + ":" +
                 classtime::SegmentKindToString(segment.kind);
  }

  if (signature != visualSignature_) {
    HideDetail();
    visualSignature_ = signature;
    visuals_.clear();
    segmentPanel_.Children().Clear();

    if (visible.empty()) {
      auto empty = MakeText(vertical ? 9 : 12, *barInk_);
      empty.Text(vertical ? L"无安排" : L"今天暂无安排");
      segmentPanel_.Children().Append(empty);
    } else {
      int totalMinutes = 0;
      for (const auto& segment : visible) totalMinutes += std::max(1, segment.DurationMinutes());
      const double contentLength = std::max(
          1.0, (vertical ? clientHeight : clientWidth) / dpiScale_ -
                   2 * (kBarInsetDip + kSurfaceStrokeDip));
      const double gap = std::min<double>(style_.barGapDip, contentLength / (2.0 * visible.size()));
      segmentPanel_.Spacing(gap);
      const double available = contentLength - gap * (visible.size() - 1);
      const double minimum = std::min(vertical ? 4.0 : 6.0, available / (2.0 * visible.size()));
      const double weightedLength = available - minimum * visible.size();

      for (size_t i = 0; i < visible.size(); ++i) {
        const auto& segment = visible[i];
        SegmentVisual visual;
        const double length = minimum + weightedLength * segment.DurationMinutes() / totalMinutes;
        visual.length = length;

        visual.host = Grid();
        visual.host.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
        if (vertical) {
          visual.host.Width(style_.EffectiveBarThickness(vertical) - 4);
          visual.host.Height(length);
          visual.host.HorizontalAlignment(HorizontalAlignment::Center);
        } else {
          visual.host.Width(length);
          visual.host.Height(style_.EffectiveBarThickness(vertical) - 4);
        }
        visual.host.VerticalAlignment(VerticalAlignment::Center);

        const double thickness = WidgetAppearance::BarSegmentThickness(style_, segment.kind, vertical);
        visual.thickness = thickness;
        const double radius = WidgetAppearance::BarSegmentCorner(style_, length, thickness);
        const auto color = KindColor(palette, segment.kind);
        visual.track = Shapes::Rectangle();
        visual.track.RadiusX(radius);
        visual.track.RadiusY(radius);
        visual.track.Fill(SolidColorBrush(WidgetAppearance::TrackColor(palette, color, 39)));
        if (vertical) {
          visual.track.Width(thickness);
          visual.track.HorizontalAlignment(HorizontalAlignment::Center);
          visual.track.VerticalAlignment(VerticalAlignment::Stretch);
        } else {
          visual.track.Height(thickness);
          visual.track.HorizontalAlignment(HorizontalAlignment::Stretch);
          visual.track.VerticalAlignment(VerticalAlignment::Center);
        }
        visual.host.Children().Append(visual.track);

        visual.fill = Shapes::Rectangle();
        visual.fill.RadiusX(radius);
        visual.fill.RadiusY(radius);
        visual.fill.Fill(SolidColorBrush(color));
        if (vertical) {
          // 填充和轨道同宽，否则会铺满整个 24 DIP 的宿主，看起来像一根粗柱子。
          visual.fill.Width(thickness);
          visual.fill.HorizontalAlignment(HorizontalAlignment::Center);
          visual.fill.VerticalAlignment(VerticalAlignment::Top);
          visual.fill.Height(0);
        } else {
          // 横向同理：高度必须跟轨道一致，不能 Stretch 到 24 DIP。
          visual.fill.Height(thickness);
          visual.fill.VerticalAlignment(VerticalAlignment::Center);
          visual.fill.HorizontalAlignment(HorizontalAlignment::Left);
          visual.fill.Width(0);
        }
        visual.host.Children().Append(visual.fill);

        visual.host.PointerMoved([this, id = segment.id](auto const& sender, auto const&) {
          ShowDetail(sender.template as<FrameworkElement>(), id);
        });
        segmentPanel_.Children().Append(visual.host);
        visuals_.push_back(visual);
      }
    }
  }

  // 更新填充长度，悬停卡独立更新。
  for (size_t i = 0; i < visuals_.size() && i < visible.size(); ++i) {
    const auto& segment = visible[i];
    const double progress = WidgetSegmentProgress(segment, now, snapshot);
    const auto& visual = visuals_[i];
    const double filled = visual.length * progress;
    const double radius = WidgetAppearance::BarSegmentCorner(style_, filled, visual.thickness);
    visual.fill.RadiusX(radius);
    visual.fill.RadiusY(radius);
    if (vertical) {
      visual.fill.Height(filled);
    } else {
      visual.fill.Width(filled);
    }
  }
}

void OverlayWindow::RefreshRing() {
  const auto now = NowClock();
  const auto& services = Services::Instance();
  const auto& presentation = services.config().presentation;
  const auto plan = services.PlanFor(now);
  const auto state = BuildWidgetState(plan.segments, now, presentation.scope);
  ringDetailId_.clear();
  if (state.snapshot.current) ringDetailId_ = state.snapshot.current->id;
  else if (state.snapshot.next) ringDetailId_ = state.snapshot.next->id;
  WidgetAppearance::UpdateRingReadout(ringFace_, presentation, ResolvePalette(presentation),
                                     state, plan.segments, now);

  if (detailAnchor_ == ringRoot_) {
    if (ringDetailId_.empty()) HideDetail(true);
    else detailSegmentId_ = ringDetailId_;
  }
}

}  // namespace ClassTimeApp
