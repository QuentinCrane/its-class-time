#include "pch.h"
#include "overlay_window.h"
#include "services.h"
#include "ui_motion.h"

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

constexpr double kBarWidthDip = 680.0;
constexpr double kBarHeightDip = 32.0;
constexpr double kSideBarThicknessDip = 34.0;
constexpr double kSideBarLengthDip = 420.0;
constexpr double kBarHostThicknessDip = 24.0;
constexpr double kRingDiameterDip = 156.0;
constexpr double kBarInsetDip = 8.0;
constexpr float kRingCenter = kRingDiameterDip / 2;
constexpr float kRingRadius = kRingCenter - 10;
constexpr double kRingStroke = 7.0;
constexpr double kDetailWidth = 280.0;
constexpr double kDetailTrackWidth = 194.0;
constexpr int kSnapInsetDip = 12;
constexpr double kHandleWidthDip = 84;
constexpr double kHandleHeightDip = 12;

struct Palette {
  Windows::UI::Color background;
  Windows::UI::Color foreground;
  Windows::UI::Color accent;
  Windows::UI::Color rest;
  Windows::UI::Color lunch;
};

Windows::UI::Color WithAlpha(Windows::UI::Color color, uint8_t alpha) {
  color.A = alpha;
  return color;
}

Palette CurrentPalette() {
  Palette palette;
  try {
    UISettings settings;
    palette.background = settings.GetColorValue(UIColorType::Background);
    palette.foreground = settings.GetColorValue(UIColorType::Foreground);
    palette.accent = settings.GetColorValue(UIColorType::Accent);
  } catch (...) {
    palette.background = Windows::UI::ColorHelper::FromArgb(255, 250, 250, 252);
    palette.foreground = Windows::UI::ColorHelper::FromArgb(255, 28, 30, 36);
    palette.accent = Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237);
  }
  palette.rest = Windows::UI::ColorHelper::FromArgb(255, 63, 167, 150);
  palette.lunch = Windows::UI::ColorHelper::FromArgb(255, 224, 164, 88);
  return palette;
}

Windows::UI::Color KindColor(const Palette& palette, classtime::SegmentKind kind) {
  switch (kind) {
    case classtime::SegmentKind::Focus: return palette.accent;
    case classtime::SegmentKind::Break: return palette.rest;
    case classtime::SegmentKind::LongBreak: return palette.rest;
    case classtime::SegmentKind::Lunch: return palette.lunch;
  }
  return palette.accent;
}

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

uint32_t PremultipliedBgra(Windows::UI::Color color) {
  const uint32_t alpha = color.A;
  return (alpha << 24) | ((color.R * alpha / 255) << 16) |
         ((color.G * alpha / 255) << 8) | (color.B * alpha / 255);
}

