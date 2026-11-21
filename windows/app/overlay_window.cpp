#include "pch.h"
#include "overlay_window.h"
#include "services.h"


#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::UI::ViewManagement;

namespace ClassTimeApp {
namespace {

constexpr double kBarWidthDip = 780.0;
constexpr double kBarHeightDip = 100.0;
constexpr double kRingWidthDip = 300.0;
constexpr double kRingHeightDip = 330.0;
constexpr double kSegmentAreaWidth = 748.0;

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
  // 卡片保持接近不透明，悬浮层不抢桌面内容的注意力。
  palette.background = WithAlpha(palette.background, 246);
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

GridLength AutoLength() { return GridLengthHelper::Auto(); }
GridLength StarLength() { return GridLengthHelper::FromValueAndType(1, GridUnitType::Star); }

TextBlock MakeText(double size, Windows::UI::Color color, bool semibold = false) {
  TextBlock block;
  block.FontSize(size);
  block.Foreground(SolidColorBrush(color));
  if (semibold) block.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  return block;
}

std::wstring RemainingText(const classtime::Segment& segment, const classtime::WallClock& now) {
  const int totalSeconds = segment.DurationMinutes() * 60;
  const int elapsedSeconds =
      (now.hour * 3600 + now.minute * 60 + now.second) - segment.start.TotalMinutes() * 60;
  const int remaining = std::max(0, (totalSeconds - elapsedSeconds + 59) / 60);
  return std::to_wstring(remaining) + L" 分钟";
}

}  // namespace

OverlayWindow::OverlayWindow() { CreateNativeWindow(); }

OverlayWindow::~OverlayWindow() {
  StopTimer();
  if (window_ != nullptr) {
    try {
      window_.Close();
    } catch (...) {
    }
  }
}

void OverlayWindow::CreateNativeWindow() {
  window_ = Window();
  window_.Title(L"ClassTime Overlay");

  // 关键顺序：先配置无边框 presenter，再挂内容并激活，
  // 避免 SetBorderAndTitleBar 重建 HWND 时把已挂载的 XAML 岛拆掉。
  auto appWindow = window_.AppWindow();
  if (auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.SetBorderAndTitleBar(false, false);
    presenter.IsResizable(false);
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);
  }

  root_ = Grid();
  root_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
  window_.Content(root_);

  BuildBar();
  BuildRing();

  window_.Activate();

  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  if (hwnd_ != nullptr) {
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
  }

  created_ = true;
  ApplyPresentation();
}

void OverlayWindow::BuildBar() {
  const Palette palette = CurrentPalette();

  auto card = Border();
  card.CornerRadius(CornerRadius{18, 18, 18, 18});
  card.BorderThickness(Thickness{1, 1, 1, 1});
  card.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 24)));
  card.Background(SolidColorBrush(palette.background));
  card.Padding(Thickness{16, 10, 16, 10});

  barRoot_ = Grid();
  auto rowTop = RowDefinition();
  rowTop.Height(AutoLength());
  auto rowBars = RowDefinition();
  rowBars.Height(AutoLength());
  auto rowBottom = RowDefinition();
  rowBottom.Height(AutoLength());
  barRoot_.RowDefinitions().Append(rowTop);
  barRoot_.RowDefinitions().Append(rowBars);
  barRoot_.RowDefinitions().Append(rowBottom);
  barRoot_.RowSpacing(8);

  // 第一行：状态 + 进度百分比
  auto top = Grid();
  auto colLeft = ColumnDefinition();
  colLeft.Width(StarLength());
  auto colRight = ColumnDefinition();
  colRight.Width(AutoLength());
  top.ColumnDefinitions().Append(colLeft);
  top.ColumnDefinitions().Append(colRight);

  auto statePanel = StackPanel();
  statePanel.Orientation(Orientation::Horizontal);
  statePanel.Spacing(8);
  statePanel.VerticalAlignment(VerticalAlignment::Center);

  auto pill = Border();
  pill.CornerRadius(CornerRadius{9, 9, 9, 9});
  pill.Padding(Thickness{8, 2, 8, 3});
  pill.Background(SolidColorBrush(WithAlpha(palette.accent, 30)));
  stateText_ = MakeText(12, palette.accent, true);
  stateText_.Text(L"空闲");
  pill.Child(stateText_);
  statePanel.Children().Append(pill);

  auto hint = MakeText(12, WithAlpha(palette.foreground, 140));
  hint.Text(L"“上课”时间到！");
  statePanel.Children().Append(hint);
  top.Children().Append(statePanel);

  progressText_ = MakeText(13, WithAlpha(palette.foreground, 170), true);
  progressText_.HorizontalAlignment(HorizontalAlignment::Right);
  progressText_.VerticalAlignment(VerticalAlignment::Center);
  Grid::SetColumn(progressText_, 1);
  top.Children().Append(progressText_);
  barRoot_.Children().Append(top);

  // 第二行：分节进度
  segmentPanel_ = StackPanel();
  segmentPanel_.Orientation(Orientation::Horizontal);
  segmentPanel_.Spacing(4);
  segmentPanel_.Height(14);
  segmentPanel_.VerticalAlignment(VerticalAlignment::Center);
  Grid::SetRow(segmentPanel_, 1);
  barRoot_.Children().Append(segmentPanel_);

  // 第三行：下一段 / 暂停状态
  auto bottom = Grid();
  auto bottomLeft = ColumnDefinition();
  bottomLeft.Width(StarLength());
  auto bottomRight = ColumnDefinition();
  bottomRight.Width(AutoLength());
  bottom.ColumnDefinitions().Append(bottomLeft);
  bottom.ColumnDefinitions().Append(bottomRight);

  nextText_ = MakeText(12, WithAlpha(palette.foreground, 150));
  nextText_.TextTrimming(TextTrimming::CharacterEllipsis);
  bottom.Children().Append(nextText_);

  pauseText_ = MakeText(12, WithAlpha(palette.foreground, 150));
  pauseText_.HorizontalAlignment(HorizontalAlignment::Right);
  pauseText_.Text(L"提醒已暂停");
  pauseText_.Visibility(Visibility::Collapsed);
  Grid::SetColumn(pauseText_, 1);
  bottom.Children().Append(pauseText_);
  Grid::SetRow(bottom, 2);
  barRoot_.Children().Append(bottom);

  card.Child(barRoot_);
  root_.Children().Append(card);
  barRoot_.Visibility(Visibility::Visible);
}