bool RenderEdgeStrip(HWND hwnd, const RECT& bounds, bool horizontal, bool reverse,
                     int filledPixels, uint32_t fill, uint32_t track, int alpha = 255) {
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
      if (horizontal) {
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

HMONITOR FindMonitor(const std::string& id) {
  struct Search {
    std::wstring device;
    HMONITOR monitor = nullptr;
  } search{ToWide(id)};
  if (!search.device.empty()) {
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
      auto& search = *reinterpret_cast<Search*>(data);
      MONITORINFOEXW info{};
      info.cbSize = sizeof(info);
      if (GetMonitorInfoW(monitor, &info) && search.device == info.szDevice) {
        search.monitor = monitor;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
  }
  return search.monitor ? search.monitor : MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
}

POINT ClampToWorkArea(POINT point, const RECT& work, int width, int height) {
  point.x = std::clamp(point.x, work.left, std::max(work.left, work.right - width));
  point.y = std::clamp(point.y, work.top, std::max(work.top, work.bottom - height));
  return point;
}

}  // namespace

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
  const auto palette = CurrentPalette();
  edgeHandle_ = Border();
  edgeHandle_.Background(SolidColorBrush(palette.background));
  edgeGrip_ = Border();
  edgeGrip_.Width(34);
  edgeGrip_.Height(3);
  edgeGrip_.CornerRadius(CornerRadius{2, 2, 2, 2});
  edgeGrip_.HorizontalAlignment(HorizontalAlignment::Center);
  edgeGrip_.VerticalAlignment(VerticalAlignment::Center);
  edgeGrip_.Background(SolidColorBrush(WithAlpha(palette.accent, 210)));
  edgeHandle_.Child(edgeGrip_);
  edgeHandle_.Visibility(Visibility::Collapsed);
  root_.Children().Append(edgeHandle_);
  BindDrag();

  // WinUI 首次 Activate 初始化内容岛。后续从托盘恢复使用无激活显示。
  window_.Activate();

  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  if (hwnd_ != nullptr) {
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
  }
  window_.SizeChanged([this](auto const&, auto const&) { UpdateWindowShape(); });

  ApplyPresentation();
}

void OverlayWindow::BuildBar() {
  const Palette palette = CurrentPalette();

  barRoot_ = Border();
  barRoot_.CornerRadius(CornerRadius{12, 12, 12, 12});
  barRoot_.Background(SolidColorBrush(palette.background));
  barRoot_.Padding(Thickness{kBarInsetDip, 0, kBarInsetDip, 0});

  segmentPanel_ = StackPanel();
  segmentPanel_.Orientation(Orientation::Horizontal);
  segmentPanel_.VerticalAlignment(VerticalAlignment::Center);
  barRoot_.Child(segmentPanel_);
  root_.Children().Append(barRoot_);
}

void OverlayWindow::BuildRing() {
  const auto palette = CurrentPalette();
  auto dial = Canvas();
  dial.Width(kRingDiameterDip);
  dial.Height(kRingDiameterDip);
  dial.Background(SolidColorBrush(palette.background));

  EllipseGeometry circle;
  circle.Center({kRingCenter, kRingCenter});
  circle.RadiusX(kRingRadius);
  circle.RadiusY(kRingRadius);
  Shapes::Path track;
  track.Data(circle);
  track.Stroke(SolidColorBrush(WithAlpha(palette.foreground, 32)));
  track.StrokeThickness(kRingStroke);
  dial.Children().Append(track);

  PathGeometry progress;
  PathFigure figure;
  figure.StartPoint({kRingCenter, kRingCenter - kRingRadius});
  ringSweep_ = ArcSegment();
  ringSweep_.Size({kRingRadius, kRingRadius});
  ringSweep_.SweepDirection(SweepDirection::Clockwise);
  ringSweep_.Point(figure.StartPoint());
  figure.Segments().Append(ringSweep_);
  progress.Figures().Append(figure);
  ringArc_ = Shapes::Path();
  ringAccent_ = SolidColorBrush(palette.accent);
  ringArc_.Data(progress);
  ringArc_.Stroke(ringAccent_);
  ringArc_.StrokeThickness(kRingStroke);
  ringArc_.StrokeStartLineCap(PenLineCap::Round);
  ringArc_.StrokeEndLineCap(PenLineCap::Round);
  ringArc_.Visibility(Visibility::Collapsed);
  dial.Children().Append(ringArc_);

  // 读数固定在盘面中央，切换范围时只改变下方文字。
  const auto placeReadout = [&](const TextBlock& text, double top) {
    text.Width(kRingDiameterDip);
    text.TextAlignment(TextAlignment::Center);
    Canvas::SetTop(text, top);
    dial.Children().Append(text);
  };
  const auto secondary = WithAlpha(palette.foreground, 180);
  ringLabel_ = MakeText(12, secondary);
  ringTime_ = MakeText(32, palette.foreground, true);
  ringTime_.FontFamily(FontFamily(L"Bahnschrift"));
  ringSummary_ = MakeText(11, secondary);
  placeReadout(ringLabel_, 43);
  placeReadout(ringTime_, 58);
  placeReadout(ringSummary_, 100);

  Viewbox face;
  face.Stretch(Stretch::Uniform);
  face.Child(dial);
  ringRoot_ = Grid();
  ringRoot_.Children().Append(face);
  ringRoot_.PointerMoved([this](auto const&, auto const&) {
    if (!ringDetailId_.empty()) ShowDetail(ringRoot_, ringDetailId_);
  });
  root_.Children().Append(ringRoot_);
  ringRoot_.Visibility(Visibility::Collapsed);
}

void OverlayWindow::BuildDetailCard() {
  const auto palette = CurrentPalette();
  const auto secondary = WithAlpha(palette.foreground, 175);

  detailCard_ = Border();
  detailCard_.Width(kDetailWidth);
  detailCard_.Padding(Thickness{14, 12, 14, 12});
  detailCard_.Background(SolidColorBrush(palette.background));
  detailCard_.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 42)));
  detailCard_.BorderThickness(Thickness{1, 1, 1, 1});
  detailCard_.CornerRadius(CornerRadius{14, 14, 14, 14});

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

  const auto accent = KindColor(CurrentPalette(), selected->kind);
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
  const int duration = show ? 190 : 145;
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
  offsetX_ = 0;
  offsetY_ = 0;
  services.UpdatePresentation(presentation);

  RECT bounds{};
  MONITORINFO info{sizeof(info)};
  if (!GetWindowRect(hwnd_, &bounds) ||
      !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
    LayoutWindow();
    return;
  }
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  POINT target{bounds.left, bounds.top};
  if (IsSideEdge(edge_)) {
    target.y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
  } else {
    target.x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
  }
  SnapTo(ClampToWorkArea(target, info.rcWork, width, height), info.rcWork);
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
    QueueEdgeCollapse();
    return;
  }

  RECT bounds{};
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetWindowRect(hwnd_, &bounds) ||
      !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
    LayoutWindow();
    QueueEdgeCollapse();
    return;
  }
  dpiScale_ = root_.XamlRoot().RasterizationScale();
  const RECT& work = info.rcWork;
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

  // 取最近的一条边；四个方向一视同仁，避免只比较上下。
  auto targetEdge = classtime::PresentationSettings::Edge::Bottom;
  int nearest = std::abs(work.bottom - bounds.bottom);
  if (const int distance = std::abs(bounds.top - work.top); distance < nearest) {
    nearest = distance;
    targetEdge = classtime::PresentationSettings::Edge::Top;
  }
  if (const int distance = std::abs(bounds.left - work.left); distance < nearest) {
    nearest = distance;
    targetEdge = classtime::PresentationSettings::Edge::Left;
  }
  if (const int distance = std::abs(work.right - bounds.right); distance < nearest) {
    targetEdge = classtime::PresentationSettings::Edge::Right;
  }

  const bool wasSide = IsSideEdge(edge_);
  const bool nowSide = IsSideEdge(targetEdge);
  auto& services = Services::Instance();
  auto presentation = services.config().presentation;
  edge_ = presentation.edge = targetEdge;
  monitorId_ = presentation.monitorId = ToUtf8(info.szDevice);
  // 横向贴边用 offsetX 记水平位置，纵向贴边用 offsetY 记垂直位置；
  // 另一轴归零，切回原来的方向时仍然居中。
  if (nowSide) {
    offsetX_ = presentation.offsetX = 0;
    offsetY_ = presentation.offsetY =
        static_cast<int>(std::lround((oldCenterY - workCenterY) / dpiScale_));
  } else {
    offsetY_ = presentation.offsetY = 0;
    offsetX_ = presentation.offsetX =
        static_cast<int>(std::lround((oldCenterX - workCenterX) / dpiScale_));
  }
  services.UpdatePresentation(presentation);

  // 横排改竖排（或反过来）时窗口尺寸会变，直接按新布局落位；
  // 同方向换边才做回弹动画。
  if (wasSide != nowSide) {
    LayoutWindow();
    QueueEdgeCollapse();
    return;
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
    SetWindowPos(hwnd_, nullptr, point.x, point.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
  const double overshoot = std::min(18.0 * dpiScale_, 0.08 * std::abs(distance));
  const double direction = distance >= 0 ? 1.0 : -1.0;
  lastSnapPosition_ = {bounds.left, bounds.top};
  snapFrame_ = CompositionTarget::Rendering(
      [this, move, target, bounds, workArea, started, overshoot, direction,
       horizontal](auto const&, auto const&) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
                         .count() / 0.30;
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
      static_cast<int>(std::clamp(265.0 + distance / 5, 265.0, 430.0)));
  const double recoil = std::min(0.022, 12 * dpiScale_ / std::max(1.0, distance));
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
        monitor.rcWork, frameWidth, frameHeight);
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
    const double t = std::chrono::duration<double>(now - started).count() / 0.26;
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
      edgeMotion_ = Motion::Slide(root_, EdgeContent(), from, {}, 145);
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
                               145);
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
                               {}, collapsed ? 105 : 170);
  }
  UpdateVisibleLayer();
  LayoutWindow();
  if (collapsed) {
    StopTimer();
  } else {
    StartTimer();
    ApplyState();
  }
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
  const bool edgeChanged = presentation.edge != edge_;
  const bool offsetChanged = presentation.offsetX != offsetX_ || presentation.offsetY != offsetY_;
  const bool monitorChanged = presentation.monitorId != monitorId_;
  const bool layoutChanged = viewChanged || monitorChanged || edgeChanged || offsetChanged ||
                             flushChanged;
  const bool orientationChanged = IsSideEdge(edge_) != IsSideEdge(presentation.edge);
  const bool canAnimate = visible_ && !IsIconic(hwnd_) && !dragPointer_ &&
                          !monitorChanged && Motion::Enabled();

  view_ = presentation.view;
  allDay_ = presentation.scope == classtime::PresentationSettings::Scope::AllDay;
  monitorId_ = presentation.monitorId;
  edge_ = presentation.edge;
  offsetX_ = presentation.offsetX;
  offsetY_ = presentation.offsetY;
  flushWithEdge_ = presentation.flushWithEdge;

  autoHideAtEdge_ = presentation.autoHideAtEdge;
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
      if (edgeCollapsed_ && view_ != View::ScreenEdge) StopTimer();
      else StartTimer();
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