void OverlayWindow::BuildRing() {
  const Palette palette = CurrentPalette();

  auto card = Border();
  card.CornerRadius(CornerRadius{24, 24, 24, 24});
  card.BorderThickness(Thickness{1, 1, 1, 1});
  card.BorderBrush(SolidColorBrush(WithAlpha(palette.foreground, 24)));
  card.Background(SolidColorBrush(palette.background));
  card.Padding(Thickness{20, 22, 20, 18});

  auto stack = StackPanel();
  stack.Spacing(10);
  stack.HorizontalAlignment(HorizontalAlignment::Center);

  auto dial = Grid();
  dial.Width(220);
  dial.Height(220);

  Shapes::Ellipse track;
  track.Width(196);
  track.Height(196);
  track.Stroke(SolidColorBrush(WithAlpha(palette.foreground, 28)));
  track.StrokeThickness(12);
  track.HorizontalAlignment(HorizontalAlignment::Center);
  track.VerticalAlignment(VerticalAlignment::Center);
  dial.Children().Append(track);

  ringArc_ = Shapes::Path();
  ringArc_.Stroke(SolidColorBrush(palette.accent));
  ringArc_.StrokeThickness(12);
  ringArc_.StrokeStartLineCap(PenLineCap::Round);
  ringArc_.StrokeEndLineCap(PenLineCap::Round);
  ringArc_.HorizontalAlignment(HorizontalAlignment::Center);
  ringArc_.VerticalAlignment(VerticalAlignment::Center);
  dial.Children().Append(ringArc_);

  ringFull_ = Shapes::Path();
  ringFull_.Stroke(SolidColorBrush(palette.accent));
  ringFull_.StrokeThickness(12);
  ringFull_.Visibility(Visibility::Collapsed);
  ringFull_.HorizontalAlignment(HorizontalAlignment::Center);
  ringFull_.VerticalAlignment(VerticalAlignment::Center);
  dial.Children().Append(ringFull_);

  auto center = StackPanel();
  center.HorizontalAlignment(HorizontalAlignment::Center);
  center.VerticalAlignment(VerticalAlignment::Center);
  center.Spacing(2);
  ringPercent_ = MakeText(34, palette.foreground, true);
  ringPercent_.HorizontalAlignment(HorizontalAlignment::Center);
  ringPercent_.Text(L"0%");
  ringLabel_ = MakeText(12, WithAlpha(palette.foreground, 160));
  ringLabel_.HorizontalAlignment(HorizontalAlignment::Center);
  ringLabel_.Text(L"空闲");
  center.Children().Append(ringPercent_);
  center.Children().Append(ringLabel_);
  dial.Children().Append(center);

  stack.Children().Append(dial);

  ringNext_ = MakeText(12, WithAlpha(palette.foreground, 160));
  ringNext_.HorizontalAlignment(HorizontalAlignment::Center);
  ringNext_.TextTrimming(TextTrimming::CharacterEllipsis);
  stack.Children().Append(ringNext_);

  ringDots_ = StackPanel();
  ringDots_.Orientation(Orientation::Horizontal);
  ringDots_.Spacing(6);
  ringDots_.HorizontalAlignment(HorizontalAlignment::Center);
  stack.Children().Append(ringDots_);

  card.Child(stack);
  ringRoot_ = Grid();
  ringRoot_.Children().Append(card);
  root_.Children().Append(ringRoot_);
  ringRoot_.Visibility(Visibility::Collapsed);
}

void OverlayWindow::ApplyPresentation() {
  const auto& presentation = Services::Instance().config().presentation;
  const bool ring = presentation.view == classtime::PresentationSettings::View::Ring;
  const bool allDay = presentation.scope == classtime::PresentationSettings::Scope::AllDay;
  const bool layoutChanged = ring != ringMode_ || allDay != allDay_ ||
                             presentation.edge != edge_ || presentation.offsetX != offsetX_ ||
                             presentation.offsetY != offsetY_;
  ringMode_ = ring;
  allDay_ = allDay;
  edge_ = presentation.edge;
  offsetX_ = presentation.offsetX;
  offsetY_ = presentation.offsetY;

  barRoot_.Visibility(ring ? Visibility::Collapsed : Visibility::Visible);
  ringRoot_.Visibility(ring ? Visibility::Visible : Visibility::Collapsed);

  if (layoutChanged) {
    visualSignature_.clear();  // 强制按新范围重建分节
    LayoutWindow();
  }
  ApplyState();
}