void OverlayWindow::LayoutWindow() {
  const RECT target = ComputeLayoutRect();
  if (IsRectEmpty(&target)) return;
  ApplyLayoutStyle();
  ApplyWindowRect(target);
}

// 先计算目标矩形，等可见内容落位时再同步样式。
RECT OverlayWindow::ComputeLayoutRect() {
  if (hwnd_ == nullptr || view_ == View::ScreenEdge) return {};
  HMONITOR monitor = FindMonitor(monitorId_);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return {};
  const RECT work = info.rcWork;
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

  double widthDip = kBarWidthDip;
  double heightDip = kBarHeightDip;
  if (ring) {
    widthDip = heightDip = kRingDiameterDip;
  } else if (side) {
    widthDip = kSideBarThicknessDip;
    const double available = std::max(80.0, workHeight / dpiScale_ - 2.0 * kSnapInsetDip);
    heightDip = std::min(kSideBarLengthDip, available);
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
  edgeGrip_.Width(side ? 3 : 34);
  edgeGrip_.Height(side ? 34 : 3);
  // 紧贴边缘时把靠边一侧的圆角削平，视觉上像从任务栏/屏幕边缘长出来。
  CornerRadius corner{12, 12, 12, 12};
  if (flushWithEdge_) {
    switch (edge_) {
      case classtime::PresentationSettings::Edge::Top: corner = {0, 0, 12, 12}; break;
      case classtime::PresentationSettings::Edge::Left: corner = {0, 12, 12, 0}; break;
      case classtime::PresentationSettings::Edge::Right: corner = {12, 0, 0, 12}; break;
      default: corner = {12, 12, 0, 0}; break;
    }
  }
  barRoot_.CornerRadius(corner);
}

void OverlayWindow::UpdateWindowShape() {
  if (!hwnd_) return;
  RECT desired{};
  const Layer layer = CurrentLayer();
  if (layer == Layer::Ring) {
    RECT window{};
    if (!GetWindowRect(hwnd_, &window)) return;
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int diameter = std::min(width, height);
    if (diameter <= 0) return;
    // 给原生窗口边线留出裁剪余量，避免表盘切点出现残边。
    const int inset = std::min<int>(diameter / 8, std::lround(2.0 * GetDpiForWindow(hwnd_) / 96.0));
    desired.left = (width - diameter) / 2 + inset;
    desired.top = (height - diameter) / 2 + inset;
    desired.right = desired.left + diameter - 2 * inset;
    desired.bottom = desired.top + diameter - 2 * inset;
  }
  // 紧贴边缘的横条需要直角落到任务栏/屏幕边缘；圆环本身用椭圆裁剪。
  const bool wantSharp = !IsRectEmpty(&desired) || (layer == Layer::Bar && flushWithEdge_);
  if (EqualRect(&desired, &ringRegionBounds_) && wantSharp == sharpWindowCorners_) return;

  HRGN region = nullptr;
  if (!IsRectEmpty(&desired)) {
    region = CreateEllipticRgn(desired.left, desired.top, desired.right, desired.bottom);
    if (!region) return;
  }
  if (SetWindowRgn(hwnd_, region, IsWindowVisible(hwnd_))) {
    ringRegionBounds_ = desired;
    sharpWindowCorners_ = wantSharp;
    // DWM 的外框独立于窗口裁剪区域：圆环和紧贴边缘时都不要方形外框。
    const COLORREF border = wantSharp ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const auto corners = wantSharp ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  } else if (region) {
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
  if (view_ == View::ScreenEdge) return;
  LayoutWindow();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  if (!edgeCollapsed_) StartTimer();
  ApplyState();
}

void OverlayWindow::HideOverlay() {
  visible_ = false;
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
  if (window_ != nullptr) {
    HideOverlay();
    window_.Close();
    window_ = nullptr;
    hwnd_ = nullptr;
  }
}

void OverlayWindow::ApplyState() {
  if (window_ == nullptr || hwnd_ == nullptr || morphOutgoing_ != Layer::None) return;
  if (edgeCollapsed_ && view_ != View::ScreenEdge) return;
  switch (view_) {
    case View::ScreenEdge:
      if (visible_) RefreshScreenEdge();
      return;
    case View::Ring: RefreshRing(); break;
    case View::Bar: RefreshBar(); break;
  }
  RefreshDetail();
}

void OverlayWindow::RefreshScreenEdge() {
  // 复用可见浮窗的刷新节奏，重新读取工作区以适应分辨率或任务栏变化。
  const HMONITOR monitor = FindMonitor(monitorId_);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) {
    HideScreenEdge();
    return;
  }
  const RECT& work = info.rcWork;
  const int width = work.right - work.left;
  const int height = work.bottom - work.top;
  if (std::min(width, height) < 3) {
    HideScreenEdge();
    return;
  }
  if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) != monitor) {
    SetWindowPos(hwnd_, nullptr, work.left, work.top, 1, 1,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  const auto& services = Services::Instance();
  const int thickness = std::clamp<int>(
      std::lround(services.config().presentation.screenEdgeThickness * GetDpiForWindow(hwnd_) / 96.0),
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
  const auto palette = CurrentPalette();
  auto accent = palette.accent;
  // 全天范围也跟随当前状态着色：上课用强调色、课间用休息色、午休用暖黄色。
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
  const auto track = PremultipliedBgra(Windows::UI::ColorHelper::FromArgb(172, 100, 117, 137));
  const int alpha = stripAlpha_;
  int remaining = static_cast<int>(std::lround(progress * perimeter));
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
    if (pixels == strip.filledPixels && fill == strip.fill && track == strip.track &&
        alpha == strip.alpha && EqualRect(&bounds[i], &strip.bounds)) continue;
    if (!strip.hwnd || !RenderEdgeStrip(strip.hwnd, bounds[i], i % 2 == 0, i >= 2,
                                        pixels, fill, track, alpha)) {
      HideScreenEdge();
      return;
    }
    strip.bounds = bounds[i];
    strip.filledPixels = pixels;
    strip.fill = fill;
    strip.track = track;
    strip.alpha = alpha;
    if (!IsWindowVisible(strip.hwnd)) ShowWindow(strip.hwnd, SW_SHOWNOACTIVATE);
  }
}

void OverlayWindow::HideScreenEdge() {
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
    if (RenderEdgeStrip(strip.hwnd, strip.bounds, i % 2 == 0, i >= 2, strip.filledPixels,
                        strip.fill, strip.track, alpha)) {
      strip.alpha = alpha;
    }
  }
}