void OverlayWindow::LayoutWindow() {
  if (hwnd_ == nullptr) return;
  dpiScale_ = static_cast<double>(GetDpiForWindow(hwnd_)) / 96.0;
  if (dpiScale_ <= 0.0) dpiScale_ = 1.0;

  const double widthDip = ringMode_ ? kRingWidthDip : kBarWidthDip;
  const double heightDip = ringMode_ ? kRingHeightDip : kBarHeightDip;
  windowWidth_ = static_cast<int>(std::lround(widthDip * dpiScale_));
  windowHeight_ = static_cast<int>(std::lround(heightDip * dpiScale_));

  HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(monitor, &info);
  const RECT work = info.rcWork;
  const int workWidth = work.right - work.left;
  int x = work.left + (workWidth - windowWidth_) / 2 + static_cast<int>(offsetX_ * dpiScale_);
  x = std::max<int>(work.left, std::min<int>(x, work.right - windowWidth_));
  const int edgeOffset = static_cast<int>(offsetY_ * dpiScale_);
  const int y = edge_ == classtime::PresentationSettings::Edge::Top
                    ? work.top + edgeOffset
                    : work.bottom - windowHeight_ - edgeOffset;

  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, windowWidth_, windowHeight_,
               SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : 0));
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
  LayoutWindow();
  // WS_EX_NOACTIVATE：显示与激活都不会把焦点从当前应用抢走。
  window_.Activate();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  StartTimer();
  ApplyState();

}

void OverlayWindow::HideOverlay() {
  visible_ = false;
  StopTimer();
  if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::Close() {
  StopTimer();
  if (window_ != nullptr) {
    window_.Close();
    window_ = nullptr;
    hwnd_ = nullptr;
  }
}

void OverlayWindow::ApplyState() {
  if (window_ == nullptr || hwnd_ == nullptr) return;
  const auto& services = Services::Instance();
  const auto& presentation = services.config().presentation;
  if (presentation.view == classtime::PresentationSettings::View::Ring) {
    RefreshRing();
  } else {
    RefreshBar();
  }
}

void OverlayWindow::RefreshBar() {
  const auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();
  const auto& config = services.config();
  const auto now = NowClock();
  const Palette palette = CurrentPalette();

  if (snapshot.current.has_value()) {
    const auto& current = *snapshot.current;
    if (current.kind == classtime::SegmentKind::Focus) {
      stateText_.Text(L"第 " + std::to_wstring(current.focusOrdinal) + L" 节");
      progressText_.Text(FormatPercent(snapshot.progress));
      progressText_.Visibility(Visibility::Visible);
      ringLabel_.Text(L"第 " + std::to_wstring(current.focusOrdinal) + L" 节");
    } else {
      stateText_.Text(SegmentKindLabel(current.kind));
      progressText_.Text(FormatPercent(snapshot.progress));
      progressText_.Visibility(Visibility::Visible);
      ringLabel_.Text(SegmentKindLabel(current.kind));
    }
    stateText_.Foreground(SolidColorBrush(KindColor(palette, current.kind)));
  } else {
    stateText_.Text(L"空闲");
    progressText_.Visibility(Visibility::Collapsed);
    ringLabel_.Text(L"空闲");
  }

  if (snapshot.next.has_value()) {
    nextText_.Text((snapshot.next->kind == classtime::SegmentKind::Focus
                        ? L"下一节 "
                        : L"接下来 ") +
                   FormatClock(snapshot.next->start) + L" · " + ToWide(snapshot.next->label));
  } else if (snapshot.hasSchedule) {
    nextText_.Text(L"今天的安排已结束");
  } else {
    nextText_.Text(L"今天没有安排");
  }
  pauseText_.Visibility(config.notifications.muted ? Visibility::Visible : Visibility::Collapsed);

  // 按当前显示范围取时段；范围或作息变化时重建分节。
  const auto visible = services.VisibleSegments();
  std::string signature;
  for (const auto& segment : visible) {
    signature += segment.id + ":" + std::to_string(segment.DurationMinutes()) + ":" +
                 classtime::SegmentKindToString(segment.kind) + ";";
  }
  signature += ringMode_ ? "r" : "b";

  if (signature != visualSignature_) {
    visualSignature_ = signature;
    visuals_.clear();
    segmentPanel_.Children().Clear();

    if (visible.empty()) {
      auto empty = MakeText(12, WithAlpha(palette.foreground, 130));
      empty.Text(L"今天暂无安排");
      segmentPanel_.Children().Append(empty);
    } else {
      int totalMinutes = 0;
      for (const auto& segment : visible) totalMinutes += std::max(1, segment.DurationMinutes());
      const double gaps = 4.0 * static_cast<double>(visible.size() - 1);
      const double available = std::max(120.0, kSegmentAreaWidth - gaps);

      for (size_t i = 0; i < visible.size(); ++i) {
        const auto& segment = visible[i];
        SegmentVisual visual;
        visual.id = segment.id;
        visual.kind = segment.kind;

        double width = available * static_cast<double>(segment.DurationMinutes()) / totalMinutes;
        width = std::max(6.0, width);
        visual.width = width;

        visual.host = Grid();
        visual.host.Width(width);
        visual.host.Height(14);

        visual.track = Shapes::Rectangle();
        visual.track.RadiusX(4);
        visual.track.RadiusY(4);
        visual.track.Height(segment.kind == classtime::SegmentKind::Break ? 6 : 9);
        visual.track.Fill(SolidColorBrush(WithAlpha(palette.foreground, 26)));
        visual.track.VerticalAlignment(VerticalAlignment::Center);
        visual.track.HorizontalAlignment(HorizontalAlignment::Stretch);
        visual.host.Children().Append(visual.track);

        visual.fill = Shapes::Rectangle();
        visual.fill.RadiusX(4);
        visual.fill.RadiusY(4);
        visual.fill.Height(segment.kind == classtime::SegmentKind::Break ? 6 : 9);
        visual.fill.Fill(SolidColorBrush(KindColor(palette, segment.kind)));
        visual.fill.VerticalAlignment(VerticalAlignment::Center);
        visual.fill.HorizontalAlignment(HorizontalAlignment::Left);
        visual.fill.Width(0);
        visual.host.Children().Append(visual.fill);

        ToolTipService::SetToolTip(visual.host, box_value(L""));
        segmentPanel_.Children().Append(visual.host);
        visuals_.push_back(visual);
      }
    }
  }

  // 更新填充宽度与 hover 文案
  for (size_t i = 0; i < visuals_.size() && i < visible.size(); ++i) {
    const auto& segment = visible[i];
    double progress = 0.0;
    if (snapshot.current.has_value() && snapshot.current->id == segment.id) {
      progress = snapshot.progress;
    } else if (segment.end.TotalMinutes() * 60 <=
               now.hour * 3600 + now.minute * 60 + now.second) {
      progress = 1.0;
    }
    visuals_[i].fill.Width(visuals_[i].width * progress);

    std::wstring tip = ToWide(segment.label);
    if (segment.kind == classtime::SegmentKind::Focus) {
      int ordinal = 0;
      for (const auto& item : services.effectiveSegments()) {
        if (item.kind == classtime::SegmentKind::Focus) ++ordinal;
        if (item.id == segment.id) break;
      }
      tip = L"第 " + std::to_wstring(ordinal) + L" 节 · " + ToWide(segment.label);
    }
    tip += L"\n" + FormatRange(segment) + L"（" + FormatMinutes(segment.DurationMinutes()) + L"）";
    if (snapshot.current.has_value() && snapshot.current->id == segment.id) {
      tip += L"\n已过 " +
             std::to_wstring(std::max(
                 0, (now.hour * 3600 + now.minute * 60 + now.second) / 60 -
                        segment.start.TotalMinutes())) +
             L" 分钟 · 剩余 " + RemainingText(segment, now);
      tip += L"\n进度 " + FormatPercent(progress);
    }
    if (snapshot.next.has_value()) {
      tip += L"\n下一段 " + FormatClock(snapshot.next->start) + L" " + ToWide(snapshot.next->label);
    }
    ToolTipService::SetToolTip(visuals_[i].host, box_value(tip));
  }
}

void OverlayWindow::RefreshRing() {
  const auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();
  const Palette palette = CurrentPalette();

  const double progress = snapshot.current.has_value() ? snapshot.progress : 0.0;
  ringPercent_.Text(FormatPercent(progress));

  if (snapshot.current.has_value()) {
    const auto& current = *snapshot.current;
    ringLabel_.Text(current.kind == classtime::SegmentKind::Focus
                        ? L"第 " + std::to_wstring(current.focusOrdinal) + L" 节"
                        : SegmentKindLabel(current.kind));
  } else {
    ringLabel_.Text(snapshot.hasSchedule ? L"今日已结束" : L"今天没有安排");
  }

  if (snapshot.next.has_value()) {
    ringNext_.Text(L"接下来 " + FormatClock(snapshot.next->start) + L" · " +
                   ToWide(snapshot.next->label));
  } else {
    ringNext_.Text(L"");
  }

  const auto accent = snapshot.current.has_value()
                          ? KindColor(palette, snapshot.current->kind)
                          : palette.accent;
  ringArc_.Stroke(SolidColorBrush(accent));

  // 圆环直径 196，半径 92，圆心（110,110）。
  constexpr double kCenter = 110.0;
  constexpr double kRadius = 92.0;
  if (progress >= 0.999) {
    EllipseGeometry geometry;
    geometry.Center(Windows::Foundation::Point(static_cast<float>(kCenter),
                                               static_cast<float>(kCenter)));
    geometry.RadiusX(static_cast<float>(kRadius));
    geometry.RadiusY(static_cast<float>(kRadius));
    ringFull_.Data(geometry);
    ringFull_.Stroke(SolidColorBrush(accent));
    ringFull_.Visibility(Visibility::Visible);
    ringArc_.Visibility(Visibility::Collapsed);
  } else {
    ringFull_.Visibility(Visibility::Collapsed);
    ringArc_.Visibility(Visibility::Visible);
    if (progress <= 0.0005) {
      ringArc_.Data(PathGeometry());
    } else {
      const double sweep = progress * 2.0 * 3.14159265358979323846;
      const double endX = kCenter + kRadius * std::sin(sweep);
      const double endY = kCenter - kRadius * std::cos(sweep);

      PathGeometry geometry;
      PathFigure figure;
      figure.StartPoint(Windows::Foundation::Point(static_cast<float>(kCenter),
                                                   static_cast<float>(kCenter - kRadius)));
      ArcSegment arc;
      arc.Point(Windows::Foundation::Point(static_cast<float>(endX), static_cast<float>(endY)));
      arc.Size(Windows::Foundation::Size(static_cast<float>(kRadius),
                                         static_cast<float>(kRadius)));
      arc.SweepDirection(SweepDirection::Clockwise);
      arc.IsLargeArc(progress > 0.5);
      figure.Segments().Append(arc);
      geometry.Figures().Append(figure);
      ringArc_.Data(geometry);
    }
  }

  // 全天范围：用一排小圆点表示各节课的完成情况（不展开成多个主环）。
  ringDots_.Children().Clear();
  if (allDay_) {
    const int nowSeconds = NowClock().hour * 3600 + NowClock().minute * 60 + NowClock().second;
    for (const auto& segment : services.effectiveSegments()) {
      if (segment.kind != classtime::SegmentKind::Focus) continue;
      Shapes::Ellipse dot;
      dot.Width(6);
      dot.Height(6);
      const bool current =
          snapshot.current.has_value() && snapshot.current->id == segment.id;
      const bool done = segment.end.TotalMinutes() * 60 <= nowSeconds;
      dot.Fill(SolidColorBrush(current ? accent
                                       : (done ? WithAlpha(palette.foreground, 120)
                                               : WithAlpha(palette.foreground, 45))));
      ringDots_.Children().Append(dot);
    }
  }

  ringRoot_.Visibility(Visibility::Visible);
  barRoot_.Visibility(Visibility::Collapsed);
}

}  // namespace ClassTimeApp