void OverlayWindow::RefreshBar() {
  const auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();
  const auto now = NowClock();
  const Palette palette = CurrentPalette();

  RECT client{};
  if (!GetClientRect(hwnd_, &client)) return;
  const int clientWidth = client.right - client.left;
  const int clientHeight = client.bottom - client.top;
  const bool vertical = IsSideEdge(edge_);
  const auto visible = services.VisibleSegments();
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
      auto empty = MakeText(vertical ? 11 : 12, WithAlpha(palette.foreground, 130));
      empty.Text(vertical ? L"无安排" : L"今天暂无安排");
      segmentPanel_.Children().Append(empty);
    } else {
      int totalMinutes = 0;
      for (const auto& segment : visible) totalMinutes += std::max(1, segment.DurationMinutes());
      const double contentLength =
          vertical ? std::max(1.0, clientHeight / dpiScale_ - 2 * kBarInsetDip)
                   : std::max(1.0, clientWidth / dpiScale_ - 2 * kBarInsetDip);
      const double gap = std::min(4.0, contentLength / (2.0 * visible.size()));
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
          visual.host.Width(kBarHostThicknessDip);
          visual.host.Height(length);
          visual.host.HorizontalAlignment(HorizontalAlignment::Center);
        } else {
          visual.host.Width(length);
          visual.host.Height(kBarHostThicknessDip);
        }
        visual.host.VerticalAlignment(VerticalAlignment::Center);

        const double thickness = segment.kind == classtime::SegmentKind::Break ? 6 : 9;
        visual.track = Shapes::Rectangle();
        visual.track.RadiusX(4);
        visual.track.RadiusY(4);
        visual.track.Fill(SolidColorBrush(WithAlpha(palette.foreground, 26)));
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
        visual.fill.RadiusX(4);
        visual.fill.RadiusY(4);
        visual.fill.Fill(SolidColorBrush(KindColor(palette, segment.kind)));
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
    double progress = 0.0;
    if (snapshot.current.has_value() && snapshot.current->id == segment.id) {
      progress = snapshot.progress;
    } else if (segment.end.TotalMinutes() * 60 <=
               now.hour * 3600 + now.minute * 60 + now.second) {
      progress = 1.0;
    }
    if (vertical) {
      visuals_[i].fill.Height(visuals_[i].length * progress);
    } else {
      visuals_[i].fill.Width(visuals_[i].length * progress);
    }
  }
}

void OverlayWindow::RefreshRing() {
  const auto now = NowClock();
  const int nowMinutes = now.MinutesSinceMidnight();
  const auto plan = Services::Instance().PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  std::wstring label;
  std::wstring time;
  ringDetailId_.clear();
  if (snapshot.current) {
    const auto& current = *snapshot.current;
    label = current.kind == classtime::SegmentKind::Focus
                ? L"第 " + std::to_wstring(current.focusOrdinal) + L" 节剩余"
                : SegmentKindLabel(current.kind) + L"剩余";
    const int remaining = (current.end.TotalMinutes() - nowMinutes) * 60 - now.second;
    time = remaining < 3600 ? FormatElapsed(remaining)
                            : std::to_wstring(remaining / 3600) + L":" +
                                  FormatElapsed(remaining % 3600);
    ringDetailId_ = current.id;
  } else if (snapshot.next) {
    const bool started = std::any_of(plan.segments.begin(), plan.segments.end(),
                                     [nowMinutes](const auto& segment) {
                                       return segment.start.TotalMinutes() <= nowMinutes;
                                     });
    label = started ? L"空档 · 下一段" : L"首段开始";
    time = FormatClock(snapshot.next->start);
    ringDetailId_ = snapshot.next->id;
  } else {
    time = snapshot.hasSchedule ? L"今日已结束" : L"今天没有安排";
  }
  ringLabel_.Text(label);
  ringTime_.Text(time);
  ringTime_.FontSize(label.empty() ? 18 : (time.size() > 5 ? 27 : 32));
  Canvas::SetTop(ringTime_, label.empty() ? 66 : 58);

  std::wstring summary;
  if (allDay_) {
    int completed = 0;
    int total = 0;
    for (const auto& segment : plan.segments) {
      if (segment.kind != classtime::SegmentKind::Focus) continue;
      ++total;
      if (segment.end.TotalMinutes() <= nowMinutes) ++completed;
    }
    if (total > 0) summary = L"今日 " + std::to_wstring(completed) + L" / " +
                             std::to_wstring(total) + L" 节";
  }
  ringSummary_.Text(summary);

  const bool showProgress = snapshot.current && snapshot.progress > 0;
  if (showProgress) {
    const double sweep = snapshot.progress * 2.0 * 3.14159265358979323846;
    ringAccent_.Color(KindColor(CurrentPalette(), snapshot.current->kind));
    ringSweep_.Point({static_cast<float>(kRingCenter + kRingRadius * std::sin(sweep)),
                      static_cast<float>(kRingCenter - kRingRadius * std::cos(sweep))});
    ringSweep_.IsLargeArc(snapshot.progress > 0.5);
  }
  ringArc_.Visibility(showProgress ? Visibility::Visible : Visibility::Collapsed);

  if (detailAnchor_ == ringRoot_) {
    if (ringDetailId_.empty()) HideDetail(true);
    else detailSegmentId_ = ringDetailId_;
  }
}

}  // namespace ClassTimeApp
