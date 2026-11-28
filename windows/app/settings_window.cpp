#include "pch.h"
#include "settings_window.h"

#include "notification_service.h"
#include "resource.h"
#include "services.h"
#include "ui_motion.h"
#include "widget_appearance.h"
#include "widget_state.h"

#include <initializer_list>
#include <cwctype>
#include <limits>

#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
using namespace Windows::Foundation;

namespace ClassTimeApp {
namespace {

constexpr wchar_t kVersion[] = L"0.1.2";
using Presentation = classtime::PresentationSettings;
constexpr double kPreviewWidth = 420;
constexpr double kPreviewHeight = 172;
using ClockStyle = classtime::NotificationSettings::AlertClockStyle;
constexpr const wchar_t* kClockNames[] = {L"翻页数字", L"机械表盘", L"电子面板", L"罗马表盘"};
constexpr int kClockScales[] = {75, 100, 125};
constexpr int kClockHolds[] = {2, 4, 8};

struct MonitorChoice {
  std::wstring device;
  std::wstring label;
  bool primary = false;
  RECT bounds{};
};

std::vector<MonitorChoice> EnumerateMonitors() {
  std::vector<MonitorChoice> monitors;
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
    auto& result = *reinterpret_cast<std::vector<MonitorChoice>*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;

    MonitorChoice choice;
    choice.device = info.szDevice;
    choice.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    choice.bounds = info.rcMonitor;
    const auto slash = choice.device.find_last_of(L'\\');
    const auto shortName = slash == std::wstring::npos ? choice.device : choice.device.substr(slash + 1);
    const auto width = info.rcMonitor.right - info.rcMonitor.left;
    const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
    choice.label = L"显示器 " + shortName;
    if (choice.primary) choice.label += L"（系统主屏）";
    choice.label += L" · " + std::to_wstring(width) + L" × " + std::to_wstring(height);
    result.push_back(std::move(choice));
    return TRUE;
  }, reinterpret_cast<LPARAM>(&monitors));
  std::stable_sort(monitors.begin(), monitors.end(), [](const MonitorChoice& a, const MonitorChoice& b) {
    return a.primary != b.primary ? a.primary : a.device < b.device;
  });
  const auto primary = std::find_if(monitors.begin(), monitors.end(),
                                    [](const MonitorChoice& monitor) { return monitor.primary; });
  if (primary != monitors.end()) {
    for (auto& monitor : monitors) {
      if (monitor.primary) continue;
      if (monitor.bounds.right <= primary->bounds.left) monitor.label += L" · 主屏左侧";
      else if (monitor.bounds.left >= primary->bounds.right) monitor.label += L" · 主屏右侧";
      else if (monitor.bounds.bottom <= primary->bounds.top) monitor.label += L" · 主屏上方";
      else if (monitor.bounds.top >= primary->bounds.bottom) monitor.label += L" · 主屏下方";
    }
  }
  return monitors;
}

Style ResourceStyle(const wchar_t* key) {
  try {
    return Application::Current().Resources().Lookup(box_value(key)).try_as<Style>();
  } catch (...) {
    return nullptr;
  }
}

Brush ResourceBrush(const wchar_t* key, Windows::UI::Color fallback) {
  try {
    if (auto brush = Application::Current().Resources().Lookup(box_value(key)).try_as<Brush>()) {
      return brush;
    }
  } catch (...) {
  }
  return SolidColorBrush(fallback);
}

Windows::UI::Color SecondaryColor() {
  return Windows::UI::ColorHelper::FromArgb(255, 110, 112, 120);
}

// 开启“自定义背景色”时，取当前系统背景色作为取色器起点。
Windows::UI::Color SystemBackgroundColor() {
  try {
    Windows::UI::ViewManagement::UISettings settings;
    return settings.GetColorValue(Windows::UI::ViewManagement::UIColorType::Background);
  } catch (...) {
    return Windows::UI::ColorHelper::FromArgb(255, 250, 250, 252);
  }
}

int SnapToTimelineStep(int minute, int step) {
  return static_cast<int>(std::llround(minute / static_cast<double>(step))) * step;
}

Windows::UI::Color SegmentColor(classtime::SegmentKind kind) {
  const auto palette = WidgetAppearance::ResolvePalette(Services::Instance().config().presentation);
  return WidgetAppearance::KindColor(palette, kind);
}

TextBlock MakeText(const std::wstring& text, double size, Brush brush = nullptr) {
  TextBlock block;
  block.Text(hstring(text));
  block.FontSize(size);
  block.TextWrapping(TextWrapping::Wrap);
  if (brush != nullptr) block.Foreground(brush);
  return block;
}

TextBlock MakeSectionTitle(const std::wstring& text) {
  auto block = MakeText(text, 30);
  if (auto style = ResourceStyle(L"TitleTextBlockStyle")) block.Style(style);
  return block;
}

TextBlock MakeSubtitle(const std::wstring& text) {
  auto block = MakeText(text, 13, ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
  return block;
}

Border MakeCard() {
  Border card;
  card.CornerRadius(CornerRadius{12, 12, 12, 12});
  card.Padding(Thickness{18, 18, 18, 18});
  card.Background(ResourceBrush(L"CardBackgroundFillColorDefaultBrush",
                               Windows::UI::ColorHelper::FromArgb(255, 250, 250, 252)));
  card.BorderBrush(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0)));
  card.BorderThickness(Thickness{1, 1, 1, 1});
  return card;
}

TextBlock MakeCardTitle(const std::wstring& text) {
  auto block = MakeText(text, 16);
  block.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  return block;
}

Border MakeDivider() {
  Border line;
  line.Height(1);
  line.Background(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0)));
  return line;
}

// A setting reads as one row: its name and explanation stay beside the native switch.
Grid MakeToggleRow(ToggleSwitch const& toggle, const std::wstring& title,
                   const std::wstring& description = L"", const wchar_t* glyph = nullptr) {
  Grid row;
  row.ColumnSpacing(14);
  if (glyph) {
    ColumnDefinition iconColumn;
    iconColumn.Width(GridLengthHelper::FromValueAndType(28, GridUnitType::Pixel));
    row.ColumnDefinitions().Append(iconColumn);
    FontIcon icon;
    icon.Glyph(glyph);
    icon.FontSize(20);
    icon.Foreground(ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
    icon.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(icon);
  }
  ColumnDefinition textColumn;
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ColumnDefinition actionColumn;
  actionColumn.Width(GridLengthHelper::Auto());
  row.ColumnDefinitions().Append(textColumn);
  row.ColumnDefinitions().Append(actionColumn);
  row.MinHeight(42);
  StackPanel copy;
  copy.Spacing(3);
  copy.VerticalAlignment(VerticalAlignment::Center);
  auto label = MakeText(title, 14);
  label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  copy.Children().Append(label);
  if (!description.empty()) copy.Children().Append(MakeSubtitle(description));
  Grid::SetColumn(copy, glyph ? 1 : 0);
  row.Children().Append(copy);
  toggle.Header(nullptr);
  toggle.OnContent(box_value(L""));
  toggle.OffContent(box_value(L""));
  toggle.MinWidth(0);
  toggle.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetName(toggle, hstring(title));
  Grid::SetColumn(toggle, glyph ? 2 : 1);
  row.Children().Append(toggle);
  return row;
}

void StyleAction(Button const& button, bool primary = false, bool quiet = false) {
  if (auto style = ResourceStyle(primary ? L"AccentButtonStyle" :
                                 quiet ? L"TextButtonStyle" : L"DefaultButtonStyle")) {
    button.Style(style);
  }
  button.MinHeight(34);
  button.Padding(Thickness{12, 5, 12, 5});
  button.CornerRadius(CornerRadius{6, 6, 6, 6});
}

Windows::UI::Color AccentColor() {
  try {
    return unbox_value<Windows::UI::Color>(
        Application::Current().Resources().Lookup(box_value(L"SystemAccentColor")));
  } catch (...) {
    return Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237);
  }
}

// 顶栏状态徽标：accent 为真时用系统强调色软底，否则中性灰底。
Border MakeChip(const std::wstring& text, bool accent) {
  Border chip;
  chip.CornerRadius(CornerRadius{10, 10, 10, 10});
  chip.Padding(Thickness{10, 3, 10, 3});
  if (accent) {
    auto fill = AccentColor();
    fill.A = 30;
    chip.Background(SolidColorBrush(fill));
    chip.BorderBrush(SolidColorBrush(AccentColor()));
    chip.BorderThickness(Thickness{1, 1, 1, 1});
    auto label = MakeText(text, 12, ResourceBrush(L"AccentTextFillColorPrimaryBrush",
                                                 Windows::UI::ColorHelper::FromArgb(255, 58, 88, 194)));
    chip.Child(label);
  } else {
    chip.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(14, 0, 0, 0)));
    chip.Child(MakeText(text, 12, ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor())));
  }
  return chip;
}

// 窄窗口把辅助卡片放到下一行，保留预览、检查和版本入口。
Grid MakeAdaptiveColumns(FrameworkElement const& primary, FrameworkElement const& secondary,
                         double secondaryWidth, double twoColumnWidth) {
  constexpr double spacing = 12;
  Grid host;
  ColumnDefinition mainColumn;
  mainColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ColumnDefinition sideColumn;
  const auto sideLength = GridLengthHelper::FromValueAndType(secondaryWidth, GridUnitType::Pixel);
  sideColumn.Width(sideLength);
  host.ColumnDefinitions().Append(mainColumn);
  host.ColumnDefinitions().Append(sideColumn);
  for (int i = 0; i < 2; ++i) {
    RowDefinition row;
    row.Height(GridLengthHelper::Auto());
    host.RowDefinitions().Append(row);
  }
  host.ColumnSpacing(spacing);
  secondary.VerticalAlignment(VerticalAlignment::Top);
  Grid::SetColumn(secondary, 1);
  host.Children().Append(primary);
  host.Children().Append(secondary);

  host.SizeChanged([secondary, sideColumn, sideLength, twoColumnWidth, spacing](
                       IInspectable const& sender, SizeChangedEventArgs const& args) {
    const bool stacked = args.NewSize().Width < twoColumnWidth;
    const int row = stacked ? 1 : 0;
    if (Grid::GetRow(secondary) == row) return;
    const auto grid = sender.as<Grid>();
    Grid::SetRow(secondary, row);
    Grid::SetColumn(secondary, stacked ? 0 : 1);
    sideColumn.Width(stacked ? GridLengthHelper::FromValueAndType(0, GridUnitType::Pixel)
                             : sideLength);
    grid.ColumnSpacing(stacked ? 0 : spacing);
    grid.RowSpacing(stacked ? spacing : 0);
  });
  return host;
}

void BindActionRow(StackPanel const& row, double horizontalWidth) {
  row.Orientation(Orientation::Horizontal);
  row.SizeChanged([horizontalWidth](IInspectable const& sender, SizeChangedEventArgs const& args) {
    sender.as<StackPanel>().Orientation(args.NewSize().Width < horizontalWidth
                                          ? Orientation::Vertical
                                          : Orientation::Horizontal);
  });
}

std::wstring TextOf(TextBox const& box) { return std::wstring(box.Text().c_str()); }

std::wstring TrimTemplateName(const std::wstring& name) {
  const auto first = std::find_if_not(name.begin(), name.end(),
                                      [](wchar_t c) { return std::iswspace(c) != 0; });
  const auto last = std::find_if_not(name.rbegin(), name.rend(),
                                     [](wchar_t c) { return std::iswspace(c) != 0; }).base();
  return first < last ? std::wstring(first, last) : std::wstring{};
}

void SetText(TextBox const& box, const std::wstring& text) {
  if (TextOf(box) != text) box.Text(hstring(text));
}

std::optional<classtime::TimeOfDay> ParseBox(TextBox const& box) {
  return classtime::TimeOfDay::Parse(ToUtf8(TextOf(box)));
}

std::optional<int> ParseDuration(TextBox const& box) {
  const auto text = TextOf(box);
  if (text.empty() || text.size() > 4) return std::nullopt;
  int value = 0;
  for (const auto digit : text) {
    if (digit < L'0' || digit > L'9') return std::nullopt;
    value = value * 10 + digit - L'0';
  }
  if (value < 1 || value > 1439) return std::nullopt;
  return value;
}

int KindToIndex(classtime::SegmentKind kind) {
  switch (kind) {
    case classtime::SegmentKind::Focus: return 0;
    case classtime::SegmentKind::Break: return 1;
    case classtime::SegmentKind::LongBreak: return 2;
    case classtime::SegmentKind::Lunch: return 3;
  }
  return 0;
}

classtime::SegmentKind IndexToKind(int index) {
  switch (index) {
    case 1: return classtime::SegmentKind::Break;
    case 2: return classtime::SegmentKind::LongBreak;
    case 3: return classtime::SegmentKind::Lunch;
    default: return classtime::SegmentKind::Focus;
  }
}

std::wstring SegmentName(const std::vector<classtime::Segment>& segments, const std::string& id) {
  for (const auto& segment : segments) {
    if (segment.id == id) return ToWide(segment.label);
  }
  return ToWide(id);
}

void Add(StackPanel const& panel, UIElement const& element) { panel.Children().Append(element); }

Border MakeSegments(Grid& grid, std::initializer_list<const wchar_t*> labels,
                    std::function<void(int)> select) {
  grid = Grid();
  grid.ColumnSpacing(2);
  int index = 0;
  for (const auto* label : labels) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(column);
    Primitives::ToggleButton button;
    button.Content(box_value(hstring(label)));
    button.HorizontalAlignment(HorizontalAlignment::Stretch);
    button.HorizontalContentAlignment(HorizontalAlignment::Center);
    button.MinHeight(36);
    button.Padding(Thickness{6, 5, 6, 5});
    button.CornerRadius(CornerRadius{6, 6, 6, 6});
    button.BorderThickness(Thickness{0, 0, 0, 0});
    button.Click([select, index](auto const&, auto const&) { select(index); });
    Grid::SetColumn(button, index++);
    grid.Children().Append(button);
  }
  auto track = MakeCard();
  track.Padding(Thickness{3, 3, 3, 3});
  track.BorderThickness(Thickness{0, 0, 0, 0});
  track.Background(ResourceBrush(L"ControlFillColorSecondaryBrush",
                                 Windows::UI::ColorHelper::FromArgb(12, 0, 0, 0)));
  track.Child(grid);
  return track;
}

// 三种显示方式用原生 ToggleButton 和简洁示意图表达，仍保留键盘和屏幕阅读器操作。
Grid MakeViewChoices(Grid& grid, std::function<void(int)> select) {
  grid = Grid();
  grid.ColumnSpacing(10);
  const wchar_t* labels[] = {L"分节长条", L"圆环", L"屏幕边框"};
  for (int index = 0; index < 3; ++index) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(column);
    RowDefinition row;
    row.Height(GridLengthHelper::Auto());
    grid.RowDefinitions().Append(row);

    Primitives::ToggleButton choice;
    choice.Tag(box_value(L"view-choice"));
    choice.HorizontalAlignment(HorizontalAlignment::Stretch);
    choice.HorizontalContentAlignment(HorizontalAlignment::Center);
    choice.Padding(Thickness{4, 10, 4, 10});
    choice.CornerRadius(CornerRadius{10, 10, 10, 10});
    Automation::AutomationProperties::SetName(choice, labels[index]);

    StackPanel body;
    body.Spacing(7);
    body.HorizontalAlignment(HorizontalAlignment::Center);
    Canvas preview;
    preview.Width(100);
    preview.Height(34);
    preview.HorizontalAlignment(HorizontalAlignment::Center);
    const auto accent = SolidColorBrush(AccentColor());
    if (index == 0) {
      for (int part = 0; part < 5; ++part) {
        Shapes::Rectangle bar;
        bar.Width(15);
        bar.Height(7);
        bar.RadiusX(3.5);
        bar.RadiusY(3.5);
        auto color = AccentColor();
        color.A = part < 3 ? 255 : 70;
        bar.Fill(SolidColorBrush(color));
        Canvas::SetLeft(bar, 5 + 19 * part);
        Canvas::SetTop(bar, 14);
        preview.Children().Append(bar);
      }
    } else if (index == 1) {
      Shapes::Ellipse ring;
      ring.Width(30);
      ring.Height(30);
      ring.Stroke(accent);
      ring.StrokeThickness(5);
      Canvas::SetLeft(ring, 35);
      Canvas::SetTop(ring, 2);
      preview.Children().Append(ring);
    } else {
      Shapes::Rectangle frame;
      frame.Width(66);
      frame.Height(28);
      frame.RadiusX(4);
      frame.RadiusY(4);
      frame.Stroke(accent);
      frame.StrokeThickness(3);
      Canvas::SetLeft(frame, 17);
      Canvas::SetTop(frame, 3);
      preview.Children().Append(frame);
    }
    body.Children().Append(preview);
    auto label = MakeText(labels[index], 14);
    label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    label.HorizontalAlignment(HorizontalAlignment::Center);
    body.Children().Append(label);
    choice.Content(body);
    choice.Click([select, index](auto const&, auto const&) { select(index); });
    Grid::SetColumn(choice, index);
    grid.Children().Append(choice);
  }
  grid.SizeChanged([](IInspectable const& sender, SizeChangedEventArgs const& args) {
    const auto host = sender.as<Grid>();
    const bool stacked = args.NewSize().Width < 360;
    const auto last = host.Children().GetAt(2).as<FrameworkElement>();
    if (Grid::GetRow(last) == (stacked ? 2 : 0)) return;
    for (int index = 0; index < 3; ++index) {
      host.ColumnDefinitions().GetAt(index).Width(
          GridLengthHelper::FromValueAndType(stacked && index > 0 ? 0 : 1,
              stacked && index > 0 ? GridUnitType::Pixel : GridUnitType::Star));
      const auto choice = host.Children().GetAt(index).as<FrameworkElement>();
      Grid::SetColumn(choice, stacked ? 0 : index);
      Grid::SetRow(choice, stacked ? index : 0);
    }
    host.ColumnSpacing(stacked ? 0 : 10);
    host.RowSpacing(stacked ? 8 : 0);
  });
  return grid;
}

void SelectSegment(const Grid& grid, int selected) {
  int index = 0;
  for (const auto& child : grid.Children()) {
    const auto button = child.as<Primitives::ToggleButton>();
    const bool active = index++ == selected;
    button.IsChecked(active);
    if (button.Tag() && unbox_value_or<hstring>(button.Tag(), L"") == L"view-choice") {
      auto color = AccentColor();
      color.A = 28;
      button.Background(active ? SolidColorBrush(color)
                               : ResourceBrush(L"ControlFillColorDefaultBrush",
                                               Windows::UI::ColorHelper::FromArgb(12, 128, 128, 128)));
      button.BorderBrush(active ? SolidColorBrush(AccentColor())
                                : ResourceBrush(L"ControlStrokeColorDefaultBrush",
                                                Windows::UI::ColorHelper::FromArgb(64, 128, 128, 128)));
      button.BorderThickness(Thickness{1, 1, 1, 1});
      const auto iconBrush = active
          ? ResourceBrush(L"TextOnAccentFillColorPrimaryBrush",
                          Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
          : Brush(SolidColorBrush(AccentColor()));
      const auto body = button.Content().as<StackPanel>();
      const auto preview = body.Children().GetAt(0).as<Canvas>();
      int part = 0;
      for (const auto& element : preview.Children()) {
        const auto shape = element.as<Shapes::Shape>();
        if (index == 1) {
          shape.Fill(iconBrush);
          shape.Opacity(part++ < 3 ? 1.0 : 0.4);
        } else {
          shape.Stroke(iconBrush);
        }
      }
    }
  }
}

// —— 显示预览：模拟屏幕是静态背景，只绘制一次 ——
constexpr double kPreviewScreenLeft = 42;
constexpr double kPreviewScreenTop = 5;
constexpr double kPreviewScreenWidth = kPreviewWidth - 2 * kPreviewScreenLeft;
constexpr double kPreviewScreenHeight = 143;
constexpr double kPreviewTaskbarHeight = 12;
constexpr double kPreviewScreenCenterX = kPreviewWidth / 2;

Windows::UI::Color PreviewColor(uint8_t r, uint8_t g, uint8_t b) {
  return Windows::UI::ColorHelper::FromArgb(255, r, g, b);
}

void DrawPreviewChrome(const Canvas& canvas) {
  const auto rect = [&](double x, double y, double width, double height,
                        Windows::UI::Color color, double radius = 0) {
    Shapes::Rectangle shape;
    shape.Width(width);
    shape.Height(height);
    shape.RadiusX(radius);
    shape.RadiusY(radius);
    shape.Fill(SolidColorBrush(color));
    Canvas::SetLeft(shape, x);
    Canvas::SetTop(shape, y);
    canvas.Children().Append(shape);
  };
  rect(kPreviewScreenLeft - 5, kPreviewScreenTop - 4, kPreviewScreenWidth + 10,
       kPreviewScreenHeight + 21, PreviewColor(24, 35, 50), 9);
  rect(kPreviewScreenLeft, kPreviewScreenTop, kPreviewScreenWidth, kPreviewScreenHeight,
       PreviewColor(35, 48, 65));
  rect(kPreviewScreenLeft, kPreviewScreenTop + kPreviewScreenHeight, kPreviewScreenWidth, 12,
       PreviewColor(57, 73, 94));
  for (double x : {kPreviewScreenCenterX - 15, kPreviewScreenCenterX - 4,
                   kPreviewScreenCenterX + 7}) {
    rect(x, kPreviewScreenTop + kPreviewScreenHeight + 3, 7, 6, PreviewColor(147, 167, 190), 1);
  }
}

bool DrawScheduleScreenEdge(const Canvas& canvas, double left, double top, double width,
                            double height, double line, const std::vector<classtime::Segment>& plan,
                            const classtime::WallClock& now, const WidgetAppearance::Palette& palette,
                            bool gradient) {
  const auto runs = BuildScreenEdgeRuns(plan, now);
  if (runs.empty()) return false;
  const std::array<double, 4> lengths = {width, height - line, width - line, height - 2 * line};
  const double perimeter = std::accumulate(lengths.begin(), lengths.end(), 0.0);
  if (!gradient) {
    for (const auto& run : ProjectScreenEdgeRuns(runs, lengths)) {
      const double span = run.end - run.begin;
      if (span <= 0) continue;
      Shapes::Rectangle shape;
      shape.Fill(SolidColorBrush(WidgetAppearance::ScreenEdgeRunColor(palette, run.kind, run.elapsed)));
      double x = left, y = top;
      if (run.side == 0) {
        x += run.begin;
        shape.Width(span);
        shape.Height(line);
      } else if (run.side == 1) {
        x += width - line;
        y += line + run.begin;
        shape.Width(line);
        shape.Height(span);
      } else if (run.side == 2) {
        x += width - line - run.end;
        y += height - line;
        shape.Width(span);
        shape.Height(line);
      } else {
        y += height - line - run.end;
        shape.Width(line);
        shape.Height(span);
      }
      Canvas::SetLeft(shape, x);
      Canvas::SetTop(shape, y);
      canvas.Children().Append(shape);
    }
    return true;
  }
  const auto stops = WidgetAppearance::ScreenEdgeColorStops(runs, palette);
  double sideStart = 0;
  for (size_t side = 0; side < lengths.size(); ++side) {
    const double begin = sideStart / perimeter;
    const double end = (sideStart + lengths[side]) / perimeter;
    LinearGradientBrush brush;
    brush.ColorInterpolationMode(ColorInterpolationMode::SRgbLinearInterpolation);
    const auto addStop = [&](double offset, Windows::UI::Color color) {
      GradientStop stop;
      stop.Offset(offset);
      stop.Color(color);
      brush.GradientStops().Append(stop);
    };
    addStop(0, WidgetAppearance::ScreenEdgeColorAt(stops, begin));
    for (const auto& stop : stops) {
      if (stop.position > begin && stop.position < end) {
        addStop((stop.position - begin) / (end - begin), stop.color);
      }
    }
    addStop(1, WidgetAppearance::ScreenEdgeColorAt(stops, end));

    Shapes::Rectangle shape;
    shape.Fill(brush);
    double x = left, y = top;
    if (side == 0) {
      brush.StartPoint({0, 0});
      brush.EndPoint({1, 0});
      shape.Width(width);
      shape.Height(line);
    } else if (side == 1) {
      x += width - line;
      y += line;
      brush.StartPoint({0, 0});
      brush.EndPoint({0, 1});
      shape.Width(line);
      shape.Height(height - line);
    } else if (side == 2) {
      y += height - line;
      brush.StartPoint({1, 0});
      brush.EndPoint({0, 0});
      shape.Width(width - line);
      shape.Height(line);
    } else {
      y += line;
      brush.StartPoint({0, 1});
      brush.EndPoint({0, 0});
      shape.Width(line);
      shape.Height(height - 2 * line);
    }
    Canvas::SetLeft(shape, x);
    Canvas::SetTop(shape, y);
    canvas.Children().Append(shape);
    sideStart += lengths[side];
  }
  return true;
}

void DrawScreenEdgeHighlight(const Canvas& canvas, double left, double top, double width,
                             double height, double line, double progress,
                             Storyboard const& pulseStoryboard = nullptr) {
  if (progress <= 0.0001 || progress >= 0.9999) return;
  const std::array<double, 4> lengths = {width, height - line, width - line, height - 2 * line};
  const double perimeter = std::accumulate(lengths.begin(), lengths.end(), 0.0);
  if (perimeter <= 0) return;
  const double head = progress * perimeter;
  const auto spec = BuildScreenEdgeHighlightSpec(perimeter, line);
  const double hlBegin = std::max(0.0, head - spec.tailLength);
  const double hlEnd = std::min(perimeter, head + spec.leadLength);
  if (hlEnd <= hlBegin) return;

  const auto placeRect = [&](size_t side, double localBegin, double localEnd,
                             double crossOffset, double crossSize, const Brush& brush) {
    const double span = localEnd - localBegin;
    if (span <= 0 || crossSize <= 0) return Shapes::Rectangle{nullptr};
    Shapes::Rectangle shape;
    shape.Fill(brush);
    double x = left, y = top;
    if (side == 0) {
      x += localBegin;
      y += crossOffset;
      shape.Width(span);
      shape.Height(crossSize);
    } else if (side == 1) {
      x += width - line + crossOffset;
      y += line + localBegin;
      shape.Width(crossSize);
      shape.Height(span);
    } else if (side == 2) {
      x += width - line - localEnd;
      y += height - line + crossOffset;
      shape.Width(span);
      shape.Height(crossSize);
    } else {
      x += crossOffset;
      y += height - line - localEnd;
      shape.Width(crossSize);
      shape.Height(span);
    }
    Canvas::SetLeft(shape, x);
    Canvas::SetTop(shape, y);
    canvas.Children().Append(shape);
    return shape;
  };

  const double inset = std::max(0.75, line * 0.22);
  const double coreLine = std::max(1.5, line - 2.0 * inset);
  const double coreOffset = (line - coreLine) * 0.5;
  const auto lipBrush = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(165, 18, 24, 38));
  const bool animate = pulseStoryboard != nullptr && Motion::Enabled();
  SineEase pulseEase;
  pulseEase.EasingMode(EasingMode::EaseInOut);

  double sideStart = 0;
  for (size_t side = 0; side < lengths.size(); ++side) {
    const double sideEnd = sideStart + lengths[side];

    // 1) 前沿未到侧的窄暗对比边，确保浅色背景下也能精确定位进度终点。
    const double lipBegin = std::max(sideStart, head);
    const double lipEnd = std::min(sideEnd, hlEnd);
    if (lipEnd > lipBegin) {
      placeRect(side, lipBegin - sideStart, lipEnd - sideStart, 0, line, lipBrush);
    }

    // 2) 居中内缩的白光条芯：保留彩色外缘不呈“断层”，并按 500ms AutoReverse 呼吸闪烁。
    const double segBegin = std::max(sideStart, hlBegin);
    const double segEnd = std::min(sideEnd, head);
    if (segEnd > segBegin) {
      const double localBegin = segBegin - sideStart;
      const double localEnd = segEnd - sideStart;
      const double span = segEnd - segBegin;

      std::vector<double> samples = {segBegin, segEnd};
      for (int k = 1; k < 8; ++k) samples.push_back(segBegin + span * k / 8.0);
      for (double key : {head - spec.headSpan, head - std::max(2.5, line * 0.9)}) {
        if (key > segBegin && key < segEnd) samples.push_back(key);
      }
      std::sort(samples.begin(), samples.end());

      LinearGradientBrush brush;
      brush.ColorInterpolationMode(ColorInterpolationMode::SRgbLinearInterpolation);
      for (double pos : samples) {
        GradientStop stop;
        stop.Offset((pos - segBegin) / span);
        const auto sample = SampleScreenEdgeHighlight(pos, head, perimeter, line, 0.65);
        const double core = pos >= head - std::max(2.5, line * 0.9)
                                ? std::max(sample.core, 0.92)
                                : sample.core;
        stop.Color(WidgetAppearance::ScreenEdgeHighlightColor(core));
        brush.GradientStops().Append(stop);
      }
      if (side == 0) {
        brush.StartPoint({0, 0});
        brush.EndPoint({1, 0});
      } else if (side == 1) {
        brush.StartPoint({0, 0});
        brush.EndPoint({0, 1});
      } else if (side == 2) {
        brush.StartPoint({1, 0});
        brush.EndPoint({0, 0});
      } else {
        brush.StartPoint({0, 1});
        brush.EndPoint({0, 0});
      }
      if (auto coreShape = placeRect(side, localBegin, localEnd, coreOffset, coreLine, brush)) {
        if (animate) {
          DoubleAnimation pulse;
          pulse.From(0.30);
          pulse.To(1.0);
          pulse.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(500)));
          pulse.AutoReverse(true);
          pulse.RepeatBehavior(RepeatBehaviorHelper::Forever());
          pulse.EasingFunction(pulseEase);
          Storyboard::SetTarget(pulse, coreShape);
          Storyboard::SetTargetProperty(pulse, L"Opacity");
          pulseStoryboard.Children().Append(pulse);
        }
      }
    }
    sideStart = sideEnd;
  }
}

void DrawPreviewDetail(const Canvas& canvas, const Presentation& presentation,
                       const WidgetState& state, const std::vector<classtime::Segment>& plan,
                       const classtime::WallClock& now, const WidgetAppearance::Palette& palette,
                       bool expanded, int thickness,
                       Storyboard const& pulseStoryboard = nullptr) {
  canvas.Children().Clear();
  const auto place = [&](const UIElement& item, double x, double y) {
    Canvas::SetLeft(item, x);
    Canvas::SetTop(item, y);
    canvas.Children().Append(item);
  };
  const auto rect = [&](double x, double y, double width, double height,
                        const Brush& brush, double radius = 0) {
    Shapes::Rectangle shape;
    shape.Width(std::max(0.0, width));
    shape.Height(std::max(0.0, height));
    shape.RadiusX(std::min({radius, width / 2, height / 2}));
    shape.RadiusY(std::min({radius, width / 2, height / 2}));
    shape.Fill(brush);
    place(shape, x, y);
  };
  const auto text = [&](const std::wstring& value, double size, double x, double y, double width,
                        Windows::UI::Color color) {
    auto label = MakeText(value, size, SolidColorBrush(color));
    label.Width(width);
    label.TextAlignment(TextAlignment::Center);
    place(label, x, y);
  };
  if (!presentation.visible) {
    text(L"悬浮窗已隐藏", 14, 0, 58, 420, palette.foreground);
    return;
  }
  const bool side = presentation.edge == Presentation::Edge::Left ||
                    presentation.edge == Presentation::Edge::Right;
  const auto currentColor = state.snapshot.current
      ? WidgetAppearance::KindColor(palette, state.snapshot.current->kind) : palette.accent;
  const auto accent = SolidColorBrush(currentColor);
  const auto surface = WidgetAppearance::Surface(palette.background);
  if (presentation.view != Presentation::View::ScreenEdge &&
      presentation.autoHideAtEdge && !expanded) {
    Border grip;
    grip.Width(side ? 12 : 84);
    grip.Height(side ? 84 : 12);
    grip.Background(surface);
    grip.CornerRadius(CornerRadius{6, 6, 6, 6});
    place(grip, side ? 204 : 168, side ? 28 : 64);
    const double progress = state.snapshot.current ? state.snapshot.progress : 0;
    const auto track = SolidColorBrush(WidgetAppearance::WithAlpha(palette.foreground, 24));
    if (side) {
      rect(208, 38, 4, 64, track, 2);
      rect(208, 38, 4, 64 * progress, accent, 2);
    } else {
      rect(178, 68, 64, 4, track, 2);
      rect(178, 68, 64 * progress, 4, accent, 2);
    }
    return;
  }
  if (presentation.view == Presentation::View::ScreenEdge) {
    constexpr double left = 50, top = 14, width = 320, height = 112;
    const double line = std::clamp(thickness * 0.5, 2.0, 10.0);
    if (presentation.scope == Presentation::Scope::AllDay &&
        presentation.screenEdgeColors == Presentation::ScreenEdgeColors::Schedule &&
        DrawScheduleScreenEdge(canvas, left, top, width, height, line, plan, now, palette,
                               presentation.screenEdgeGradient)) {
      if (presentation.screenEdgeHighlight) {
        DrawScreenEdgeHighlight(canvas, left, top, width, height, line, state.edgeProgress,
                                pulseStoryboard);
      }
      return;
    }
    const auto track = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(172, 100, 117, 137));
    rect(left, top, width, line, track);
    rect(left + width - line, top + line, line, height - line, track);
    rect(left, top + height - line, width - line, line, track);
    rect(left, top + line, line, height - 2 * line, track);
    double remaining = state.edgeProgress * 2 * (width + height - 2 * line);
    if (remaining > 0) rect(left, top, std::min(width, remaining), line, accent);
    remaining -= width;
    if (remaining > 0) rect(left + width - line, top + line, line,
                            std::min(height - line, remaining), accent);
    remaining -= height - line;
    if (remaining > 0) {
      const double length = std::min(width - line, remaining);
      rect(left + width - line - length, top + height - line, length, line, accent);
    }
    remaining -= width - line;
    if (remaining > 0) {
      const double length = std::min(height - 2 * line, remaining);
      rect(left, top + height - line - length, line, length, accent);
    }
    if (presentation.screenEdgeHighlight && !plan.empty()) {
      DrawScreenEdgeHighlight(canvas, left, top, width, height, line, state.edgeProgress,
                              pulseStoryboard);
    }
    return;
  }
  if (presentation.view == Presentation::View::Bar) {
    const double fraction = std::clamp((presentation.EffectiveBarLength() - 240) / 1360.0, 0.0, 1.0);
    const double length = side ? 52 + 78 * fraction : 130 + 270 * fraction;
    const double width = side ? 34 : length;
    const double height = side ? length : 32;
    const double x = (420 - width) / 2, y = (140 - height) / 2;
    auto background = palette.background;
    auto foreground = palette.foreground;
    if (!presentation.barBackground.empty()) {
      background = WidgetAppearance::ParseHexColor(presentation.barBackground);
      const bool dark = 299 * background.R + 587 * background.G + 114 * background.B < 128000;
      foreground = dark ? PreviewColor(255, 255, 255) : PreviewColor(0, 0, 0);
    }
    Border bar;
    bar.Width(width);
    bar.Height(height);
    bar.Background(WidgetAppearance::Surface(background));
    bar.BorderThickness(Thickness{1, 1, 1, 1});
    bar.BorderBrush(SolidColorBrush(WidgetAppearance::WithAlpha(foreground, 40)));
    bar.CornerRadius(WidgetAppearance::DockCorners(16, presentation.edge, presentation.flushWithEdge));
    place(bar, x, y);
    if (state.visibleSegments.empty()) {
      text(side ? L"无安排" : L"今天暂无安排", side ? 8 : 12, x, y + height / 2 - 8,
           width, WidgetAppearance::WithAlpha(foreground, 130));
      return;
    }
    const double count = static_cast<double>(state.visibleSegments.size());
    const double inner = std::max(1.0, length - 34);
    const double gap = std::min(5.0, inner / (2 * count));
    const double available = inner - gap * (count - 1);
    const double minimum = std::min(side ? 4.0 : 6.0, available / (2 * count));
    const double weighted = available - minimum * count;
    int total = 0;
    for (const auto& segment : state.visibleSegments) total += std::max(1, segment.DurationMinutes());
    double cursor = (side ? y : x) + 17;
    for (const auto& segment : state.visibleSegments) {
      const double span = minimum + weighted * segment.DurationMinutes() / total;
      const double line = segment.kind == classtime::SegmentKind::Break ? 5 : 8;
      const auto kind = WidgetAppearance::KindColor(palette, segment.kind);
      const auto track = SolidColorBrush(WidgetAppearance::WithAlpha(kind, 39));
      const auto fill = SolidColorBrush(kind);
      const double progress = WidgetSegmentProgress(segment, now, state.snapshot);
      if (side) {
        rect(x + (width - line) / 2, cursor, line, span, track, line / 2);
        if (progress > 0) rect(x + (width - line) / 2, cursor, line, span * progress, fill, line / 2);
      } else {
        rect(cursor, y + (height - line) / 2, span, line, track, line / 2);
        if (progress > 0) rect(cursor, y + (height - line) / 2, span * progress, line, fill, line / 2);
      }
      cursor += span + gap;
    }
    return;
  }

  constexpr double cx = 210, cy = 70, radius = 50;
  Shapes::Ellipse face;
  face.Width(122);
  face.Height(122);
  face.Fill(surface);
  place(face, cx - 61, cy - 61);
  Shapes::Ellipse ringTrack;
  ringTrack.Width(radius * 2);
  ringTrack.Height(radius * 2);
  ringTrack.Stroke(SolidColorBrush(WidgetAppearance::WithAlpha(palette.foreground, 24)));
  ringTrack.StrokeThickness(5);
  place(ringTrack, cx - radius, cy - radius);
  if (state.snapshot.current && state.snapshot.progress > 0) {
    PathGeometry geometry;
    PathFigure figure;
    figure.StartPoint({static_cast<float>(cx), static_cast<float>(cy - radius)});
    ArcSegment arc;
    arc.Size({static_cast<float>(radius), static_cast<float>(radius)});
    arc.SweepDirection(SweepDirection::Clockwise);
    const double sweep = state.snapshot.progress * 2 * 3.14159265358979323846;
    arc.IsLargeArc(state.snapshot.progress > 0.5);
    arc.Point({static_cast<float>(cx + radius * std::sin(sweep)),
               static_cast<float>(cy - radius * std::cos(sweep))});
    figure.Segments().Append(arc);
    geometry.Figures().Append(figure);
    Shapes::Path path;
    path.Data(geometry);
    path.Stroke(accent);
    path.StrokeThickness(5);
    path.StrokeStartLineCap(PenLineCap::Round);
    path.StrokeEndLineCap(PenLineCap::Round);
    canvas.Children().Append(path);
  }
  std::wstring label, value;
  if (state.snapshot.current) {
    const auto& current = *state.snapshot.current;
    label = current.kind == classtime::SegmentKind::Focus
                ? L"第 " + std::to_wstring(current.focusOrdinal) + L" 节剩余"
                : SegmentKindLabel(current.kind) + L"剩余";
    const int remaining = (current.end.TotalMinutes() - now.MinutesSinceMidnight()) * 60 - now.second;
    wchar_t digits[16]{};
    swprintf_s(digits, L"%02d:%02d", (remaining % 3600) / 60, remaining % 60);
    value = remaining >= 3600 ? std::to_wstring(remaining / 3600) + L":" + digits : digits;
  } else if (state.snapshot.next) {
    const bool started = std::any_of(plan.begin(), plan.end(), [&now](const auto& segment) {
      return segment.start.TotalMinutes() <= now.MinutesSinceMidnight();
    });
    label = started ? L"空档 · 下一段" : L"首段开始";
    value = FormatClock(state.snapshot.next->start);
  } else {
    value = state.snapshot.hasSchedule ? L"今日已结束" : L"今天没有安排";
  }
  if (!label.empty()) text(label, 9, cx - 60, cy - 29, 120, palette.foreground);
  text(value, label.empty() ? 13 : 21, cx - 60, cy - 12, 120, palette.foreground);
  if (presentation.scope == Presentation::Scope::AllDay && state.focusTotal > 0) {
    text(L"今日 " + std::to_wstring(state.focusCompleted) + L" / " +
             std::to_wstring(state.focusTotal) + L" 节", 8, cx - 60, cy + 18, 120,
         palette.foreground);
  }
}

struct PreviewPoint {
  double x = 0;
  double y = 0;
};

// 位置示意按常见工作区比例映射偏移，始终限制在示意屏幕之内。
PreviewPoint PreviewAnchor(const Presentation& presentation, bool expanded) {
  using Edge = Presentation::Edge;
  const auto edge = presentation.edge;
  const bool side = edge == Edge::Left || edge == Edge::Right;
  const bool grip = presentation.autoHideAtEdge && !expanded;
  const bool flush = presentation.flushWithEdge;
  const double screenHeight =
      kPreviewScreenHeight + (presentation.coverTaskbar ? kPreviewTaskbarHeight : 0.0);
  if (!presentation.visible || presentation.view == Presentation::View::ScreenEdge) {
    return {kPreviewScreenCenterX, kPreviewScreenTop + screenHeight / 2};
  }
  const bool ring = presentation.view == Presentation::View::Ring && !grip;
  const double fraction = std::clamp((presentation.EffectiveBarLength() - 240) / 1360.0, 0.0, 1.0);
  if (side) {
    const double inset = grip ? 7.5 : ring ? (flush ? 46 : 55) : (flush ? 12.5 : 18.5);
    const double x = edge == Edge::Left ? kPreviewScreenLeft + inset
                                       : kPreviewScreenLeft + kPreviewScreenWidth - inset;
    const double half = grip ? 26 : ring ? 44 : (50 + 86 * fraction) / 2;
    const double y = std::clamp(kPreviewScreenTop + screenHeight / 2 +
                                    presentation.offsetY * screenHeight / 1080.0,
                                kPreviewScreenTop + half,
                                kPreviewScreenTop + screenHeight - half);
    return {x, y};
  }
  const double inset = grip ? 4.5 : ring ? (flush ? 46 : 51) : (flush ? 12.5 : 19.5);
  const double half = grip ? 26 : ring ? 44 : (96 + 234 * fraction) / 2;
  const double x = std::clamp(kPreviewScreenCenterX +
                                  presentation.offsetX * kPreviewScreenWidth / 1920.0,
                              kPreviewScreenLeft + half,
                              kPreviewScreenLeft + kPreviewScreenWidth - half);
  return {x, edge == Edge::Top ? kPreviewScreenTop + inset
                                : kPreviewScreenTop + screenHeight - inset};
}

}  // namespace

SettingsWindow::SettingsWindow(std::function<void()> exitRequested,
                               std::function<bool()> testVisualAlertRequested)
    : exitRequested_(std::move(exitRequested)),
      testVisualAlertRequested_(std::move(testVisualAlertRequested)) {
  Build();
}

SettingsWindow::~SettingsWindow() {
  StopPreviewRefresh();
  CommitScreenEdgeThickness();
  CommitBarLength();
  CommitBarBackground();
  CommitPhaseColors();
  CommitAlertColors();
  CommitTemplateName();
  Motion::Stop(editorMotion_);
  Motion::Stop(sectionMotion_);
  Motion::Stop(previewMotion_);
  Motion::Stop(previewHighlightMotion_);
  Motion::Stop(displayPanelMotion_);
  if (window_ != nullptr) {
    try {
      allowClose_ = true;
      window_.Close();
    } catch (...) {
    }
  }
  if (smallIcon_) DestroyIcon(smallIcon_);
  if (largeIcon_) DestroyIcon(largeIcon_);
}

void SettingsWindow::Build() {
  window_ = Window();
  window_.Title(L"“上课”时间到！ 设置");
  window_.ExtendsContentIntoTitleBar(true);

  const auto appWindow = window_.AppWindow();
  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  double scale = 1.0;
  if (hwnd_ != nullptr) {
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi > 0) scale = static_cast<double>(dpi) / 96.0;
    const auto loadIcon = [dpi](int widthMetric, int heightMetric) {
      const UINT iconDpi = dpi ? dpi : 96;
      return static_cast<HICON>(LoadImageW(
          GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_CLASSTIME), IMAGE_ICON,
          GetSystemMetricsForDpi(widthMetric, iconDpi), GetSystemMetricsForDpi(heightMetric, iconDpi),
          LR_DEFAULTCOLOR));
    };
    smallIcon_ = loadIcon(SM_CXSMICON, SM_CYSMICON);
    largeIcon_ = loadIcon(SM_CXICON, SM_CYICON);
    if (smallIcon_) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon_));
    if (largeIcon_) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon_));
  }
  // 设计规范：展开导航 200–220 px；主操作区 600–760 px + 预览列 260–320 px，
  // 默认窗口宽度按两栏最小内容推算。
  const int width = static_cast<int>(1200 * scale);
  const int height = static_cast<int>(800 * scale);
  try {
    const auto area = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
        appWindow.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary);
    const auto work = area.WorkArea();
    const int initialWidth = std::min(width, work.Width);
    const int initialHeight = std::min(height, work.Height);
    appWindow.MoveAndResize({work.X + (work.Width - initialWidth) / 2,
                             work.Y + (work.Height - initialHeight) / 3,
                             initialWidth, initialHeight});
  } catch (...) {
    appWindow.Resize({width, height});
  }

  appWindow.Closing([this](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
    if (allowClose_) return;
    StopPreviewRefresh();
    CommitScreenEdgeThickness();
    CommitBarLength();
    CommitBarBackground();
    CommitPhaseColors();
    CommitAlertColors();
    CommitTemplateName();
    args.Cancel(true);
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
  });

  auto root = Grid();
  rootGrid_ = root;
  window_.Content(root);

  nav_ = NavigationView();
  nav_.IsSettingsVisible(false);
  nav_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
  nav_.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);
  nav_.PaneTitle(L"");
  nav_.OpenPaneLength(228);
  nav_.CompactPaneLength(52);
  nav_.CompactModeThresholdWidth(560);
  nav_.ExpandedModeThresholdWidth(900);

  StackPanel brand;
  brand.Orientation(Orientation::Horizontal);
  brand.Spacing(11);
  brand.Margin(Thickness{14, 12, 8, 20});
  Border brandIcon;
  brandIcon.Width(40);
  brandIcon.Height(40);
  brandIcon.CornerRadius(CornerRadius{11, 11, 11, 11});
  brandIcon.Background(SolidColorBrush(AccentColor()));
  SymbolIcon clockIcon;
  clockIcon.Symbol(Symbol::Clock);
  clockIcon.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)));
  clockIcon.HorizontalAlignment(HorizontalAlignment::Center);
  clockIcon.VerticalAlignment(VerticalAlignment::Center);
  brandIcon.Child(clockIcon);
  Add(brand, brandIcon);
  StackPanel brandText;
  brandText.VerticalAlignment(VerticalAlignment::Center);
  auto brandTitle = MakeText(L"“上课”时间到！", 16);
  brandTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  brandTitle.TextWrapping(TextWrapping::NoWrap);
  Add(brandText, brandTitle);
  Add(brandText, MakeSubtitle(L"作息与课程管理"));
  Add(brand, brandText);
  nav_.PaneHeader(brand);

  auto version = MakeSubtitle(L"ClassTime  ·  v" + std::wstring(kVersion));
  version.Margin(Thickness{20, 12, 12, 18});
  nav_.PaneFooter(version);
  root.Children().Append(nav_);

  struct Section {
    const wchar_t* title;
    const wchar_t* tag;
    Symbol symbol;
  };
  const Section sections[] = {
      {L"作息计划", L"schedule", Symbol::Calendar},
      {L"显示与位置", L"display", Symbol::View},
      {L"通知", L"notifications", Symbol::Clock},
      {L"常规", L"general", Symbol::Setting},
  };
  for (const auto& section : sections) {
    NavigationViewItem item;
    item.Content(box_value(hstring(section.title)));
    item.Tag(box_value(hstring(section.tag)));
    if (std::wstring(section.tag) == L"notifications" || std::wstring(section.tag) == L"display") {
      FontIcon icon;
      icon.Glyph(std::wstring(section.tag) == L"notifications" ? L"\uEA8F" : L"\uE7F4");
      item.Icon(icon);
    } else {
      SymbolIcon icon;
      icon.Symbol(section.symbol);
      item.Icon(icon);
    }
    nav_.MenuItems().Append(item);
  }

  auto content = Grid();
  content.Margin(Thickness{28, 32, 28, 24});
  nav_.Content(content);

  auto makePage = [&](ScrollViewer& scroll) {
    ScrollViewer viewer;
    viewer.Visibility(Visibility::Collapsed);
    viewer.HorizontalScrollMode(ScrollMode::Disabled);
    viewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    viewer.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    auto stack = StackPanel();
    stack.MaxWidth(1100);
    stack.Spacing(16);
    viewer.Content(stack);
    content.Children().Append(viewer);
    scroll = viewer;
    return stack;
  };

  // —— 作息计划 ——
  auto scheduleStack = makePage(scheduleScroll_);

  scheduleNotice_ = InfoBar();
  scheduleNotice_.Severity(InfoBarSeverity::Success);
  scheduleNotice_.IsClosable(true);
  scheduleNotice_.IsOpen(false);
  Add(scheduleStack, scheduleNotice_);

  {
    // 页首保留当前状态，并把新增入口放在最容易找到的位置。
    auto header = Grid();
    auto headerLeft = ColumnDefinition();
    headerLeft.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto headerRight = ColumnDefinition();
    headerRight.Width(GridLengthHelper::Auto());
    header.ColumnDefinitions().Append(headerLeft);
    header.ColumnDefinitions().Append(headerRight);
    auto titles = StackPanel();
    titles.Spacing(4);
    Add(titles, MakeSectionTitle(L"作息计划"));
    Add(titles, MakeSubtitle(L"管理一周的作息安排，包括上课、课间与午休。"));
    Grid::SetColumn(titles, 0);
    header.Children().Append(titles);
    auto chips = StackPanel();
    chips.Orientation(Orientation::Horizontal);
    chips.Spacing(8);
    chips.VerticalAlignment(VerticalAlignment::Center);
    scheduleNowText_ = MakeText(L"", 12);
    scheduleDateText_ = MakeText(L"", 12);
    auto nowChip = MakeChip(L"", true);
    nowChip.Child(scheduleNowText_);
    auto dateChip = MakeChip(L"", false);
    dateChip.Child(scheduleDateText_);
    Add(chips, nowChip);
    Add(chips, dateChip);
    Add(titles, chips);
    auto add = Button();
    StackPanel addLabel;
    addLabel.Orientation(Orientation::Horizontal);
    addLabel.Spacing(8);
    SymbolIcon addIcon;
    addIcon.Symbol(Symbol::Add);
    addIcon.VerticalAlignment(VerticalAlignment::Center);
    Add(addLabel, addIcon);
    Add(addLabel, MakeText(L"新增时段", 14));
    add.Content(addLabel);
    Automation::AutomationProperties::SetName(add, L"新增时段");
    add.VerticalAlignment(VerticalAlignment::Top);
    StyleAction(add, true);
    add.Click([this](auto const&, auto const&) { OpenNewEditor(); });
    Grid::SetColumn(add, 1);
    header.Children().Append(add);
    Add(scheduleStack, header);
  }

  {
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(14);

    auto templateRow = Grid();
    auto labelCol = ColumnDefinition();
    labelCol.Width(GridLengthHelper::Auto());
    auto valueCol = ColumnDefinition();
    valueCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto actionCol = ColumnDefinition();
    actionCol.Width(GridLengthHelper::Auto());
    templateRow.ColumnDefinitions().Append(labelCol);
    templateRow.ColumnDefinitions().Append(valueCol);
    templateRow.ColumnDefinitions().Append(actionCol);
    templateRow.ColumnSpacing(12);
    auto label = MakeText(L"模板", 14);
    label.VerticalAlignment(VerticalAlignment::Center);
    templateRow.Children().Append(label);
    templateCombo_ = ComboBox();
    templateCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
    Grid::SetColumn(templateCombo_, 1);
    templateRow.Children().Append(templateCombo_);
    auto addTemplate = Button();
    addTemplate.Content(box_value(hstring(L"新增模板")));
    StyleAction(addTemplate, false);
    addTemplate.Click([this](auto const&, auto const&) { ShowAddTemplateDialog(); });
    Grid::SetColumn(addTemplate, 2);
    templateRow.Children().Append(addTemplate);
    planScope_ = RadioButtons();
    planScope_.MaxColumns(2);
    planScope_.Items().Append(box_value(L"常规模板"));
    planScope_.Items().Append(box_value(L"今天的计划"));
    planScope_.SelectedIndex(0);
    Automation::AutomationProperties::SetName(planScope_, L"查看的作息计划");
    Add(stack, MakeAdaptiveColumns(templateRow, planScope_, 270, 650));

    templateStatus_ = MakeSubtitle(L"");
    Add(stack, templateStatus_);

    Expander templateDetails;
    templateDetails.Header(box_value(L"修改模板名称"));
    templateDetails.HorizontalAlignment(HorizontalAlignment::Stretch);
    templateDetails.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    StackPanel templateFields;
    templateFields.HorizontalAlignment(HorizontalAlignment::Stretch);
    templateFields.Spacing(10);
    templateNameBox_ = TextBox();
    templateNameBox_.Header(box_value(hstring(L"模板名称")));
    templateNameBox_.PlaceholderText(L"例如：我的高中课表");
    Automation::AutomationProperties::SetName(templateNameBox_, L"模板名称");
    Add(templateFields, templateNameBox_);
    Add(templateFields, MakeSubtitle(L"修改名称后自动保存。"));

    templateDetails.Content(templateFields);
    Add(stack, templateDetails);

    auto weekdayHeading = MakeText(L"生效星期", 14);
    weekdayHeading.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    Add(stack, weekdayHeading);
    weekdayPanel_ = VariableSizedWrapGrid();
    weekdayPanel_.Orientation(Orientation::Horizontal);
    weekdayPanel_.ItemWidth(80);
    weekdayPanel_.ItemHeight(34);
    Add(stack, weekdayPanel_);
    card.Child(stack);

    auto editingCard = MakeCard();
    auto editingStack = StackPanel();
    editingStack.Spacing(12);
    Add(editingStack, MakeCardTitle(L"时段安排"));

    timetableScroll_ = ScrollViewer();
    timetableScroll_.HorizontalScrollMode(ScrollMode::Enabled);
    timetableScroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    timetableScroll_.VerticalScrollMode(ScrollMode::Enabled);
    timetableScroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    timetableScroll_.MaxHeight(430);
    Automation::AutomationProperties::SetName(timetableScroll_, L"课表预览");

    segmentList_ = StackPanel();
    segmentList_.Spacing(4);

    auto buttons = StackPanel();
    buttons.Spacing(8);
    restorePresetButton_ = Button();
    restorePresetButton_.Content(box_value(hstring(L"恢复默认...")));
    StyleAction(restorePresetButton_, false, true);
    ToolTipService::SetToolTip(restorePresetButton_, box_value(L"仅内置高中和大学模板可恢复预设"));
    restorePresetButton_.Click([this](auto const&, auto const&) { ConfirmRestorePreset(); });
    clearTodayButton_ = Button();
    clearTodayButton_.Content(box_value(hstring(L"撤销今日调整")));
    StyleAction(clearTodayButton_, false, true);
    clearTodayButton_.Click([this](auto const&, auto const&) {
      Services::Instance().ClearTodayOverride();
      ShowScheduleNotice(L"已撤销今天的临时调整，恢复按常规模板执行。");
      RefreshAll();
    });
    Add(buttons, restorePresetButton_);
    Add(buttons, clearTodayButton_);
    BindActionRow(buttons, 350);
    Add(editingStack, buttons);
    Add(editingStack, segmentList_);

    // 时间轴和周视图仍可展开编辑，列表优先展示日常作息。
    Expander timelineExpander;
    timelineExpander.Header(box_value(L"时间轴 · 拖动调整时段"));
    timelineExpander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    StackPanel timelineStack;
    timelineStack.Spacing(10);
    Add(timelineStack, MakeSubtitle(L"拖动时段或两端调整时间；双击时段编辑，双击空白处新增。"));
    auto zoomRow = Grid();
    zoomRow.Children().Append(MakeText(L"缩放", 14));
    timelineZoomSlider_ = Slider();
    timelineZoomSlider_.Minimum(40);
    timelineZoomSlider_.Maximum(200);
    timelineZoomSlider_.StepFrequency(10);
    timelineZoomSlider_.Value(timelinePxPerHour_);
    timelineZoomSlider_.VerticalAlignment(VerticalAlignment::Bottom);
    Automation::AutomationProperties::SetName(timelineZoomSlider_, L"时间轴缩放");
    Grid::SetColumn(timelineZoomSlider_, 1);
    auto zoomColumn = ColumnDefinition();
    zoomColumn.Width(GridLengthHelper::FromValueAndType(160, GridUnitType::Pixel));
    auto zoomSpace = ColumnDefinition();
    zoomSpace.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    zoomRow.ColumnDefinitions().Append(zoomSpace);
    zoomRow.ColumnDefinitions().Append(zoomColumn);
    zoomRow.ColumnSpacing(12);
    zoomRow.Children().Append(timelineZoomSlider_);
    Add(timelineStack, zoomRow);

    auto snapRow = StackPanel();
    snapRow.Orientation(Orientation::Horizontal);
    snapRow.Spacing(12);
    auto snapLabel = MakeText(L"拖动步进", 14);
    snapLabel.VerticalAlignment(VerticalAlignment::Center);
    Add(snapRow, snapLabel);
    timelineSnapCombo_ = ComboBox();
    timelineSnapCombo_.Items().Append(box_value(L"1 分钟"));
    timelineSnapCombo_.Items().Append(box_value(L"5 分钟"));
    timelineSnapCombo_.Items().Append(box_value(L"15 分钟"));
    Automation::AutomationProperties::SetName(timelineSnapCombo_, L"时间轴拖动步进");
    Add(snapRow, timelineSnapCombo_);
    Add(timelineStack, snapRow);

    timelineScroll_ = ScrollViewer();
    timelineScroll_.HorizontalScrollMode(ScrollMode::Enabled);
    timelineScroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    timelineScroll_.VerticalScrollMode(ScrollMode::Disabled);
    timelineScroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
    timelineScroll_.BorderBrush(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                             Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0)));
    timelineScroll_.BorderThickness(Thickness{1, 1, 1, 1});
    timelineScroll_.CornerRadius(CornerRadius{6, 6, 6, 6});
    Automation::AutomationProperties::SetName(timelineScroll_, L"作息时间轴");
    timelineCanvas_ = Canvas();
    timelineCanvas_.Height(100);
    // 透明背景让空白区域也参与命中测试，双击空白新增才可用。
    timelineCanvas_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
    timelineScroll_.Content(timelineCanvas_);
    timelineScroll_.Loaded([this](auto const&, auto const&) {
      window_.DispatcherQueue().TryEnqueue([this] { PositionTimelineInitially(); });
    });
    timelineScroll_.SizeChanged([this](auto const&, auto const&) { PositionTimelineInitially(); });
    Add(timelineStack, timelineScroll_);
    timelineStatusText_ = MakeSubtitle(L"");
    Add(timelineStack, timelineStatusText_);
    timelineExpander.Content(timelineStack);
    Add(editingStack, timelineExpander);
    Expander timetableExpander;
    timetableExpander.Header(box_value(L"课表预览"));
    timetableExpander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    timetableExpander.Content(timetableScroll_);
    Add(editingStack, timetableExpander);

    editingCard.Child(editingStack);
    auto mainStack = StackPanel();
    mainStack.Spacing(12);
    Add(mainStack, card);
    Add(mainStack, editingCard);

    Expander overview;
    overview.Header(box_value(L"今日概览"));
    overview.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    todayOverview_ = StackPanel();
    todayOverview_.Spacing(6);
    overview.Content(todayOverview_);
    Add(mainStack, overview);
    Add(scheduleStack, mainStack);
  }

  // —— 显示与位置 ——
  displayScroll_ = ScrollViewer();
  displayScroll_.Visibility(Visibility::Collapsed);
  displayScroll_.VerticalScrollMode(ScrollMode::Disabled);
  displayScroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
  displayScroll_.HorizontalScrollMode(ScrollMode::Disabled);
  displayScroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  displayScroll_.VerticalContentAlignment(VerticalAlignment::Stretch);
  displayScroll_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
  Grid displayPage;
  displayPage.MaxWidth(1180);
  displayPage.RowSpacing(16);
  RowDefinition displayHeadingRow;
  displayHeadingRow.Height(GridLengthHelper::Auto());
  RowDefinition displayWorkspaceRow;
  displayWorkspaceRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  displayPage.RowDefinitions().Append(displayHeadingRow);
  displayPage.RowDefinitions().Append(displayWorkspaceRow);
  auto displayHeading = StackPanel();
  displayHeading.Spacing(4);
  Add(displayHeading, MakeSectionTitle(L"显示与位置"));
  Add(displayHeading, MakeSubtitle(L"调整桌面组件的显示样式、显示范围与位置。"));
  displayPage.Children().Append(displayHeading);
  Grid displayWorkspace;
  Grid::SetRow(displayWorkspace, 1);
  displayPage.Children().Append(displayWorkspace);
  displayScroll_.Content(displayPage);
  content.Children().Append(displayScroll_);
  {
    // 实时预览固定在上方，下面的设置独立滚动。
    StackPanel settingsContent;
    settingsContent.Spacing(12);
    auto viewCard = MakeCard();
    StackPanel viewStack;
    viewStack.Spacing(10);
    Add(viewStack, MakeCardTitle(L"显示形式"));
    Add(viewStack, MakeSubtitle(L"选择桌面组件的样式。"));
    Add(viewStack, MakeViewChoices(viewSegments_, [this](int index) {
      SelectSegment(viewSegments_, index);
      constexpr Presentation::View views[] = {
          Presentation::View::Bar, Presentation::View::Ring, Presentation::View::ScreenEdge};
      auto presentation = Services::Instance().config().presentation;
      if (presentation.view == views[index]) return;
      presentation.view = views[index];
      Services::Instance().UpdatePresentation(presentation);
    }));
    viewCard.Child(viewStack);
    Add(settingsContent, viewCard);

    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(10);
    Add(stack, MakeCardTitle(L"显示设置"));
    Add(stack, MakeText(L"显示范围", 14));
    Add(stack, MakeSegments(scopeSegments_, {L"全天", L"当前时段"}, [this](int index) {
      SelectSegment(scopeSegments_, index);
      const auto scope = index == 0 ? Presentation::Scope::AllDay : Presentation::Scope::CurrentBlock;
      auto presentation = Services::Instance().config().presentation;
      if (presentation.scope == scope) return;
      presentation.scope = scope;
      Services::Instance().UpdatePresentation(presentation);
    }));

    auto locationCard = MakeCard();
    auto locationStack = StackPanel();
    locationStack.Spacing(10);
    Add(locationStack, MakeCardTitle(L"位置设置"));
    monitorCombo_ = ComboBox();
    monitorCombo_.Header(box_value(L"组件所在显示器"));
    monitorCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
    Automation::AutomationProperties::SetName(monitorCombo_, L"组件所在显示器");
    Add(locationStack, monitorCombo_);
    monitorHint_ = MakeSubtitle(L"");
    Add(locationStack, monitorHint_);

    screenEdgeControls_ = StackPanel();
    screenEdgeControls_.Spacing(4);
    auto thicknessRow = Grid();
    thicknessRow.Children().Append(MakeText(L"边框粗细", 14));
    screenEdgeValue_ = MakeSubtitle(L"");
    screenEdgeValue_.HorizontalAlignment(HorizontalAlignment::Right);
    thicknessRow.Children().Append(screenEdgeValue_);
    Add(screenEdgeControls_, thicknessRow);
    screenEdgeSlider_ = Slider();
    screenEdgeSlider_.Minimum(Presentation::kMinScreenEdgeThickness);
    screenEdgeSlider_.Maximum(Presentation::kMaxScreenEdgeThickness);
    screenEdgeSlider_.StepFrequency(1);
    Automation::AutomationProperties::SetName(screenEdgeSlider_, L"边框粗细");
    Add(screenEdgeControls_, screenEdgeSlider_);
    Add(screenEdgeControls_, MakeSubtitle(L"鼠标可穿过边框，全天进度从首段开始到末段结束。"));
    screenEdgeColorPanel_ = StackPanel();
    screenEdgeColorPanel_.Spacing(6);
    Add(screenEdgeColorPanel_, MakeText(L"全天配色", 14));
    Add(screenEdgeColorPanel_, MakeSegments(screenEdgeColorSegments_,
        {L"按时段分色", L"跟随当前时段"}, [this](int index) {
          SelectSegment(screenEdgeColorSegments_, index);
          const auto colors = index == 0 ? Presentation::ScreenEdgeColors::Schedule
                                         : Presentation::ScreenEdgeColors::CurrentState;
          auto presentation = Services::Instance().config().presentation;
          if (presentation.screenEdgeColors == colors) return;
          presentation.screenEdgeColors = colors;
          Services::Instance().UpdatePresentation(presentation);
        }));
    Add(screenEdgeColorPanel_, MakeSubtitle(L"分色按各时段的实际时长排列，较浅部分尚未到达。"));
    screenEdgeGradientToggle_ = ToggleSwitch();
    screenEdgeGradientRow_ = MakeToggleRow(
        screenEdgeGradientToggle_, L"颜色平滑渐变",
        L"关闭后各时段与进度边界清晰分段，不做渐变过渡");
    Add(screenEdgeColorPanel_, screenEdgeGradientRow_);
    Add(screenEdgeControls_, screenEdgeColorPanel_);
    screenEdgeHighlightToggle_ = ToggleSwitch();
    Add(screenEdgeControls_, MakeToggleRow(
        screenEdgeHighlightToggle_, L"高亮当前进度位置",
        L"在当前时间前沿显示流光与呼吸闪烁高光，便于一眼分辨进度位置"));

    dockControls_ = StackPanel();
    dockControls_.Spacing(10);
    Add(dockControls_, MakeText(L"贴边位置", 14));
    Add(dockControls_, MakeSegments(edgeSegments_, {L"顶部", L"底部", L"左侧", L"右侧"},
                                    [this](int index) {
      SelectSegment(edgeSegments_, index);
      constexpr Presentation::Edge edges[] = {
          Presentation::Edge::Top, Presentation::Edge::Bottom,
          Presentation::Edge::Left, Presentation::Edge::Right};
      auto presentation = Services::Instance().config().presentation;
      const auto edge = edges[index];
      if (presentation.edge == edge) return;
      presentation.edge = edge;
      Services::Instance().UpdatePresentation(presentation);
    }));
    reanchorButton_ = Button();
    reanchorButton_.Content(box_value(hstring(L"恢复居中位置")));
    StyleAction(reanchorButton_, false, true);
    reanchorButton_.HorizontalAlignment(HorizontalAlignment::Left);
    reanchorButton_.Click([](auto const&, auto const&) {
      auto presentation = Services::Instance().config().presentation;
      presentation.visible = true;
      presentation.offsetX = 0;
      presentation.offsetY = 0;
      Services::Instance().UpdatePresentation(presentation);
    });
    Add(dockControls_, reanchorButton_);
    // 长条长度：分节长条沿贴边方向的整体长度；仅长条视图可见。
    barLengthPanel_ = StackPanel();
    barLengthPanel_.Spacing(4);
    auto lengthRow = Grid();
    lengthRow.Children().Append(MakeText(L"长条长度", 14));
    barLengthValue_ = MakeSubtitle(L"");
    barLengthValue_.HorizontalAlignment(HorizontalAlignment::Right);
    lengthRow.Children().Append(barLengthValue_);
    Add(barLengthPanel_, lengthRow);
    barLengthSlider_ = Slider();
    barLengthSlider_.Minimum(Presentation::kMinBarLength);
    barLengthSlider_.Maximum(Presentation::kMaxBarLength);
    barLengthSlider_.StepFrequency(10);
    Automation::AutomationProperties::SetName(barLengthSlider_, L"长条长度");
    Add(barLengthPanel_, barLengthSlider_);
    Add(barLengthPanel_, MakeSubtitle(L"左右贴边时调整竖向长度。"));
    auto resetBarLength = Button();
    resetBarLength.Content(box_value(hstring(L"恢复默认长度")));
    StyleAction(resetBarLength, false, true);
    resetBarLength.Click([this](auto const&, auto const&) {
      if (barLengthTimer_) barLengthTimer_.Stop();
      pendingBarLength_.reset();
      auto presentation = Services::Instance().config().presentation;
      presentation.barLengthDip = Presentation::kDefaultBarLength;
      Services::Instance().UpdatePresentation(presentation);
    });
    Add(barLengthPanel_, resetBarLength);
    Add(stack, barLengthPanel_);

    barColorPanel_ = StackPanel();
    barColorPanel_.Spacing(4);
    barBackgroundToggle_ = ToggleSwitch();
    Add(barColorPanel_, MakeToggleRow(barBackgroundToggle_, L"自定义背景色",
                                     L"关闭时使用系统背景色"));
    barColorPicker_ = ColorPicker();
    barColorPicker_.IsAlphaEnabled(false);
    barColorPicker_.IsMoreButtonVisible(false);
    barColorPicker_.Visibility(Visibility::Collapsed);
    Automation::AutomationProperties::SetName(barColorPicker_, L"长条背景颜色");
    Add(barColorPanel_, barColorPicker_);
    Add(stack, barColorPanel_);

    Expander phaseColorExpander;
    phaseColorExpander.Header(box_value(L"自定义阶段配色（上课 / 课间 / 午休）"));
    phaseColorExpander.HorizontalAlignment(HorizontalAlignment::Stretch);
    phaseColorExpander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    StackPanel phaseColorBody;
    phaseColorBody.Spacing(8);
    Add(phaseColorBody, MakeSegments(
        phaseColorTargetSegments_, {L"上课", L"课间", L"午休"}, [this](int index) {
          phaseColorTarget_ = index;
          RefreshPhaseColorEditor();
        }));
    phaseColorToggle_ = ToggleSwitch();
    Add(phaseColorBody, MakeToggleRow(
        phaseColorToggle_, L"自定义该阶段颜色", L"应用于长条、圆环、全屏边框、课表与时间轴"));
    phaseColorHint_ = MakeSubtitle(L"");
    Add(phaseColorBody, phaseColorHint_);
    phaseColorPicker_ = ColorPicker();
    phaseColorPicker_.IsAlphaEnabled(false);
    phaseColorPicker_.IsMoreButtonVisible(false);
    phaseColorPicker_.Visibility(Visibility::Collapsed);
    Automation::AutomationProperties::SetName(phaseColorPicker_, L"阶段自定义颜色");
    Add(phaseColorBody, phaseColorPicker_);
    auto resetPhaseColors = Button();
    resetPhaseColors.Content(box_value(hstring(L"恢复全部默认阶段配色")));
    StyleAction(resetPhaseColors, false, true);
    resetPhaseColors.HorizontalAlignment(HorizontalAlignment::Left);
    resetPhaseColors.Click([this](auto const&, auto const&) {
      if (phaseColorTimer_) phaseColorTimer_.Stop();
      pendingPhaseColors_.fill(std::nullopt);
      auto presentation = Services::Instance().config().presentation;
      presentation.focusColor.clear();
      presentation.restColor.clear();
      presentation.lunchColor.clear();
      Services::Instance().UpdatePresentation(presentation);
      RefreshPhaseColorEditor();
    });
    Add(phaseColorBody, resetPhaseColors);
    phaseColorExpander.Content(phaseColorBody);
    Add(stack, phaseColorExpander);

    // 两种形式各自需要的贴边选项放在同一个宿主里：切换时宿主高度做补间，
    // 下面的开关就不会被瞬间顶开或吸上。
    displayOptionsHost_ = Grid();
    displayOptionsClip_ = RectangleGeometry();
    screenEdgeControls_.VerticalAlignment(VerticalAlignment::Top);
    dockControls_.VerticalAlignment(VerticalAlignment::Top);
    displayOptionsHost_.Children().Append(screenEdgeControls_);
    displayOptionsHost_.Children().Append(dockControls_);
    screenEdgeControls_.Visibility(Visibility::Collapsed);
    dockControls_.Visibility(Visibility::Collapsed);
    displayOptionsHost_.SizeChanged([this](auto const&, auto const& args) {
      const auto size = args.NewSize();
      displayOptionsClip_.Rect({0.0f, 0.0f, size.Width, size.Height});
    });
    Add(locationStack, displayOptionsHost_);

    card.Child(stack);
    locationCard.Child(locationStack);
    Add(settingsContent, MakeAdaptiveColumns(card, locationCard, 400, 820));

    auto behaviorCard = MakeCard();
    StackPanel behaviorStack;
    behaviorStack.Spacing(10);
    Add(behaviorStack, MakeCardTitle(L"行为设置"));
    visibleToggle_ = ToggleSwitch();
    Add(behaviorStack, MakeToggleRow(visibleToggle_, L"显示桌面组件"));
    hideInFullscreenToggle_ = ToggleSwitch();
    Add(behaviorStack, MakeToggleRow(
        hideInFullscreenToggle_, L"全屏应用时自动隐藏",
        L"目标显示器有全屏应用时临时隐藏组件，不影响后台提醒"));
    coverTaskbarToggle_ = ToggleSwitch();
    Add(behaviorStack, MakeToggleRow(
        coverTaskbarToggle_, L"允许覆盖任务栏",
        L"贴合完整屏幕四边或将组件覆于任务栏上方；仍遵循开始菜单与系统层级"));
    dockBehavior_ = StackPanel();
    dockBehavior_.Spacing(10);
    flushToggle_ = ToggleSwitch();
    Add(dockBehavior_, MakeToggleRow(flushToggle_, L"紧贴边缘", L"关闭后保留一点间距"));
    edgeAutoHideToggle_ = ToggleSwitch();
    Add(dockBehavior_, MakeToggleRow(edgeAutoHideToggle_, L"贴边自动隐藏", L"鼠标靠近时展开组件"));
    Add(behaviorStack, dockBehavior_);
    behaviorCard.Child(behaviorStack);
    Add(settingsContent, behaviorCard);

    // 大幅预览与引擎状态摘要保持在页面前方。
    auto previewCard = MakeCard();
    previewCard.Padding(Thickness{16, 12, 16, 12});
    previewCard.VerticalAlignment(VerticalAlignment::Top);
    auto previewContent = StackPanel();
    previewContent.Spacing(6);
    Add(previewContent, MakeCardTitle(L"实时预览"));
    Grid previewVisuals;
    ColumnDefinition detailColumn;
    detailColumn.Width(GridLengthHelper::FromValueAndType(3, GridUnitType::Star));
    ColumnDefinition positionColumn;
    positionColumn.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
    previewVisuals.ColumnDefinitions().Append(detailColumn);
    previewVisuals.ColumnDefinitions().Append(positionColumn);
    previewVisuals.ColumnSpacing(12);
    previewDetail_ = Canvas();
    previewDetail_.Width(420);
    previewDetail_.Height(140);
    previewDetail_.IsHitTestVisible(false);
    Viewbox previewDetailScale;
    previewDetailScale.MaxHeight(84);
    previewDetailScale.HorizontalAlignment(HorizontalAlignment::Stretch);
    previewDetailScale.Child(previewDetail_);
    Border previewDetailFrame;
    previewDetailFrame.CornerRadius(CornerRadius{8, 8, 8, 8});
    previewDetailFrame.Background(ResourceBrush(L"SubtleFillColorSecondaryBrush",
                                               Windows::UI::ColorHelper::FromArgb(20, 128, 128, 128)));
    previewDetailFrame.Child(previewDetailScale);
    previewVisuals.Children().Append(previewDetailFrame);
    previewSummaryText_ = MakeSubtitle(L"");
    displayPreview_ = Canvas();
    displayPreview_.Width(kPreviewWidth);
    displayPreview_.Height(kPreviewHeight);
    displayPreview_.IsHitTestVisible(false);
    const auto makeStage = [this] {
      Canvas stage;
      stage.Width(kPreviewWidth);
      stage.Height(kPreviewHeight);
      stage.IsHitTestVisible(false);
      stage.Opacity(0);
      return stage;
    };
    previewChrome_ = Canvas();
    previewChrome_.Width(kPreviewWidth);
    previewChrome_.Height(kPreviewHeight);
    previewChrome_.IsHitTestVisible(false);
    DrawPreviewChrome(previewChrome_);
    previewStageA_ = makeStage();
    previewStageB_ = makeStage();
    displayPreview_.Children().Append(previewChrome_);
    Canvas motionSurface;
    motionSurface.Width(kPreviewWidth);
    motionSurface.Height(kPreviewHeight);
    RectangleGeometry screenClip;
    screenClip.Rect({kPreviewScreenLeft, kPreviewScreenTop, kPreviewScreenWidth,
                     kPreviewScreenHeight + kPreviewTaskbarHeight});
    motionSurface.Clip(screenClip);
    motionSurface.Children().Append(previewStageA_);
    motionSurface.Children().Append(previewStageB_);
    displayPreview_.Children().Append(motionSurface);
    Viewbox previewScale;
    previewScale.MaxHeight(84);
    previewScale.HorizontalAlignment(HorizontalAlignment::Stretch);
    previewScale.Child(displayPreview_);
    Grid::SetColumn(previewScale, 1);
    previewVisuals.Children().Append(previewScale);
    Add(previewContent, previewVisuals);
    Add(previewContent, previewSummaryText_);
    ToolTipService::SetToolTip(previewCard, box_value(L"位置为缩放示意；自动隐藏时悬停查看展开效果。"));
    previewCard.Child(previewContent);
    previewCard.PointerEntered([this](auto const&, auto const&) {
      previewHovered_ = true;
      const auto& presentation = Services::Instance().config().presentation;
      if (presentation.autoHideAtEdge && presentation.view != Presentation::View::ScreenEdge) {
        RefreshDisplayPreview(presentation);
      }
    });
    previewCard.PointerExited([this](auto const&, auto const&) {
      previewHovered_ = false;
      const auto& presentation = Services::Instance().config().presentation;
      if (presentation.autoHideAtEdge && presentation.view != Presentation::View::ScreenEdge) {
        RefreshDisplayPreview(presentation);
      }
    });
    ScrollViewer settingsPane;
    settingsPane.VerticalScrollMode(ScrollMode::Enabled);
    settingsPane.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    settingsPane.HorizontalScrollMode(ScrollMode::Disabled);
    settingsPane.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    settingsPane.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    settingsPane.Content(settingsContent);

    RowDefinition firstRow;
    RowDefinition secondRow;
    firstRow.Height(GridLengthHelper::Auto());
    secondRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    displayWorkspace.RowDefinitions().Append(firstRow);
    displayWorkspace.RowDefinitions().Append(secondRow);
    displayWorkspace.RowSpacing(12);
    Grid::SetRow(settingsPane, 1);
    displayWorkspace.Children().Append(previewCard);
    displayWorkspace.Children().Append(settingsPane);
  }

  // —— 通知 ——
  auto notificationsStack = makePage(notificationsScroll_);
  Add(notificationsStack, MakeSectionTitle(L"通知"));
  {
    StackPanel hero;
    hero.Spacing(6);
    hero.HorizontalAlignment(HorizontalAlignment::Center);
    Border heroIcon;
    heroIcon.Width(64);
    heroIcon.Height(64);
    heroIcon.CornerRadius(CornerRadius{16, 16, 16, 16});
    auto iconTint = AccentColor();
    iconTint.A = 30;
    heroIcon.Background(SolidColorBrush(iconTint));
    heroIcon.HorizontalAlignment(HorizontalAlignment::Center);
    FontIcon bell;
    bell.Glyph(L"\uEA8F");
    bell.FontSize(29);
    bell.Foreground(SolidColorBrush(AccentColor()));
    bell.HorizontalAlignment(HorizontalAlignment::Center);
    bell.VerticalAlignment(VerticalAlignment::Center);
    heroIcon.Child(bell);
    Add(hero, heroIcon);
    auto heroTitle = MakeText(L"作息提醒", 22);
    heroTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    heroTitle.HorizontalAlignment(HorizontalAlignment::Center);
    Add(hero, heroTitle);
    auto heroSubtitle = MakeSubtitle(L"在合适的时间开始工作与休息。");
    heroSubtitle.HorizontalAlignment(HorizontalAlignment::Center);
    Add(hero, heroSubtitle);
    notificationStateText_ = MakeText(L"", 13);
    notificationStateText_.TextAlignment(TextAlignment::Center);
    Add(hero, notificationStateText_);
    Add(notificationsStack, hero);

    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(10);
    notificationsToggle_ = ToggleSwitch();
    Add(stack, MakeToggleRow(notificationsToggle_, L"启用作息提醒",
                             L"根据作息计划在关键时间点发送系统通知。", L"\uEA8F"));
    Add(stack, MakeDivider());
    muteToggle_ = ToggleSwitch();
    Add(stack, MakeToggleRow(muteToggle_, L"临时暂停提醒",
                             L"暂停期间不再发送通知，但作息进度会继续运行。", L"\uE769"));
    card.Child(stack);
    Add(notificationsStack, card);

    Add(notificationsStack, MakeCardTitle(L"提醒时机"));
    auto timingCard = MakeCard();
    StackPanel timingStack;
    timingStack.Spacing(8);
    focusToggle_ = ToggleSwitch();
    Add(timingStack, MakeToggleRow(focusToggle_, L"上课开始与结束", L"", L"\uE7BE"));
    Add(timingStack, MakeDivider());
    restToggle_ = ToggleSwitch();
    Add(timingStack, MakeToggleRow(restToggle_, L"课间开始与结束", L"", L"\uEC32"));
    Add(timingStack, MakeDivider());
    lunchToggle_ = ToggleSwitch();
    Add(timingStack, MakeToggleRow(lunchToggle_, L"午休开始与结束", L"", L"\uE708"));
    timingCard.Child(timingStack);
    Add(notificationsStack, timingCard);

    Add(notificationsStack, MakeCardTitle(L"视觉到点动效"));
    auto visualCard = MakeCard();
    auto visualStack = StackPanel();
    visualStack.Spacing(10);
    screenEdgeFlashToggle_ = ToggleSwitch();
    Add(visualStack, MakeToggleRow(
        screenEdgeFlashToggle_, L"全屏边缘闪光提醒",
        L"到点时在目标显示器四边发出 3 次柔和阶段色呼吸光带，鼠标点击完全穿透", L"\uE793"));
    Add(visualStack, MakeDivider());
    dropClockToggle_ = ToggleSwitch();
    Add(visualStack, MakeToggleRow(
        dropClockToggle_, L"掉落 3D 放大时钟提醒",
        L"到点时从屏幕顶部/边角掉落并放大 3D 立体时钟，可调整大小和停留时间，悬停暂留、点击收回", L"\uE823"));

    Border visualPreviewFrame;
    visualPreviewFrame.CornerRadius(CornerRadius{10, 10, 10, 10});
    visualPreviewFrame.Padding(Thickness{8, 8, 8, 8});
    visualPreviewFrame.Background(
        SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 20, 24, 34)));
    visualPreviewFrame.BorderBrush(ResourceBrush(
        L"CardStrokeColorDefaultBrush", Windows::UI::ColorHelper::FromArgb(50, 255, 255, 255)));
    visualPreviewFrame.BorderThickness(Thickness{1, 1, 1, 1});
    visualPreviewFrame.HorizontalAlignment(HorizontalAlignment::Stretch);
    Viewbox visualPreviewFit;
    visualPreviewFit.MaxHeight(220);
    visualPreviewFit.Stretch(Stretch::Uniform);
    visualPreviewCanvas_ = Canvas();
    visualPreviewCanvas_.Width(420);
    visualPreviewCanvas_.Height(206);
    visualPreviewFit.Child(visualPreviewCanvas_);
    visualPreviewFrame.Child(visualPreviewFit);
    Add(visualStack, visualPreviewFrame);
    visualPreviewCaption_ = MakeSubtitle(L"");
    Add(visualStack, visualPreviewCaption_);

    dropClockOptionsPanel_ = StackPanel();
    dropClockOptionsPanel_.Spacing(10);
    Add(dropClockOptionsPanel_, MakeDivider());
    Add(dropClockOptionsPanel_, MakeText(L"3D 时钟样式", 14));
    Add(dropClockOptionsPanel_, MakeSegments(
        dropClockStyleSegments_, {kClockNames[0], kClockNames[1], kClockNames[2], kClockNames[3]},
        [this](int index) {
          SelectSegment(dropClockStyleSegments_, index);
          const auto style = static_cast<ClockStyle>(index);
          auto settings = Services::Instance().config().notifications;
          if (settings.dropClockStyle == style) return;
          settings.dropClockStyle = style;
          Services::Instance().UpdateNotifications(settings);
        }));
    Add(dropClockOptionsPanel_, MakeText(L"时钟大小", 14));
    Add(dropClockOptionsPanel_, MakeSegments(dropClockScaleSegments_, {L"75%", L"100%", L"125%"},
        [this](int index) {
          SelectSegment(dropClockScaleSegments_, index);
          auto settings = Services::Instance().config().notifications;
          if (settings.dropClockScalePercent == kClockScales[index]) return;
          settings.dropClockScalePercent = kClockScales[index];
          Services::Instance().UpdateNotifications(settings);
        }));
    Add(dropClockOptionsPanel_, MakeText(L"停留时间", 14));
    Add(dropClockOptionsPanel_, MakeSegments(dropClockHoldSegments_, {L"2 秒", L"4 秒", L"8 秒"},
        [this](int index) {
          SelectSegment(dropClockHoldSegments_, index);
          auto settings = Services::Instance().config().notifications;
          if (settings.dropClockHoldSeconds == kClockHolds[index]) return;
          settings.dropClockHoldSeconds = kClockHolds[index];
          Services::Instance().UpdateNotifications(settings);
        }));
    Add(dropClockOptionsPanel_, MakeText(L"掉落位置", 14));
    Add(dropClockOptionsPanel_, MakeSegments(
        dropClockPositionSegments_, {L"顶部居中", L"右上角", L"左上角"}, [this](int index) {
          SelectSegment(dropClockPositionSegments_, index);
          using Pos = classtime::NotificationSettings::AlertClockPosition;
          constexpr Pos positions[] = {Pos::TopCenter, Pos::TopRight, Pos::TopLeft};
          auto settings = Services::Instance().config().notifications;
          if (settings.dropClockPosition == positions[index]) return;
          settings.dropClockPosition = positions[index];
          Services::Instance().UpdateNotifications(settings);
        }));
    Add(dropClockOptionsPanel_, MakeText(L"3D 时钟皮肤", 14));
    {
      using Skin = classtime::NotificationSettings::AlertClockSkin;
      struct SkinChoice {
        Skin skin;
        const wchar_t* label;
      };
      constexpr SkinChoice kSkins[] = {
          {Skin::Graphite, L"深邃金属"},
          {Skin::Ivory, L"象牙白瓷"},
          {Skin::VintageGold, L"复古金铜"},
          {Skin::CyberNeon, L"霓虹赛博"},
          {Skin::RetroMatrix, L"复古荧光"},
          {Skin::OceanBlue, L"海洋蔚蓝"},
      };
      dropClockSkinGrid_ = Grid();
      dropClockSkinGrid_.ColumnSpacing(6);
      dropClockSkinGrid_.RowSpacing(6);
      for (int c = 0; c < 3; ++c) {
        ColumnDefinition col;
        col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        dropClockSkinGrid_.ColumnDefinitions().Append(col);
      }
      for (int r = 0; r < 2; ++r) {
        RowDefinition row;
        row.Height(GridLengthHelper::Auto());
        dropClockSkinGrid_.RowDefinitions().Append(row);
      }
      for (int i = 0; i < 6; ++i) {
        const auto preset = WidgetAppearance::GetClockSkinPreset(kSkins[i].skin);
        Primitives::ToggleButton btn;
        btn.HorizontalAlignment(HorizontalAlignment::Stretch);
        btn.HorizontalContentAlignment(HorizontalAlignment::Center);
        btn.MinHeight(38);
        btn.Padding(Thickness{8, 6, 8, 6});
        btn.CornerRadius(CornerRadius{8, 8, 8, 8});

        StackPanel body;
        body.Orientation(Orientation::Horizontal);
        body.Spacing(7);
        body.VerticalAlignment(VerticalAlignment::Center);

        StackPanel swatches;
        swatches.Orientation(Orientation::Horizontal);
        swatches.Spacing(2);
        swatches.VerticalAlignment(VerticalAlignment::Center);
        for (const auto swatchColor : {preset.body, preset.face, preset.faceText}) {
          Border dot;
          dot.Width(9);
          dot.Height(9);
          dot.CornerRadius(CornerRadius{4.5, 4.5, 4.5, 4.5});
          dot.Background(SolidColorBrush(swatchColor));
          dot.BorderBrush(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(72, 128, 136, 152)));
          dot.BorderThickness(Thickness{0.75, 0.75, 0.75, 0.75});
          swatches.Children().Append(dot);
        }
        body.Children().Append(swatches);
        auto name = MakeText(kSkins[i].label, 12.5);
        name.TextWrapping(TextWrapping::NoWrap);
        name.VerticalAlignment(VerticalAlignment::Center);
        body.Children().Append(name);
        btn.Content(body);
        Automation::AutomationProperties::SetName(btn, hstring(std::wstring(L"3D 时钟皮肤 ") + kSkins[i].label));

        const Skin chosenSkin = kSkins[i].skin;
        btn.Click([this, i, chosenSkin](auto const&, auto const&) {
          SelectSegment(dropClockSkinGrid_, i);
          auto settings = Services::Instance().config().notifications;
          if (settings.dropClockSkin == chosenSkin) return;
          settings.dropClockSkin = chosenSkin;
          Services::Instance().UpdateNotifications(settings);
        });
        Grid::SetRow(btn, i / 3);
        Grid::SetColumn(btn, i % 3);
        dropClockSkinGrid_.Children().Append(btn);
      }
      Add(dropClockOptionsPanel_, dropClockSkinGrid_);
    }
    Add(visualStack, dropClockOptionsPanel_);

    Expander alertColorExpander;
    alertColorExpander.Header(box_value(L"自定义 3D 时钟与闪光颜色"));
    alertColorExpander.HorizontalAlignment(HorizontalAlignment::Stretch);
    alertColorExpander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    StackPanel alertColorBody;
    alertColorBody.Spacing(8);
    Add(alertColorBody, MakeSegments(
        alertColorTargetSegments_, {L"机身", L"盘面", L"数字 / 指针", L"强调色", L"闪光"},
        [this](int index) {
          alertColorTarget_ = index;
          RefreshAlertColorEditor();
        }));
    alertColorToggle_ = ToggleSwitch();
    Add(alertColorBody, MakeToggleRow(
        alertColorToggle_, L"自定义该项颜色", L"关闭时自动跟随当前皮肤或当前时段颜色"));
    alertColorHint_ = MakeSubtitle(L"");
    Add(alertColorBody, alertColorHint_);
    alertColorPicker_ = ColorPicker();
    alertColorPicker_.IsAlphaEnabled(false);
    alertColorPicker_.IsMoreButtonVisible(false);
    alertColorPicker_.Visibility(Visibility::Collapsed);
    Automation::AutomationProperties::SetName(alertColorPicker_, L"视觉提醒自定义颜色");
    Add(alertColorBody, alertColorPicker_);
    auto resetAlertColors = Button();
    resetAlertColors.Content(box_value(hstring(L"恢复当前皮肤默认颜色")));
    StyleAction(resetAlertColors, false, true);
    resetAlertColors.HorizontalAlignment(HorizontalAlignment::Left);
    resetAlertColors.Click([this](auto const&, auto const&) {
      if (alertColorTimer_) alertColorTimer_.Stop();
      pendingAlertColors_.fill(std::nullopt);
      auto settings = Services::Instance().config().notifications;
      settings.screenEdgeFlashColor.clear();
      settings.dropClockBodyColor.clear();
      settings.dropClockFaceColor.clear();
      settings.dropClockTextColor.clear();
      settings.dropClockAccentColor.clear();
      Services::Instance().UpdateNotifications(settings);
      RefreshAlertColorEditor();
      RefreshVisualPreview();
    });
    Add(alertColorBody, resetAlertColors);
    alertColorExpander.Content(alertColorBody);
    Add(visualStack, alertColorExpander);

    Add(visualStack, MakeDivider());

    auto visualTestRow = StackPanel();
    visualTestRow.Orientation(Orientation::Horizontal);
    visualTestRow.Spacing(10);
    auto testVisual = Button();
    testVisual.Content(box_value(hstring(L"测试视觉提醒")));
    StyleAction(testVisual);
    testVisual.Click([this](auto const&, auto const&) {
      CommitPhaseColors();
      CommitAlertColors();
      const bool triggered = testVisualAlertRequested_ && testVisualAlertRequested_();
      visualTestResultText_.Text(hstring(
          triggered ? L"已触发预览：边缘柔光不遮挡画面，悬停 3D 时钟可暂停收回、点击可立即关闭。"
                    : L"请先开启“全屏边缘闪光提醒”或“掉落 3D 放大时钟提醒”后再预览。"));
    });
    visualTestRow.Children().Append(testVisual);
    Add(visualStack, visualTestRow);
    visualTestResultText_ = MakeSubtitle(
        L"作为系统通知的补充呈现，遵守“启用作息提醒”“临时暂停”及全屏隐藏设置。");
    Add(visualStack, visualTestResultText_);
    visualCard.Child(visualStack);
    Add(notificationsStack, visualCard);

    Add(notificationsStack, MakeCardTitle(L"系统通知"));
    auto checkCard = MakeCard();
    auto checkStack = StackPanel();
    checkStack.Spacing(10);
    Add(checkStack, MakeCardTitle(L"Windows 通知权限"));
    permissionValueText_ = MakeSubtitle(L"");
    Add(checkStack, permissionValueText_);
    Add(checkStack, MakeDivider());

    auto buttons = StackPanel();
    buttons.Spacing(8);
    auto test = Button();
    test.Content(box_value(hstring(L"发送测试通知")));
    StyleAction(test);
    test.Click([this](auto const&, auto const&) {
      const bool ok = ShowTestSystemNotification();
      testResultText_.Text(hstring(ok ? L"已请求系统投递；勿扰或静音时不保证弹出。"
                                      : L"发送失败：请检查系统通知权限。"));
    });
    auto open = Button();
    open.Content(box_value(hstring(L"打开系统通知设置")));
    StyleAction(open, false, true);
    open.Click([](auto const&, auto const&) {
      try {
        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri(L"ms-settings:notifications"));
      } catch (...) {
      }
    });
    Add(buttons, test);
    Add(buttons, open);
    Add(checkStack, buttons);

    testResultText_ = MakeSubtitle(L"尚未发送测试通知。");
    Add(checkStack, testResultText_);
    Add(checkStack, MakeSubtitle(L"系统是否显示横幅取决于 Windows 的通知与勿扰设置。"));

    checkCard.Child(checkStack);
    Add(notificationsStack, checkCard);
  }

  // —— 常规 ——
  auto generalStack = makePage(generalScroll_);
  Add(generalStack, MakeSectionTitle(L"常规"));
  Add(generalStack, MakeSubtitle(L"管理应用启动和本地数据。"));
  {
    // 左列启动与本地数据，右列关于；底部单独放退出按钮。
    auto leftStack = StackPanel();
    leftStack.Spacing(12);

    auto windowCard = MakeCard();
    auto windowStack = StackPanel();
    windowStack.Spacing(10);
    Add(windowStack, MakeCardTitle(L"启动与窗口"));
    startupToggle_ = ToggleSwitch();
    Add(windowStack, MakeToggleRow(startupToggle_, L"登录时自动启动"));
    Add(windowStack,
        MakeSubtitle(L"关闭设置窗口时应用继续后台运行，提醒照常；托盘图标可重新打开设置或退出程序。"));
    windowCard.Child(windowStack);
    Add(leftStack, windowCard);

    auto dataCard = MakeCard();
    auto dataStack = StackPanel();
    dataStack.Spacing(10);
    Add(dataStack, MakeCardTitle(L"本地数据"));
    Add(dataStack, MakeSubtitle(L"配置保存在此设备上，不需要登录账号。"));
    configPathText_ = MakeSubtitle(L"");
    configPathText_.IsTextSelectionEnabled(true);
    Add(dataStack, configPathText_);
    auto openFolder = Button();
    openFolder.Content(box_value(hstring(L"打开配置文件夹")));
    StyleAction(openFolder);
    openFolder.Click([](auto const&, auto const&) {
      const auto path = Services::Instance().dataDirectory().wstring();
      ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    Add(dataStack, openFolder);
    dataCard.Child(dataStack);
    Add(leftStack, dataCard);

    auto aboutCard = MakeCard();
    aboutCard.Padding(Thickness{14, 14, 14, 14});
    aboutCard.VerticalAlignment(VerticalAlignment::Top);
    auto aboutStack = StackPanel();
    aboutStack.Spacing(10);
    Add(aboutStack, MakeCardTitle(L"关于“上课”时间到！"));
    versionText_ = MakeText(L"", 14);
    Add(aboutStack, versionText_);
    Add(aboutStack, MakeSubtitle(L"Windows · C++/WinRT + WinUI 3"));
    aboutCard.Child(aboutStack);
    Add(generalStack, MakeAdaptiveColumns(leftStack, aboutCard, 260, 640));

    auto exitRow = Grid();
    auto exitColumn = ColumnDefinition();
    exitColumn.Width(GridLengthHelper::Auto());
    exitRow.ColumnDefinitions().Append(ColumnDefinition());
    exitRow.ColumnDefinitions().Append(exitColumn);
    auto exit = Button();
    exit.Content(box_value(hstring(L"退出应用")));
    StyleAction(exit, false, true);
    exit.Click([dispatcher = window_.DispatcherQueue(), exitRequested = exitRequested_]
               (auto const&, auto const&) {
      dispatcher.TryEnqueue([exitRequested] { if (exitRequested) exitRequested(); });
    });
    Grid::SetColumn(exit, 1);
    exitRow.Children().Append(exit);
    Add(generalStack, exitRow);
  }

  // —— 编辑浮层 ——
  editorOverlay_ = Grid();
  editorOverlay_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(94, 0, 0, 0)));
  editorOverlay_.Visibility(Visibility::Collapsed);
  root.Children().Append(editorOverlay_);

  editorWorkspace_ = Grid();
  auto workspace = editorWorkspace_;
  workspace.MaxWidth(940);
  workspace.Margin(Thickness{20, 44, 20, 20});
  workspace.ColumnSpacing(12);
  workspace.RowSpacing(10);
  ColumnDefinition formColumn;
  formColumn.Width(GridLengthHelper::FromValueAndType(3, GridUnitType::Star));
  workspace.ColumnDefinitions().Append(formColumn);
  ColumnDefinition previewColumn;
  previewColumn.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
  workspace.ColumnDefinitions().Append(previewColumn);
  RowDefinition mainRow;
  mainRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  workspace.RowDefinitions().Append(mainRow);
  RowDefinition compactPreviewRow;
  compactPreviewRow.Height(GridLengthHelper::Auto());
  workspace.RowDefinitions().Append(compactPreviewRow);
  editorOverlay_.Children().Append(workspace);

  editorCard_ = MakeCard();
  editorCard_.MaxWidth(560);
  editorCard_.MaxHeight(620);
  editorCard_.CornerRadius(CornerRadius{16, 16, 16, 16});
  editorCard_.Padding(Thickness{22, 18, 22, 18});
  editorCard_.Background(ResourceBrush(L"SolidBackgroundFillColorBaseBrush",
                                      Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)));
  workspace.Children().Append(editorCard_);

  auto previewCard = MakeCard();
  previewCard.CornerRadius(CornerRadius{16, 16, 16, 16});
  previewCard.MaxHeight(620);
  previewCard.Background(editorCard_.Background());
  Grid::SetColumn(previewCard, 1);
  workspace.Children().Append(previewCard);
  ScrollViewer previewScroll;
  previewScroll.MaxHeight(500);
  previewScroll.VerticalAlignment(VerticalAlignment::Top);
  previewCard.Child(previewScroll);
  editorPreview_ = StackPanel();
  editorPreview_.Spacing(10);
  previewScroll.Content(editorPreview_);

  workspace.SizeChanged([previewCard, previewColumn, previewScroll](auto const&,
                                                      SizeChangedEventArgs const& args) {
    const bool compact = args.NewSize().Width < 720;
    previewColumn.Width(GridLengthHelper::FromValueAndType(
        compact ? 0 : 2, compact ? GridUnitType::Pixel : GridUnitType::Star));
    Grid::SetRow(previewCard, compact ? 1 : 0);
    Grid::SetColumn(previewCard, compact ? 0 : 1);
    previewCard.Height(compact ? 176 : std::numeric_limits<double>::quiet_NaN());
    previewScroll.MaxHeight(compact ? 142 : 500);
  });

  Grid editorLayout;
  editorLayout.RowSpacing(14);
  for (const auto height : {GridLengthHelper::Auto(),
                            GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                            GridLengthHelper::Auto()}) {
    RowDefinition row;
    row.Height(height);
    editorLayout.RowDefinitions().Append(row);
  }
  editorCard_.Child(editorLayout);

  StackPanel editorHeading;
  editorHeading.Spacing(5);
  editorTitle_ = MakeText(L"编辑时段", 22);
  editorTitle_.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  Add(editorHeading, editorTitle_);
  editorScope_ = RadioButtons();
  editorScope_.MaxColumns(2);
  editorScope_.Items().Append(box_value(L"仅今天"));
  editorScope_.Items().Append(box_value(L"永久修改模板"));
  Automation::AutomationProperties::SetName(editorScope_, L"保存范围");
  Add(editorHeading, editorScope_);
  editorScopeHint_ = MakeSubtitle(L"");
  Add(editorHeading, editorScopeHint_);
  editorLayout.Children().Append(editorHeading);

  ScrollViewer editorScroll;
  Grid::SetRow(editorScroll, 1);
  editorLayout.Children().Append(editorScroll);
  StackPanel editorStack;
  editorStack.Spacing(12);
  editorScroll.Content(editorStack);

  Grid identityRow;
  identityRow.ColumnSpacing(12);
  ColumnDefinition nameColumn;
  nameColumn.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
  identityRow.ColumnDefinitions().Append(nameColumn);
  ColumnDefinition kindColumn;
  kindColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  identityRow.ColumnDefinitions().Append(kindColumn);
  editorName_ = TextBox();
  editorName_.Header(box_value(hstring(L"名称")));
  editorName_.PlaceholderText(L"例如：第 3 节 / 午休");
  identityRow.Children().Append(editorName_);

  editorKind_ = ComboBox();
  editorKind_.Header(box_value(hstring(L"类型")));
  editorKind_.HorizontalAlignment(HorizontalAlignment::Stretch);
  for (const wchar_t* kind : {L"上课", L"课间", L"长休息", L"午休"}) {
    editorKind_.Items().Append(box_value(hstring(kind)));
  }
  Grid::SetColumn(editorKind_, 1);
  identityRow.Children().Append(editorKind_);
  Add(editorStack, identityRow);

  auto timeRow = Grid();
  auto timeCol1 = ColumnDefinition();
  timeCol1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  auto timeCol2 = ColumnDefinition();
  timeCol2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  timeRow.ColumnDefinitions().Append(timeCol1);
  timeRow.ColumnDefinitions().Append(timeCol2);
  timeRow.ColumnSpacing(12);
  editorStart_ = TextBox();
  editorStart_.Header(box_value(hstring(L"开始（HH:mm）")));
  editorStart_.PlaceholderText(L"10:00");
  editorEnd_ = TextBox();
  editorEnd_.Header(box_value(hstring(L"结束（HH:mm）")));
  editorEnd_.PlaceholderText(L"10:45");
  Grid::SetColumn(editorEnd_, 1);
  timeRow.Children().Append(editorStart_);
  timeRow.Children().Append(editorEnd_);
  Add(editorStack, timeRow);

  Grid durationRow;
  durationRow.ColumnSpacing(16);
  ColumnDefinition minutesColumn;
  minutesColumn.Width(GridLengthHelper::FromValueAndType(120, GridUnitType::Pixel));
  durationRow.ColumnDefinitions().Append(minutesColumn);
  ColumnDefinition sliderColumn;
  sliderColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  durationRow.ColumnDefinitions().Append(sliderColumn);
  editorMinutes_ = TextBox();
  editorMinutes_.Header(box_value(L"时长（分钟）"));
  durationRow.Children().Append(editorMinutes_);
  editorSlider_ = Slider();
  editorSlider_.Minimum(5);
  editorSlider_.Maximum(180);
  editorSlider_.StepFrequency(5);
  editorSlider_.VerticalAlignment(VerticalAlignment::Bottom);
  Automation::AutomationProperties::SetName(editorSlider_, L"调整时长（每格 5 分钟）");
  Grid::SetColumn(editorSlider_, 1);
  durationRow.Children().Append(editorSlider_);
  Add(editorStack, durationRow);
  editorSliderHint_ = MakeSubtitle(L"");
  Add(editorStack, editorSliderHint_);

  editorStrategy_ = RadioButtons();
  editorStrategy_.Header(box_value(L"后续时段"));
  editorStrategy_.MaxColumns(2);
  editorStrategy_.Items().Append(box_value(L"顺延后续时段"));
  editorStrategy_.Items().Append(box_value(L"保持原时间"));
  StackPanel editorFooter;
  editorFooter.Spacing(8);
  Grid::SetRow(editorFooter, 2);
  editorLayout.Children().Append(editorFooter);
  Add(editorFooter, editorStrategy_);
  editorError_ = InfoBar();
  editorError_.Severity(InfoBarSeverity::Error);
  editorError_.IsOpen(false);
  editorError_.IsClosable(false);
  Add(editorFooter, editorError_);

  Grid actions;
  auto editorButtons = StackPanel();
  editorButtons.Orientation(Orientation::Horizontal);
  editorButtons.Spacing(8);
  editorButtons.HorizontalAlignment(HorizontalAlignment::Right);
  deleteButton_ = Button();
  deleteButton_.Content(box_value(L"删除时段"));
  StyleAction(deleteButton_, false, true);
  deleteButton_.HorizontalAlignment(HorizontalAlignment::Left);
  auto cancel = Button();
  cancel.Content(box_value(hstring(L"取消")));
  StyleAction(cancel, false, true);
  saveButton_ = Button();
  saveButton_.Content(box_value(hstring(L"保存调整")));
  StyleAction(saveButton_, true);
  Add(editorButtons, cancel);
  Add(editorButtons, saveButton_);
  actions.Children().Append(deleteButton_);
  actions.Children().Append(editorButtons);
  Add(editorFooter, actions);

  // —— 事件 ——
  nav_.SelectionChanged([this](auto const&, NavigationViewSelectionChangedEventArgs const& args) {
    if (args.SelectedItem() == nullptr) return;
    if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
      ShowSection(std::wstring(unbox_value<hstring>(item.Tag()).c_str()));
    }
  });
  templateCombo_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    const int index = templateCombo_.SelectedIndex();
    const auto& config = Services::Instance().config();
    if (index < 0 || index >= static_cast<int>(config.templates.size())) return;
    const std::string templateId = config.templates[index].id;
    CommitTemplateName();
    std::string error;
    if (!Services::Instance().SelectTemplate(templateId, &error)) return;
    RefreshAll();
  });
  planScope_.SelectionChanged([this](auto const&, auto const&) {
    if (!syncing_) RefreshSegments();
  });
  monitorCombo_.GotFocus([this](auto const&, auto const&) { RefreshDisplayMonitors(); });
  monitorCombo_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    const auto item = monitorCombo_.SelectedItem().try_as<ComboBoxItem>();
    if (!item) return;
    const auto device = monitorCombo_.SelectedIndex() == 0
                            ? std::string{}
                            : ToUtf8(std::wstring(unbox_value<hstring>(item.Tag()).c_str()));
    auto presentation = Services::Instance().config().presentation;
    if (presentation.monitorId == device) return;
    presentation.monitorId = device;
    Services::Instance().UpdatePresentation(presentation);
  });
  screenEdgeSlider_.ValueChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    pendingThickness_ = static_cast<int>(std::lround(args.NewValue()));
    RefreshDisplayPreview(Services::Instance().config().presentation);
    // 拖动期间预览即时更新，短暂停顿后再合并保存和桌面边框刷新。
    if (!thicknessTimer_) {
      thicknessTimer_ = window_.DispatcherQueue().CreateTimer();
      thicknessTimer_.IsRepeating(false);
      thicknessTimer_.Interval(std::chrono::milliseconds(150));
      thicknessTimer_.Tick([this](auto const&, auto const&) { CommitScreenEdgeThickness(); });
    }
    thicknessTimer_.Stop();
    thicknessTimer_.Start();
  });
  visibleToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.visible = visibleToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  hideInFullscreenToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                          [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.hideInFullscreen == hideInFullscreenToggle_.IsOn()) return;
    presentation.hideInFullscreen = hideInFullscreenToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  coverTaskbarToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.coverTaskbar == coverTaskbarToggle_.IsOn()) return;
    presentation.coverTaskbar = coverTaskbarToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  screenEdgeGradientToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                            [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.screenEdgeGradient == screenEdgeGradientToggle_.IsOn()) return;
    presentation.screenEdgeGradient = screenEdgeGradientToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  screenEdgeHighlightToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                             [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.screenEdgeHighlight == screenEdgeHighlightToggle_.IsOn()) return;
    presentation.screenEdgeHighlight = screenEdgeHighlightToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  edgeAutoHideToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.autoHideAtEdge = edgeAutoHideToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  flushToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                               [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.flushWithEdge = flushToggle_.IsOn();
    Services::Instance().UpdatePresentation(presentation);
  });
  barLengthSlider_.ValueChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    pendingBarLength_ = static_cast<int>(std::lround(args.NewValue()));
    barLengthValue_.Text(std::to_wstring(*pendingBarLength_));
    RefreshDisplayPreview(Services::Instance().config().presentation);
    // 拖动期间预览即时更新，停顿后合并保存与浮窗尺寸调整。
    if (!barLengthTimer_) {
      barLengthTimer_ = window_.DispatcherQueue().CreateTimer();
      barLengthTimer_.IsRepeating(false);
      barLengthTimer_.Interval(std::chrono::milliseconds(150));
      barLengthTimer_.Tick([this](auto const&, auto const&) { CommitBarLength(); });
    }
    barLengthTimer_.Stop();
    barLengthTimer_.Start();
  });
  barBackgroundToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                       [this](auto const&, auto const&) {
    if (syncing_) return;
    if (barColorTimer_) barColorTimer_.Stop();
    pendingBarBackground_.reset();
    auto presentation = Services::Instance().config().presentation;
    if (barBackgroundToggle_.IsOn()) {
      barColorPicker_.Visibility(Visibility::Visible);
      // 以当前系统背景为起点，避免开启取色器后颜色跳变。
      if (presentation.barBackground.empty()) {
        const auto color = SystemBackgroundColor();
        const bool wasSyncing = std::exchange(syncing_, true);
        barColorPicker_.Color(color);
        syncing_ = wasSyncing;
        presentation.barBackground = WidgetAppearance::ColorToHex(color);
        Services::Instance().UpdatePresentation(presentation);
      }
      return;
    }
    barColorPicker_.Visibility(Visibility::Collapsed);
    if (presentation.barBackground.empty()) return;
    presentation.barBackground.clear();
    Services::Instance().UpdatePresentation(presentation);
  });
  barColorPicker_.ColorChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    pendingBarBackground_ = WidgetAppearance::ColorToHex(args.NewColor());
    RefreshDisplayPreview(Services::Instance().config().presentation);
    if (!barColorTimer_) {
      barColorTimer_ = window_.DispatcherQueue().CreateTimer();
      barColorTimer_.IsRepeating(false);
      barColorTimer_.Interval(std::chrono::milliseconds(200));
      barColorTimer_.Tick([this](auto const&, auto const&) { CommitBarBackground(); });
    }
    barColorTimer_.Stop();
    barColorTimer_.Start();
  });
  phaseColorToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    const int target = std::clamp(phaseColorTarget_, 0, 2);
    if (phaseColorToggle_.IsOn()) {
      const auto defPalette = WidgetAppearance::CurrentPalette();
      const auto fallbackColor =
          target == 0 ? defPalette.accent : (target == 1 ? defPalette.rest : defPalette.lunch);
      const std::string initialHex = pendingPhaseColors_[target].value_or(
          WidgetAppearance::ColorToHex(fallbackColor));
      pendingPhaseColors_[target] = initialHex;
      RefreshPhaseColorEditor();
      RefreshDisplayPreview(Services::Instance().config().presentation);
      CommitPhaseColors();
      return;
    }
    pendingPhaseColors_[target] = std::string{};
    RefreshPhaseColorEditor();
    RefreshDisplayPreview(Services::Instance().config().presentation);
    CommitPhaseColors();
  });
  phaseColorPicker_.ColorChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    const int target = std::clamp(phaseColorTarget_, 0, 2);
    pendingPhaseColors_[target] = WidgetAppearance::ColorToHex(args.NewColor());
    phaseColorHint_.Text(hstring(L"当前使用自定义颜色：#" + ToWide(*pendingPhaseColors_[target])));
    RefreshDisplayPreview(Services::Instance().config().presentation);
    if (!phaseColorTimer_) {
      phaseColorTimer_ = window_.DispatcherQueue().CreateTimer();
      phaseColorTimer_.IsRepeating(false);
      phaseColorTimer_.Interval(std::chrono::milliseconds(180));
      phaseColorTimer_.Tick([this](auto const&, auto const&) { CommitPhaseColors(); });
    }
    phaseColorTimer_.Stop();
    phaseColorTimer_.Start();
  });
  templateNameBox_.TextChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    const std::wstring name = TextOf(templateNameBox_);
    if (templateNameTimer_) templateNameTimer_.Stop();
    if (name == templateRenameBase_) {
      pendingTemplateId_.clear();
      pendingTemplateName_.clear();
      return;
    }
    pendingTemplateId_ = Services::Instance().config().selectedTemplateId;
    pendingTemplateName_ = name;
    if (name.empty()) return;
    if (!templateNameTimer_) {
      templateNameTimer_ = window_.DispatcherQueue().CreateTimer();
      templateNameTimer_.IsRepeating(false);
      templateNameTimer_.Interval(std::chrono::milliseconds(600));
      templateNameTimer_.Tick([this](auto const&, auto const&) { CommitTemplateName(); });
    }
    templateNameTimer_.Start();
  });
  timelineZoomSlider_.ValueChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    timelinePxPerHour_ = std::max(20.0, args.NewValue());
    // 拖动进行中不重建，避免拆掉正在操作的可视树。
    if (timelineDragMode_ != TimelineDragMode::None) return;
    const bool permanent = planScope_.SelectedIndex() != 1;
    RebuildTimelineBlocks(classtime::SortedByStart(EditorBaseSegments(permanent)));
  });
  timelineSnapCombo_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    const int index = timelineSnapCombo_.SelectedIndex();
    if (index < 0 || index > 2) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.timelineSnapMinutes = index == 0 ? 1 : index == 2 ? 15 : 5;
    Services::Instance().UpdatePresentation(presentation);
  });
  // 时间轴拖动：指针事件统一挂在画布上，块只负责发起。
  timelineCanvas_.PointerMoved([this](auto const&, Input::PointerRoutedEventArgs const& args) {
    if (timelineDragMode_ == TimelineDragMode::None || !timelineDragPointer_ ||
        args.Pointer().PointerId() != *timelineDragPointer_) return;
    const int minuteAtPointer =
        TimelineMinute(args.GetCurrentPoint(timelineCanvas_).Position().X);
    const int step = Services::Instance().config().presentation.timelineSnapMinutes;
    const auto range = DraggedTimelineRange({timelineDragStart_, timelineDragEnd_},
                                             timelineDragMode_,
                                             minuteAtPointer - timelineDragAnchorMinute_, step);
    const int newStart = range.start;
    const int newEnd = range.end;
    timelineDragCurrentStart_ = newStart;
    timelineDragCurrentEnd_ = newEnd;

    auto block = std::find_if(timelineBlocks_.begin(), timelineBlocks_.end(),
                              [this](const TimelineBlock& item) {
                                return item.id == timelineDragId_;
                              });
    if (block == timelineBlocks_.end()) return;
    const double pxPerMinute = timelinePxPerHour_ / 60.0;
    Canvas::SetLeft(block->host, TimelineX(newStart));
    block->host.Width(std::max(4.0, (newEnd - newStart) * pxPerMinute));
    block->startMinute = newStart;
    block->endMinute = newEnd;
    const classtime::TimeOfDay rangeStart = *classtime::TimeOfDay::FromTotalMinutes(newStart);
    const classtime::TimeOfDay rangeEnd = *classtime::TimeOfDay::FromTotalMinutes(newEnd);
    block->sub.Text(hstring(FormatClock(rangeStart) + L"–" + FormatClock(rangeEnd)));
    // 与其他时段重叠时给出即时反馈，松手仍按规则拦截。
    const auto segments = EditorBaseSegments(planScope_.SelectedIndex() != 1);
    const auto dragged = std::find_if(segments.begin(), segments.end(),
                                      [this](const classtime::Segment& other) {
                                        return other.id == timelineDragId_;
                                      });
    const auto kindColor = SegmentColor(
        dragged != segments.end() ? dragged->kind : classtime::SegmentKind::Focus);
    const bool overlap = std::any_of(segments.begin(), segments.end(),
                                     [this, newStart, newEnd](const classtime::Segment& other) {
                                       return other.id != timelineDragId_ &&
                                              newStart < other.end.TotalMinutes() &&
                                              other.start.TotalMinutes() < newEnd;
                                     });
    block->host.BorderBrush(SolidColorBrush(
        overlap ? Windows::UI::ColorHelper::FromArgb(255, 188, 53, 66) : kindColor));
    timelineStatusText_.Text(hstring(
        (overlap ? std::wstring(L"⚠ 与其他时段重叠，松手将恢复原时间。")
                 : L"正在调整（步进 " + std::to_wstring(step) + L" 分钟）：") +
        FormatClock(rangeStart) + L"–" + FormatClock(rangeEnd)));
    args.Handled(true);
  });
  timelineCanvas_.PointerReleased([this](auto const&, Input::PointerRoutedEventArgs const& args) {
    if (!timelineDragPointer_ || args.Pointer().PointerId() != *timelineDragPointer_) return;
    FinishTimelineDrag(true);
    args.Handled(true);
  });
  const auto cancelTimelineDrag = [this](auto const&, Input::PointerRoutedEventArgs const& args) {
    if (timelineDragPointer_ && args.Pointer().PointerId() == *timelineDragPointer_) {
      FinishTimelineDrag(false);
    }
  };
  timelineCanvas_.PointerCaptureLost(cancelTimelineDrag);
  timelineCanvas_.PointerCanceled(cancelTimelineDrag);
  timelineCanvas_.DoubleTapped([this](auto const&, Input::DoubleTappedRoutedEventArgs const& args) {
    if (timelineDragMode_ != TimelineDragMode::None) return;
    const double x = args.GetPosition(timelineCanvas_).X;
    const int step = Services::Instance().config().presentation.timelineSnapMinutes;
    const int minute = std::max(0, SnapToTimelineStep(TimelineMinute(x), step));
    if (minute >= 1439) {
      ShowScheduleNotice(L"所选时间太晚，请选择 23:58 或更早的时间新增时段。", true);
      return;
    }
    OpenNewEditorAt(minute);
    args.Handled(true);
  });
  notificationsToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    auto settings = Services::Instance().config().notifications;
    settings.enabled = notificationsToggle_.IsOn();
    Services::Instance().UpdateNotifications(settings);
  });
  muteToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    Services::Instance().SetMuted(muteToggle_.IsOn());
  });
  auto categoryHandler = [this](auto const&, auto const&) {
    if (syncing_) return;
    auto settings = Services::Instance().config().notifications;
    settings.focusBoundaries = focusToggle_.IsOn();
    settings.restBoundaries = restToggle_.IsOn();
    settings.lunchBoundaries = lunchToggle_.IsOn();
    Services::Instance().UpdateNotifications(settings);
  };
  focusToggle_.Toggled(categoryHandler);
  restToggle_.Toggled(categoryHandler);
  lunchToggle_.Toggled(categoryHandler);
  screenEdgeFlashToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                         [this](auto const&, auto const&) {
    if (syncing_) return;
    auto settings = Services::Instance().config().notifications;
    if (settings.screenEdgeFlash == screenEdgeFlashToggle_.IsOn()) return;
    settings.screenEdgeFlash = screenEdgeFlashToggle_.IsOn();
    Services::Instance().UpdateNotifications(settings);
  });
  dropClockToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                     [this](auto const&, auto const&) {
    if (syncing_) return;
    auto settings = Services::Instance().config().notifications;
    if (settings.dropClockAlert == dropClockToggle_.IsOn()) return;
    settings.dropClockAlert = dropClockToggle_.IsOn();
    Services::Instance().UpdateNotifications(settings);
  });
  alertColorToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    const int target = std::clamp(alertColorTarget_, 0, 4);
    if (alertColorToggle_.IsOn()) {
      const auto& config = Services::Instance().config();
      const auto preset = WidgetAppearance::GetClockSkinPreset(config.notifications.dropClockSkin);
      const auto palette = WidgetAppearance::ResolvePalette(config.presentation);
      const auto snapColor = Services::Instance().snapshot().current
                                 ? WidgetAppearance::KindColor(
                                       palette, Services::Instance().snapshot().current->kind)
                                 : palette.accent;
      Windows::UI::Color fallbackColor = snapColor;
      if (target == 0) {
        fallbackColor = preset.body;
      } else if (target == 1) {
        fallbackColor = preset.face;
      } else if (target == 2) {
        const std::string faceHex =
            pendingAlertColors_[1].value_or(config.notifications.dropClockFaceColor);
        if (!faceHex.empty()) {
          const auto face = WidgetAppearance::ParseHexColor(faceHex);
          fallbackColor = WidgetAppearance::IsLightColor(face)
                              ? Windows::UI::ColorHelper::FromArgb(255, 22, 27, 38)
                              : Windows::UI::ColorHelper::FromArgb(255, 244, 248, 255);
        } else {
          fallbackColor = preset.faceText;
        }
      }
      pendingAlertColors_[target] = pendingAlertColors_[target].value_or(
          WidgetAppearance::ColorToHex(fallbackColor));
      RefreshAlertColorEditor();
      RefreshVisualPreview();
      CommitAlertColors();
      return;
    }
    pendingAlertColors_[target] = std::string{};
    RefreshAlertColorEditor();
    RefreshVisualPreview();
    CommitAlertColors();
  });
  alertColorPicker_.ColorChanged([this](auto const&, auto const& args) {
    if (syncing_) return;
    const int target = std::clamp(alertColorTarget_, 0, 4);
    pendingAlertColors_[target] = WidgetAppearance::ColorToHex(args.NewColor());
    alertColorHint_.Text(hstring(L"当前使用自定义颜色：#" + ToWide(*pendingAlertColors_[target])));
    RefreshVisualPreview();
    if (!alertColorTimer_) {
      alertColorTimer_ = window_.DispatcherQueue().CreateTimer();
      alertColorTimer_.IsRepeating(false);
      alertColorTimer_.Interval(std::chrono::milliseconds(180));
      alertColorTimer_.Tick([this](auto const&, auto const&) { CommitAlertColors(); });
    }
    alertColorTimer_.Stop();
    alertColorTimer_.Start();
  });

  startupToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    const bool want = startupToggle_.IsOn();
    bool applied = false;
    try {
      auto task = Windows::ApplicationModel::StartupTask::GetAsync(L"ClassTimeStartup").get();
      if (want) {
        const auto state = task.RequestEnableAsync().get();
        applied = state == Windows::ApplicationModel::StartupTaskState::Enabled ||
                  state == Windows::ApplicationModel::StartupTaskState::EnabledByPolicy;
      } else {
        task.Disable();
        applied = true;
      }
    } catch (...) {
      applied = false;
    }
    syncing_ = true;
    startupToggle_.IsOn(want && applied);
    syncing_ = false;
    Services::Instance().SetLaunchAtLogin(want && applied);
  });

  editorOverlay_.KeyDown([this](auto const&, Input::KeyRoutedEventArgs const& args) {
    if (args.Key() == Windows::System::VirtualKey::Escape) {
      CloseEditor();
      args.Handled(true);
    }
  });
  editorScope_.SelectionChanged([this](auto const&, auto const&) {
    if (editorOpen_ && !syncing_) UpdateEditorHints();
  });
  const auto refreshPreview = [this](auto const&, auto const&) {
    if (editorOpen_ && !syncing_) RefreshEditorPreview();
  };
  editorName_.TextChanged(refreshPreview);
  editorStrategy_.SelectionChanged(refreshPreview);
  editorKind_.SelectionChanged([this](auto const&, auto const&) {
    if (!editorOpen_ || syncing_) return;
    syncing_ = true;
    editorSlider_.Maximum(editorKind_.SelectedIndex() == 0 ? 180 : 240);
    if (const auto minutes = ParseDuration(editorMinutes_)) {
      editorSlider_.Value(std::clamp<double>(*minutes, editorSlider_.Minimum(),
                                            editorSlider_.Maximum()));
    }
    syncing_ = false;
    UpdateEditorHints();
  });
  editorStart_.TextChanged([this](auto const&, auto const&) { UpdateEditorEnd(); });
  editorMinutes_.TextChanged([this](auto const&, auto const&) { UpdateEditorEnd(); });
  editorEnd_.TextChanged([this](auto const&, auto const&) {
    if (!editorOpen_ || syncing_) return;
    const auto start = ParseBox(editorStart_);
    const auto end = ParseBox(editorEnd_);
    if (!start || !end || end->TotalMinutes() <= start->TotalMinutes()) {
      RefreshEditorPreview();
      return;
    }
    SyncDurationControls(end->TotalMinutes() - start->TotalMinutes());
    editorError_.IsOpen(false);
    UpdateEditorHints();
  });
  editorSlider_.ValueChanged([this](auto const&, auto const&) {
    if (!editorOpen_ || syncing_) return;
    SetText(editorMinutes_, std::to_wstring(static_cast<int>(std::lround(editorSlider_.Value()))));
  });
  saveButton_.Click([this](auto const&, auto const&) {
    if (!editorOpen_) return;
    const auto start = ParseBox(editorStart_);
    const auto end = ParseBox(editorEnd_);
    const std::wstring name = TextOf(editorName_);
    if (!start.has_value() || !end.has_value()) {
      editorError_.Message(hstring(L"时间格式应为 HH:mm，例如 10:05。"));
      editorError_.IsOpen(true);
      return;
    }
    if (name.empty()) {
      editorError_.Message(hstring(L"名称不能为空。"));
      editorError_.IsOpen(true);
      return;
    }
    if (end->TotalMinutes() <= start->TotalMinutes()) {
      editorError_.Message(hstring(L"结束时间必须晚于开始时间。"));
      editorError_.IsOpen(true);
      return;
    }
    if (const auto minutes = ParseDuration(editorMinutes_);
        !minutes || *minutes != end->TotalMinutes() - start->TotalMinutes()) {
      editorError_.Message(L"请输入有效的整分钟时长，并确认结束时间在当天 23:59 之前。");
      editorError_.IsOpen(true);
      return;
    }
    const bool permanent = editorScope_.SelectedIndex() == 1;
    auto base = EditorBaseSegments(permanent);
    if (!newSegment_ && !classtime::FindSegmentById(base, editingId_)) {
      editorError_.Message(L"所选计划里没有这个时段。请选择原来的保存范围，或在目标计划中新增时段。");
      editorError_.IsOpen(true);
      return;
    }
    const classtime::Segment draft{editingId_, ToUtf8(name), IndexToKind(editorKind_.SelectedIndex()),
                                   *start, *end};
    const auto outcome = EvaluateDraft(base, draft);
    if (!outcome.ok()) {
      std::wstring message = ToWide(outcome.message);
      if (!outcome.conflictIds.empty()) {
        message += L"（冲突：" + SegmentName(base, outcome.conflictIds.front());
        for (size_t i = 1; i < outcome.conflictIds.size(); ++i) {
          message += L"、" + SegmentName(base, outcome.conflictIds[i]);
        }
        message += L"）";
      }
      editorError_.Message(hstring(message));
      editorError_.IsOpen(true);
      return;
    }
    std::string error;
    const auto scope = permanent ? Services::SaveScope::Permanent : Services::SaveScope::TodayOnly;
    if (!Services::Instance().ApplyScheduleEdit(outcome.segments, scope, &error)) {
      editorError_.Message(hstring(ToWide(error)));
      editorError_.IsOpen(true);
      return;
    }
    ShowScheduleNotice(permanent ? L"模板已保存，并会应用到设定的生效星期。"
                                 : L"今日计划已更新；常规模板未改变。");
    CloseEditor();
    RefreshAll();
  });
  deleteButton_.Click([this](auto const&, auto const&) {
    if (!editorOpen_ || newSegment_) return;
    ConfirmEditorDelete();
  });
  cancel.Click([this](auto const&, auto const&) { CloseEditor(); });

  // 星期
  const wchar_t* names[] = {L"周一", L"周二", L"周三", L"周四", L"周五", L"周六", L"周日"};
  for (int day = 1; day <= 7; ++day) {
    CheckBox box;
    box.Content(box_value(hstring(names[day - 1])));
    box.MinWidth(0);
    box.Tag(box_value(day));
    box.Checked([this](auto const&, auto const&) {
      if (!syncing_) CommitWeekdays();
    });
    box.Unchecked([this](auto const&, auto const&) {
      if (!syncing_) CommitWeekdays();
    });
    weekdayPanel_.Children().Append(box);
  }

  nav_.SelectedItem(nav_.MenuItems().GetAt(0));
  RefreshAll();
}

void SettingsWindow::Show() {
  RefreshAll();
  window_.Activate();
  window_.Activate();
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
  }
  if (requestedSection_ == displayScroll_) StartPreviewRefresh();
}

void SettingsWindow::ApplyState(bool presentationOnly) {
  if (presentationOnly) {
    const bool wasSyncing = std::exchange(syncing_, true);
    RefreshDisplayControls();
    syncing_ = wasSyncing;
    return;
  }
  if (editorOpen_) {
    RefreshNotificationControls();
    return;
  }
  RefreshAll();
}

void SettingsWindow::RefreshAll() {
  syncing_ = true;
  RefreshTemplates();
  RefreshWeekdays();
  RefreshSegments();
  RefreshDisplayControls();
  RefreshNotificationControls();
  RefreshGeneralControls();
  syncing_ = false;
}

void SettingsWindow::RefreshTemplates() {
  const auto& config = Services::Instance().config();
  restorePresetButton_.IsEnabled(config.selectedTemplateId == "high-school" ||
                                 config.selectedTemplateId == "university");
  int selected = 0;
  int index = 0;
  for (const auto& schedule : config.templates) {
    if (schedule.id == config.selectedTemplateId) selected = index;
    ++index;
  }
  // 选择事件尚未结束时不要拆除下拉项；名称变更仅更新已有项目。
  if (!templateCombo_.IsDropDownOpen()) {
    if (templateCombo_.Items().Size() != config.templates.size()) {
      templateCombo_.Items().Clear();
      for (const auto& schedule : config.templates) {
        ComboBoxItem item;
        item.Content(box_value(hstring(ToWide(schedule.name))));
        item.Tag(box_value(hstring(ToWide(schedule.id))));
        templateCombo_.Items().Append(item);
      }
    } else {
      for (uint32_t i = 0; i < config.templates.size(); ++i) {
        auto item = templateCombo_.Items().GetAt(i).as<ComboBoxItem>();
        item.Content(box_value(hstring(ToWide(config.templates[i].name))));
        item.Tag(box_value(hstring(ToWide(config.templates[i].id))));
      }
    }
    templateCombo_.SelectedIndex(config.templates.empty() ? -1 : selected);
  }

  // 模板重命名输入框与当前模板名称保持一致。
  if (const auto* schedule = classtime::SelectedTemplate(config)) {
    templateRenameBase_ = ToWide(schedule->name);
    if (templateNameBox_ && pendingTemplateId_ != schedule->id &&
        TextOf(templateNameBox_) != templateRenameBase_) {
      templateNameBox_.Text(hstring(templateRenameBase_));
    }
  }

  const auto now = NowClock();
  if (Services::Instance().usingTodayOverride()) {
    templateStatus_.Text(hstring(L"今天使用临时调整，常规模板未被改写。"));
  } else if (const auto* schedule = classtime::SelectedTemplate(config)) {
    if (!schedule->IsActiveOn(now.Weekday())) {
      templateStatus_.Text(hstring(L"今天不是该模板的生效日，悬浮层会显示“今天没有安排”。"));
    } else {
      templateStatus_.Text(hstring(L"今天按常规模板执行。修改后立即按新时段重新定位。"));
    }
  } else {
    templateStatus_.Text(hstring(L"没有可用的模板。"));
  }
}

void SettingsWindow::RefreshWeekdays() {
  const auto* schedule = classtime::SelectedTemplate(Services::Instance().config());
  for (const auto& child : weekdayPanel_.Children()) {
    if (auto check = child.try_as<CheckBox>()) {
      const int day = unbox_value<int>(check.Tag());
      check.IsChecked(schedule != nullptr && schedule->IsActiveOn(day));
    }
  }
}

void SettingsWindow::RefreshSegments() {
  const bool permanent = planScope_.SelectedIndex() != 1;
  const bool wasSyncing = std::exchange(syncing_, true);
  const int snap = Services::Instance().config().presentation.timelineSnapMinutes;
  timelineSnapCombo_.SelectedIndex(snap == 1 ? 0 : snap == 15 ? 2 : 1);
  syncing_ = wasSyncing;
  segmentList_.Children().Clear();
  auto sorted = classtime::SortedByStart(EditorBaseSegments(permanent));
  RefreshTimetable(sorted, permanent);
  RefreshTimeline(sorted);
  RefreshTodayOverview();
  // “撤销今日调整”只在确有临时调整时可用，并说明原因。
  const bool hasOverride = Services::Instance().usingTodayOverride();
  clearTodayButton_.IsEnabled(hasOverride);
  ToolTipService::SetToolTip(clearTodayButton_, box_value(hstring(
      hasOverride ? L"撤销今天的临时覆盖，恢复按常规模板执行" : L"今天没有临时调整可撤销")));
  if (sorted.empty()) {
    auto emptyRow = StackPanel();
    emptyRow.Spacing(8);
    Add(emptyRow, MakeSubtitle(permanent ? L"模板还没有安排时段。" : L"今天没有安排的时段。"));
    auto addRow = Button();
    addRow.Content(box_value(hstring(L"添加时段")));
    StyleAction(addRow, true);
    addRow.Click([this](auto const&, auto const&) { OpenNewEditor(); });
    Add(emptyRow, addRow);
    Add(segmentList_, emptyRow);
  }
  int ordinal = 0;
  std::wstring previousGroup;
  for (const auto& segment : sorted) {
    if (segment.kind == classtime::SegmentKind::Focus) ++ordinal;

    const auto group = segment.kind == classtime::SegmentKind::Lunch ? std::wstring(L"午休")
                     : segment.start.hour < 12 ? std::wstring(L"上午")
                     : segment.start.hour < 18 ? std::wstring(L"下午")
                                               : std::wstring(L"晚上");
    if (group != previousGroup) {
      auto heading = MakeText(group, 14, SolidColorBrush(SegmentColor(segment.kind)));
      heading.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
      heading.Margin(Thickness{2, previousGroup.empty() ? 2.0 : 14.0, 0, 3});
      Add(segmentList_, heading);
      previousGroup = group;
    }

    auto row = Border();
    row.CornerRadius(CornerRadius{9, 9, 9, 9});
    row.Padding(Thickness{12, 8, 12, 8});
    auto rowColor = SegmentColor(segment.kind);
    rowColor.A = 18;
    row.Background(SolidColorBrush(rowColor));
    rowColor.A = 48;
    row.BorderBrush(SolidColorBrush(rowColor));
    row.BorderThickness(Thickness{1, 1, 1, 1});

    auto grid = Grid();
    auto colBadge = ColumnDefinition();
    colBadge.Width(GridLengthHelper::Auto());
    auto colText = ColumnDefinition();
    colText.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto colAction = ColumnDefinition();
    colAction.Width(GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(colBadge);
    grid.ColumnDefinitions().Append(colText);
    grid.ColumnDefinitions().Append(colAction);
    grid.ColumnSpacing(12);

    auto badge = Border();
    badge.Width(52);
    badge.Padding(Thickness{0, 3, 0, 3});
    badge.CornerRadius(CornerRadius{10, 10, 10, 10});
    badge.VerticalAlignment(VerticalAlignment::Center);
    const auto kindColor = SegmentColor(segment.kind);
    auto softColor = kindColor;
    softColor.A = 38;
    badge.Background(SolidColorBrush(softColor));
    auto badgeText = MakeText(
        segment.kind == classtime::SegmentKind::Focus
            ? L"第 " + std::to_wstring(ordinal) + L" 节"
            : SegmentKindLabel(segment.kind),
        11);
    badgeText.Foreground(SolidColorBrush(kindColor));
    badgeText.HorizontalAlignment(HorizontalAlignment::Center);
    badge.Child(badgeText);
    grid.Children().Append(badge);

    auto text = StackPanel();
    text.VerticalAlignment(VerticalAlignment::Center);
    text.Spacing(1);
    auto name = MakeText(ToWide(segment.label), 14);
    name.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    auto detail = MakeSubtitle(FormatRange(segment) + L" · " + FormatMinutes(segment.DurationMinutes()));
    text.Children().Append(name);
    text.Children().Append(detail);
    Grid::SetColumn(text, 1);
    grid.Children().Append(text);

    auto actions = StackPanel();
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);
    auto edit = Button();
    edit.Content(box_value(hstring(L"编辑")));
    StyleAction(edit, false, true);
    edit.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(edit, hstring(L"编辑 " + ToWide(segment.label)));
    edit.Click([this, segment](auto const&, auto const&) { OpenEditor(segment); });
    actions.Children().Append(edit);
    auto duplicate = Button();
    duplicate.Content(box_value(hstring(L"复制")));
    StyleAction(duplicate, false, true);
    duplicate.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(duplicate,
                                             hstring(L"复制 " + ToWide(segment.label)));
    duplicate.Click([this, segment](auto const&, auto const&) { OpenDuplicateEditor(segment); });
    actions.Children().Append(duplicate);
    Grid::SetColumn(actions, 2);
    grid.Children().Append(actions);

    row.Child(grid);
    segmentList_.Children().Append(row);
  }
}

void SettingsWindow::RefreshTimetable(const std::vector<classtime::Segment>& segments,
                                      bool permanent) {
  if (segments.empty()) {
    timetableScroll_.Content(nullptr);
    timetableScroll_.Visibility(Visibility::Collapsed);
    return;
  }
  timetableScroll_.Visibility(Visibility::Visible);
  const auto* schedule = classtime::SelectedTemplate(Services::Instance().config());
  const int days = permanent ? 7 : 1;
  const int firstHour = segments.front().start.hour;
  const int endMinutes = segments.back().end.TotalMinutes();
  const int lastHour = (endMinutes + 59) / 60;
  constexpr double kHeaderHeight = 30;
  constexpr double kHourHeight = 53;
  const double bodyHeight = (lastHour - firstHour) * kHourHeight;
  const auto lineBrush = ResourceBrush(L"CardStrokeColorDefaultBrush",
                                       Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0));

  Grid timetable;
  timetable.MinWidth(permanent ? 556 : 260);
  timetable.Height(kHeaderHeight + bodyHeight);
  ColumnDefinition timeColumn;
  timeColumn.Width(GridLengthHelper::FromValueAndType(52, GridUnitType::Pixel));
  timetable.ColumnDefinitions().Append(timeColumn);
  for (int day = 0; day < days; ++day) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    timetable.ColumnDefinitions().Append(column);
  }

  for (int hour = firstHour; hour < lastHour; ++hour) {
    const double y = kHeaderHeight + (hour - firstHour) * kHourHeight;
    Border line;
    line.VerticalAlignment(VerticalAlignment::Top);
    line.Margin(Thickness{0, y, 0, 0});
    line.Height(1);
    line.Background(lineBrush);
    Grid::SetColumnSpan(line, days + 1);
    timetable.Children().Append(line);

    const classtime::TimeOfDay time{hour, 0};
    auto label = MakeSubtitle(FormatClock(time));
    label.VerticalAlignment(VerticalAlignment::Top);
    label.Margin(Thickness{0, y + 3, 6, 0});
    timetable.Children().Append(label);
  }

  const wchar_t* weekdays[] = {L"周一", L"周二", L"周三", L"周四", L"周五", L"周六", L"周日"};
  const auto todayLabel = L"今天  " + ToWide(NowClock().DateKey());
  for (int day = 0; day < days; ++day) {
    const bool active = !permanent || (schedule && schedule->IsActiveOn(day + 1));
    std::wstring heading = permanent ? weekdays[day] : todayLabel;
    if (!active) heading += L" · 休息";
    auto label = active ? MakeText(heading, 13) : MakeSubtitle(heading);
    label.VerticalAlignment(VerticalAlignment::Top);
    label.HorizontalAlignment(permanent ? HorizontalAlignment::Center : HorizontalAlignment::Left);
    label.Margin(Thickness{5, 4, 5, 0});
    label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    Grid::SetColumn(label, day + 1);
    timetable.Children().Append(label);

    Border divider;
    divider.Margin(Thickness{0, kHeaderHeight, 0, 0});
    divider.HorizontalAlignment(HorizontalAlignment::Left);
    divider.Width(1);
    divider.Background(lineBrush);
    Grid::SetColumn(divider, day + 1);
    timetable.Children().Append(divider);
    if (!active) continue;

    for (const auto& segment : segments) {
      const double start = (segment.start.TotalMinutes() - firstHour * 60) * kHourHeight / 60;
      const double height = segment.DurationMinutes() * kHourHeight / 60;
      auto paint = SegmentColor(segment.kind);
      paint.A = 64;
      Button block;
      block.Background(SolidColorBrush(paint));
      block.BorderThickness(Thickness{0, 0, 0, 0});
      block.CornerRadius(CornerRadius{5, 5, 5, 5});
      block.MinWidth(0);
      block.MinHeight(0);
      block.Padding(Thickness{4, 0, 4, 0});
      block.HorizontalAlignment(HorizontalAlignment::Stretch);
      block.HorizontalContentAlignment(HorizontalAlignment::Left);
      block.VerticalAlignment(VerticalAlignment::Top);
      block.Margin(Thickness{3, kHeaderHeight + start, 3, 0});
      block.Height(std::max(1.0, height - 1));

      const auto name = ToWide(segment.label);
      const auto time = FormatRange(segment);
      if (height >= 19) {
        auto caption = MakeText(permanent ? name : name + L"  ·  " + time, 12);
        caption.TextWrapping(TextWrapping::NoWrap);
        caption.TextTrimming(TextTrimming::CharacterEllipsis);
        block.Content(caption);
      }
      const auto description = heading + L"  " + name + L"  " + time;
      Automation::AutomationProperties::SetName(block, hstring(L"课表编辑 " + description));
      ToolTipService::SetToolTip(block, box_value(hstring(name + L"  " + time)));
      block.Click([this, segment](auto const&, auto const&) { OpenEditor(segment); });
      Grid::SetColumn(block, day + 1);
      timetable.Children().Append(block);
    }
  }
  timetableScroll_.Content(timetable);
}

// —— 作息时间轴 ——

double SettingsWindow::TimelineX(int minute) const {
  return (minute - timelineStartMinute_) * timelinePxPerHour_ / 60.0;
}

int SettingsWindow::TimelineMinute(double x) const {
  return timelineStartMinute_ +
         static_cast<int>(std::lround(x * 60.0 / timelinePxPerHour_));
}

void SettingsWindow::PositionTimelineInitially() {
  if (timelinePositioned_ || !timelineScroll_ || timelineScroll_.ScrollableWidth() <= 0) return;
  const auto segments = classtime::SortedByStart(EditorBaseSegments(planScope_.SelectedIndex() != 1));
  if (segments.empty()) {
    timelinePositioned_ = true;
    return;
  }
  const double target = std::clamp(TimelineX(segments.front().start.TotalMinutes()) - timelinePxPerHour_,
                                   0.0, timelineScroll_.ScrollableWidth());
  const bool changed = timelineScroll_.ChangeView(Windows::Foundation::IReference<double>{target},
                                                   nullptr, nullptr, true);
  timelinePositioned_ = changed || target == 0;
}

void SettingsWindow::RefreshTimeline(const std::vector<classtime::Segment>& segments) {
  timelineStartMinute_ = 0;
  timelineEndMinute_ = 24 * 60;
  RebuildTimelineBlocks(segments);
}

void SettingsWindow::RebuildTimelineBlocks(const std::vector<classtime::Segment>& segments) {
  // 拖动进行中不重建可视树，松手后的刷新会补齐。
  if (timelineDragMode_ != TimelineDragMode::None || timelineDragPointer_) return;
  timelineBlocks_.clear();
  timelineCanvas_.Children().Clear();

  const double pxPerMinute = timelinePxPerHour_ / 60.0;
  const double canvasWidth = 24.0 * timelinePxPerHour_;
  timelineCanvas_.Width(canvasWidth);
  timelineCanvas_.Height(100);
  const auto lineBrush = ResourceBrush(L"CardStrokeColorDefaultBrush",
                                       Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0));
  const int labelStepHours = timelinePxPerHour_ >= 55 ? 1 : 2;

  // 刻度：整点短线与时刻标签（低缩放时隔小时显示）。
  for (int hour = 0; hour <= 24; ++hour) {
    const double x = hour * timelinePxPerHour_;
    Border tick;
    tick.Width(1);
    tick.Height(hour % labelStepHours == 0 ? 10 : 5);
    tick.Background(lineBrush);
    Canvas::SetLeft(tick, x - 0.5);
    Canvas::SetTop(tick, 17);
    timelineCanvas_.Children().Append(tick);
    if (hour < 24 && hour % labelStepHours == 0) {
      const classtime::TimeOfDay time{hour, 0};
      auto label = MakeSubtitle(FormatClock(time));
      Canvas::SetLeft(label, x + 3);
      Canvas::SetTop(label, 0);
      timelineCanvas_.Children().Append(label);
    }
  }
  Border lane;
  lane.Width(canvasWidth);
  lane.Height(1);
  lane.Background(lineBrush);
  Canvas::SetTop(lane, 27);
  timelineCanvas_.Children().Append(lane);

  // “现在”标记：与引擎同一时钟取值，随刷新重画。
  const auto now = NowClock();
  const int nowMinute = now.MinutesSinceMidnight();
  if (nowMinute >= timelineStartMinute_ && nowMinute < timelineEndMinute_) {
    Border marker;
    marker.Width(2);
    marker.Height(70);
    marker.Opacity(0.55);
    marker.Background(SolidColorBrush(SegmentColor(classtime::SegmentKind::Focus)));
    Canvas::SetLeft(marker, TimelineX(nowMinute) - 1);
    Canvas::SetTop(marker, 27);
    timelineCanvas_.Children().Append(marker);
  }

  if (segments.empty()) {
    timelineStatusText_.Text(L"暂无时段：双击时间轴空白处，在对应时刻新增。");
    return;
  }
  timelineStatusText_.Text(planScope_.SelectedIndex() != 1
                               ? L"显示常规模板时段；拖动调整会保存到模板。"
                               : L"显示今天的计划；拖动调整只影响今天。");

  for (const auto& segment : segments) {
    TimelineBlock block;
    block.id = segment.id;
    block.startMinute = segment.start.TotalMinutes();
    block.endMinute = segment.end.TotalMinutes();

    const auto kindColor = SegmentColor(segment.kind);
    auto fill = kindColor;
    fill.A = 64;

    block.host = Border();
    block.host.Height(52);
    block.host.Width(std::max(4.0, (block.endMinute - block.startMinute) * pxPerMinute));
    block.host.CornerRadius(CornerRadius{6, 6, 6, 6});
    block.host.Background(SolidColorBrush(fill));
    block.host.BorderBrush(SolidColorBrush(kindColor));
    block.host.BorderThickness(Thickness{1, 1, 1, 1});
    block.host.VerticalAlignment(VerticalAlignment::Top);
    Canvas::SetLeft(block.host, TimelineX(block.startMinute));
    Canvas::SetTop(block.host, 32);

    // 内部三列：左右拖动把手 + 中间名称与时间。
    auto layout = Grid();
    ColumnDefinition gripLeftColumn;
    gripLeftColumn.Width(GridLengthHelper::FromValueAndType(12, GridUnitType::Pixel));
    ColumnDefinition contentColumn;
    contentColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition gripRightColumn;
    gripRightColumn.Width(GridLengthHelper::FromValueAndType(12, GridUnitType::Pixel));
    layout.ColumnDefinitions().Append(gripLeftColumn);
    layout.ColumnDefinitions().Append(contentColumn);
    layout.ColumnDefinitions().Append(gripRightColumn);

    const auto makeGrip = [&kindColor]() {
      Border grip;
      grip.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
      auto bar = Border();
      auto barColor = kindColor;
      barColor.A = 175;
      bar.Width(4);
      bar.Height(24);
      bar.CornerRadius(CornerRadius{2, 2, 2, 2});
      bar.Background(SolidColorBrush(barColor));
      bar.HorizontalAlignment(HorizontalAlignment::Center);
      bar.VerticalAlignment(VerticalAlignment::Center);
      grip.Child(bar);
      return grip;
    };
    block.leftGrip = makeGrip();
    block.leftGrip.PointerPressed([this, id = segment.id](auto const&,
                                                          Input::PointerRoutedEventArgs const& args) {
      StartTimelineDrag(TimelineDragMode::ResizeStart, id, args);
    });
    layout.Children().Append(block.leftGrip);

    auto content = StackPanel();
    content.Margin(Thickness{2, 4, 2, 4});
    content.Spacing(1);
    block.label = MakeText(ToWide(segment.label), 12);
    block.label.TextTrimming(TextTrimming::CharacterEllipsis);
    content.Children().Append(block.label);
    block.sub = MakeText(FormatRange(segment), 11,
                         ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
    block.sub.TextTrimming(TextTrimming::CharacterEllipsis);
    content.Children().Append(block.sub);
    Grid::SetColumn(content, 1);
    layout.Children().Append(content);

    block.rightGrip = makeGrip();
    Grid::SetColumn(block.rightGrip, 2);
    block.rightGrip.PointerPressed([this, id = segment.id](auto const&,
                                                           Input::PointerRoutedEventArgs const& args) {
      StartTimelineDrag(TimelineDragMode::ResizeEnd, id, args);
    });
    layout.Children().Append(block.rightGrip);

    block.host.Child(layout);
    // 整体拖动与双击编辑。
    block.host.PointerPressed([this, id = segment.id](auto const&,
                                                      Input::PointerRoutedEventArgs const& args) {
      StartTimelineDrag(TimelineDragMode::Move, id, args);
    });
    block.host.DoubleTapped([this, segment](auto const&,
                                            Input::DoubleTappedRoutedEventArgs const& args) {
      OpenEditor(segment);
      args.Handled(true);
    });
    const std::wstring description = ToWide(segment.label) + L"  " + FormatRange(segment);
    Automation::AutomationProperties::SetName(block.host, hstring(L"时间轴时段 " + description));
    ToolTipService::SetToolTip(block.host, box_value(hstring(description)));

    timelineCanvas_.Children().Append(block.host);
    timelineBlocks_.push_back(std::move(block));
  }
}

void SettingsWindow::StartTimelineDrag(TimelineDragMode mode, const std::string& id,
                                       Input::PointerRoutedEventArgs const& args) {
  if (timelineDragMode_ != TimelineDragMode::None || editorOpen_) return;
  const auto block = std::find_if(timelineBlocks_.begin(), timelineBlocks_.end(),
                                  [&id](const TimelineBlock& item) { return item.id == id; });
  if (block == timelineBlocks_.end()) return;
  if (!timelineCanvas_.CapturePointer(args.Pointer())) return;
  timelineDragMode_ = mode;
  timelineDragId_ = id;
  timelineDragStart_ = block->startMinute;
  timelineDragEnd_ = block->endMinute;
  timelineDragCurrentStart_ = block->startMinute;
  timelineDragCurrentEnd_ = block->endMinute;
  timelineDragAnchorMinute_ =
      TimelineMinute(args.GetCurrentPoint(timelineCanvas_).Position().X);
  timelineDragPointer_ = args.Pointer().PointerId();
  args.Handled(true);
}

void SettingsWindow::FinishTimelineDrag(bool commit) {
  if (timelineDragMode_ == TimelineDragMode::None) return;
  timelineDragPointer_.reset();
  const std::string id = std::exchange(timelineDragId_, std::string{});
  const int start = timelineDragCurrentStart_;
  const int end = timelineDragCurrentEnd_;
  const bool changed = start != timelineDragStart_ || end != timelineDragEnd_;
  timelineDragMode_ = TimelineDragMode::None;
  // 释放捕获可能同步触发 PointerCaptureLost，先清除拖动状态防止重复提交。
  if (timelineCanvas_) timelineCanvas_.ReleasePointerCaptures();

  if (commit && changed) {
    ApplyTimelineEdit(id, start, end);
    return;
  }
  // 未移动时不重建点击目标，让双击仍能打开编辑器。
  if (!changed) return;
  // 取消时重绘恢复原位。
  const bool permanent = planScope_.SelectedIndex() != 1;
  RebuildTimelineBlocks(classtime::SortedByStart(EditorBaseSegments(permanent)));
}

void SettingsWindow::ApplyTimelineEdit(const std::string& id, int startMinute, int endMinute) {
  const bool permanent = planScope_.SelectedIndex() != 1;
  auto base = EditorBaseSegments(permanent);
  auto target = std::find_if(base.begin(), base.end(),
                             [&id](const classtime::Segment& item) { return item.id == id; });
  if (target == base.end()) {
    ShowScheduleNotice(L"所选计划里没有这个时段，已恢复原时间。", true);
    RefreshSegments();
    return;
  }
  const auto start = classtime::TimeOfDay::FromTotalMinutes(startMinute);
  const auto end = classtime::TimeOfDay::FromTotalMinutes(endMinute);
  if (!start || !end || end->TotalMinutes() <= start->TotalMinutes()) {
    ShowScheduleNotice(L"调整后的时间不合法，已恢复原时间。", true);
    RefreshSegments();
    return;
  }
  target->start = *start;
  target->end = *end;
  auto sorted = classtime::SortedByStart(base);
  if (!classtime::ValidateSegments(sorted, nullptr)) {
    ShowScheduleNotice(L"调整后的时段与其他时段重叠，已恢复原时间。", true);
    RefreshSegments();
    return;
  }
  std::string error;
  const auto scope = permanent ? Services::SaveScope::Permanent : Services::SaveScope::TodayOnly;
  if (!Services::Instance().ApplyScheduleEdit(sorted, scope, &error)) {
    ShowScheduleNotice(ToWide(error), true);
    RefreshSegments();
    return;
  }
  ShowScheduleNotice(permanent ? (Services::Instance().usingTodayOverride()
                                      ? L"时间轴调整已保存到常规模板；今天仍按临时计划执行。"
                                      : L"时间轴调整已保存到常规模板。")
                                : L"时间轴调整已保存到今天的计划。");
  // 保存触发的状态广播会刷新整页，包括时间轴本身。
}

void SettingsWindow::RefreshDisplayControls() {
  const auto& presentation = Services::Instance().config().presentation;
  using View = Presentation::View;
  const bool screenEdge = presentation.view == View::ScreenEdge;
  SelectSegment(viewSegments_, screenEdge ? 2 : (presentation.view == View::Ring ? 1 : 0));
  SelectSegment(scopeSegments_, presentation.scope == Presentation::Scope::AllDay ? 0 : 1);
  SelectSegment(screenEdgeColorSegments_,
                presentation.screenEdgeColors == Presentation::ScreenEdgeColors::Schedule ? 0 : 1);
  screenEdgeColorPanel_.Visibility(presentation.scope == Presentation::Scope::AllDay
                                      ? Visibility::Visible : Visibility::Collapsed);
  screenEdgeGradientRow_.Visibility(
      presentation.scope == Presentation::Scope::AllDay &&
              presentation.screenEdgeColors == Presentation::ScreenEdgeColors::Schedule
          ? Visibility::Visible : Visibility::Collapsed);
  const int edgeIndex = presentation.edge == Presentation::Edge::Top ? 0
                        : presentation.edge == Presentation::Edge::Bottom ? 1
                        : presentation.edge == Presentation::Edge::Left ? 2
                                                                        : 3;
  SelectSegment(edgeSegments_, edgeIndex);
  RefreshDisplayMonitors();
  ApplyDisplayPanelVisibility(screenEdge, true);
  screenEdgeSlider_.Value(pendingThickness_.value_or(presentation.screenEdgeThickness));
  screenEdgeGradientToggle_.IsOn(presentation.screenEdgeGradient);
  screenEdgeHighlightToggle_.IsOn(presentation.screenEdgeHighlight);
  visibleToggle_.IsOn(presentation.visible);
  hideInFullscreenToggle_.IsOn(presentation.hideInFullscreen);
  coverTaskbarToggle_.IsOn(presentation.coverTaskbar);
  edgeAutoHideToggle_.IsOn(presentation.autoHideAtEdge);
  flushToggle_.IsOn(presentation.flushWithEdge);
  dockBehavior_.Visibility(screenEdge ? Visibility::Collapsed : Visibility::Visible);

  // 长条长度与背景色：只对分节长条有意义；取色器在程序化赋值时由 syncing_ 屏蔽。
  const bool wasSyncing = std::exchange(syncing_, true);
  barLengthPanel_.Visibility(presentation.view == View::Bar && !screenEdge
                                 ? Visibility::Visible
                                 : Visibility::Collapsed);
  barColorPanel_.Visibility(presentation.view == View::Bar ? Visibility::Visible
                                                           : Visibility::Collapsed);
  const int barLength = pendingBarLength_.value_or(presentation.EffectiveBarLength());
  barLengthSlider_.Value(barLength);
  barLengthValue_.Text(hstring((!pendingBarLength_ && presentation.barLengthDip == 0
                                   ? L"默认 · " : L"") +
                              std::to_wstring(barLength) + L" DIP"));
  const auto background = pendingBarBackground_.value_or(presentation.barBackground);
  const bool customBackground = !background.empty();
  barBackgroundToggle_.IsOn(customBackground);
  barColorPicker_.Visibility(customBackground ? Visibility::Visible : Visibility::Collapsed);
  if (customBackground) {
    barColorPicker_.Color(WidgetAppearance::ParseHexColor(background));
  }
  syncing_ = wasSyncing;

  RefreshPhaseColorEditor();
  RefreshDisplayPreview(presentation);
  RefreshPreviewSummary();
}

void SettingsWindow::RefreshDisplayMonitors() {
  // 不在下拉列表打开时重建 Items：选择事件还在使用这些项目。
  if (!monitorCombo_ || monitorCombo_.IsDropDownOpen()) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  const auto selectedDevice = Services::Instance().config().presentation.monitorId;
  const auto monitors = EnumerateMonitors();
  std::wstring signature = L"follow-primary\n";
  int selected = selectedDevice.empty() ? 0 : -1;
  for (size_t index = 0; index < monitors.size(); ++index) {
    const auto& monitor = monitors[index];
    signature += monitor.device + L"\n" + monitor.label + L"\n";
    if (ToUtf8(monitor.device) == selectedDevice) {
      selected = static_cast<int>(index) + 1;
    }
  }
  if (selected < 0) {
    signature += L"unavailable\n" + ToWide(selectedDevice);
    selected = static_cast<int>(monitors.size()) + 1;
    monitorHint_.Text(L"该显示器不可用，组件定位时会回退到系统主屏。");
  } else {
    monitorHint_.Text(L"只改变组件所在位置，不更改 Windows 的主显示器。");
  }

  // 普通设置变更只同步选中项，不替换仍被 WinUI 引用的下拉项目。
  if (monitorListSignature_ != signature) {
    monitorCombo_.Items().Clear();
    ComboBoxItem primary;
    primary.Content(box_value(L"跟随系统主显示器"));
    primary.Tag(box_value(hstring(L"")));
    monitorCombo_.Items().Append(primary);
    for (const auto& monitor : monitors) {
      ComboBoxItem item;
      item.Content(box_value(hstring(monitor.label)));
      item.Tag(box_value(hstring(monitor.device)));
      monitorCombo_.Items().Append(item);
    }
    if (selected > static_cast<int>(monitors.size())) {
      ComboBoxItem unavailable;
      unavailable.Content(box_value(L"上次选择的显示器不可用"));
      unavailable.Tag(box_value(hstring(ToWide(selectedDevice))));
      unavailable.IsEnabled(false);
      monitorCombo_.Items().Append(unavailable);
    }
    monitorListSignature_ = std::move(signature);
  }
  if (monitorCombo_.SelectedIndex() != selected) monitorCombo_.SelectedIndex(selected);
  syncing_ = wasSyncing;
}

void SettingsWindow::ApplyDisplayPanelVisibility(bool screenEdge, bool animate) {
  const StackPanel incoming = screenEdge ? screenEdgeControls_ : dockControls_;
  const StackPanel outgoing = screenEdge ? dockControls_ : screenEdgeControls_;
  if (incoming == nullptr || outgoing == nullptr || displayPanelScreenEdge_ == screenEdge) return;
  displayPanelScreenEdge_ = screenEdge;

  const double incomingOpacity =
      incoming.Visibility() == Visibility::Visible ? incoming.Opacity() : 0;
  const double outgoingOpacity =
      outgoing.Visibility() == Visibility::Visible ? outgoing.Opacity() : 0;
  const double width = displayOptionsHost_.ActualWidth();
  const double from = displayOptionsHost_.ActualHeight();
  Motion::Stop(displayPanelMotion_);

  const auto settle = [this, incoming, outgoing] {
    incoming.Visibility(Visibility::Visible);
    outgoing.Visibility(Visibility::Collapsed);
    outgoing.IsHitTestVisible(false);
    incoming.IsHitTestVisible(true);
    incoming.Opacity(1);
    outgoing.Opacity(0);
    incoming.RenderTransform(nullptr);
    outgoing.RenderTransform(nullptr);
    displayOptionsHost_.Height(std::numeric_limits<double>::quiet_NaN());
    displayOptionsHost_.Clip(nullptr);
    Motion::Stop(displayPanelMotion_);
  };
  if (!animate || !Motion::Enabled() || width <= 1 || from <= 1) {
    settle();
    return;
  }

  incoming.Visibility(Visibility::Visible);
  incoming.Measure(Size{static_cast<float>(width), std::numeric_limits<float>::infinity()});
  const double to = incoming.DesiredSize().Height;
  displayOptionsHost_.Height(from);
  displayOptionsClip_.Rect({0, 0, static_cast<float>(width), static_cast<float>(from)});
  displayOptionsHost_.Clip(displayOptionsClip_);
  incoming.IsHitTestVisible(true);
  outgoing.IsHitTestVisible(false);
  Canvas::SetZIndex(incoming, 1);
  Canvas::SetZIndex(outgoing, 0);

  Storyboard storyboard;
  CubicEase ease;
  ease.EasingMode(EasingMode::EaseInOut);
  Motion::Animate(storyboard, displayOptionsHost_, L"Height", from, to, 235, ease, true);
  incoming.Opacity(incomingOpacity);
  outgoing.Opacity(outgoingOpacity);
  Motion::Animate(storyboard, outgoing, L"Opacity", outgoingOpacity, 0, 115, ease);
  Motion::Animate(storyboard, incoming, L"Opacity", incomingOpacity, 1, 145, ease,
                  false, 90);
  storyboard.Completed([this, settle](auto const& sender, auto const&) {
    if (sender == displayPanelMotion_) settle();
  });
  displayPanelMotion_ = storyboard;
  displayPanelMotion_.Begin();
}

void SettingsWindow::CommitScreenEdgeThickness() {
  if (thicknessTimer_) thicknessTimer_.Stop();
  if (!pendingThickness_) return;
  auto presentation = Services::Instance().config().presentation;
  const int thickness = *std::exchange(pendingThickness_, std::nullopt);
  if (presentation.screenEdgeThickness == thickness) return;
  presentation.screenEdgeThickness = thickness;
  Services::Instance().UpdatePresentation(presentation);
}

void SettingsWindow::CommitBarLength() {
  if (barLengthTimer_) barLengthTimer_.Stop();
  if (!pendingBarLength_) return;
  auto presentation = Services::Instance().config().presentation;
  const int length = *std::exchange(pendingBarLength_, std::nullopt);
  if (presentation.barLengthDip == length) return;
  presentation.barLengthDip = length;
  Services::Instance().UpdatePresentation(presentation);
}

void SettingsWindow::CommitBarBackground() {
  if (barColorTimer_) barColorTimer_.Stop();
  if (!pendingBarBackground_) return;
  auto presentation = Services::Instance().config().presentation;
  const std::string background = *std::exchange(pendingBarBackground_, std::nullopt);
  if (presentation.barBackground == background) return;
  presentation.barBackground = background;
  Services::Instance().UpdatePresentation(presentation);
}

void SettingsWindow::RefreshPhaseColorEditor() {
  if (!phaseColorTargetSegments_ || !phaseColorToggle_ || !phaseColorPicker_) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  const int target = std::clamp(phaseColorTarget_, 0, 2);
  SelectSegment(phaseColorTargetSegments_, target);
  const auto& presentation = Services::Instance().config().presentation;
  const std::string savedHex =
      target == 0 ? presentation.focusColor
                  : (target == 1 ? presentation.restColor : presentation.lunchColor);
  const std::string activeHex = pendingPhaseColors_[target].value_or(savedHex);
  const bool custom = !activeHex.empty();
  phaseColorToggle_.IsOn(custom);
  phaseColorPicker_.Visibility(custom ? Visibility::Visible : Visibility::Collapsed);
  if (custom) {
    phaseColorPicker_.Color(WidgetAppearance::ParseHexColor(activeHex));
    phaseColorHint_.Text(hstring(L"当前使用自定义颜色：#" + ToWide(activeHex)));
  } else {
    phaseColorHint_.Text(
        target == 0 ? L"当前为默认：跟随 Windows 系统强调色"
                    : (target == 1 ? L"当前为默认：#3FA796（柔和青绿）"
                                   : L"当前为默认：#E0A458（暖琥珀）"));
  }
  syncing_ = wasSyncing;
}

void SettingsWindow::CommitPhaseColors() {
  if (phaseColorTimer_) phaseColorTimer_.Stop();
  if (!pendingPhaseColors_[0] && !pendingPhaseColors_[1] && !pendingPhaseColors_[2]) return;
  auto presentation = Services::Instance().config().presentation;
  bool changed = false;
  if (auto hex = std::exchange(pendingPhaseColors_[0], std::nullopt)) {
    if (presentation.focusColor != *hex) {
      presentation.focusColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingPhaseColors_[1], std::nullopt)) {
    if (presentation.restColor != *hex) {
      presentation.restColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingPhaseColors_[2], std::nullopt)) {
    if (presentation.lunchColor != *hex) {
      presentation.lunchColor = *hex;
      changed = true;
    }
  }
  if (changed) Services::Instance().UpdatePresentation(presentation);
}

void SettingsWindow::CommitTemplateName() {
  if (templateNameTimer_) templateNameTimer_.Stop();
  const std::wstring name = std::exchange(pendingTemplateName_, std::wstring{});
  const std::string id = std::exchange(pendingTemplateId_, std::string{});
  if (id.empty() || name.empty()) return;
  Services::Instance().RenameTemplate(id, ToUtf8(name));
}

void SettingsWindow::StartPreviewRefresh() {
  if (!previewRefreshTimer_) {
    previewRefreshTimer_ = window_.DispatcherQueue().CreateTimer();
    previewRefreshTimer_.Interval(std::chrono::seconds(1));
    previewRefreshTimer_.Tick([this](auto const&, auto const&) {
      if (!hwnd_ || !IsWindowVisible(hwnd_) || requestedSection_ != displayScroll_) {
        StopPreviewRefresh();
        return;
      }
      RefreshDisplayPreview(Services::Instance().config().presentation);
      RefreshPreviewSummary();
    });
  }
  RefreshDisplayPreview(Services::Instance().config().presentation);
  RefreshPreviewSummary();
  previewRefreshTimer_.Start();
}

void SettingsWindow::StopPreviewRefresh() {
  if (previewRefreshTimer_) previewRefreshTimer_.Stop();
  Motion::Stop(previewHighlightMotion_);
}

void SettingsWindow::RefreshDisplayPreview(const Presentation& savedPresentation) {
  auto presentation = savedPresentation;
  if (pendingBarLength_) presentation.barLengthDip = *pendingBarLength_;
  if (pendingBarBackground_) presentation.barBackground = *pendingBarBackground_;
  if (pendingPhaseColors_[0]) presentation.focusColor = *pendingPhaseColors_[0];
  if (pendingPhaseColors_[1]) presentation.restColor = *pendingPhaseColors_[1];
  if (pendingPhaseColors_[2]) presentation.lunchColor = *pendingPhaseColors_[2];
  using View = Presentation::View;
  const int thickness = pendingThickness_.value_or(presentation.screenEdgeThickness);
  screenEdgeValue_.Text(std::to_wstring(thickness));
  if (previewChrome_ == nullptr || previewStageA_ == nullptr || previewStageB_ == nullptr) return;

  const auto now = NowClock();
  const auto plan = Services::Instance().PlanFor(now);
  const auto state = BuildWidgetState(plan.segments, now, presentation.scope);
  const auto palette = WidgetAppearance::ResolvePalette(presentation);
  const std::string stateSignature = now.DateKey() + ":" +
      std::to_string(now.hour * 3600 + now.minute * 60 + now.second) + ":" +
      std::to_string(plan.revision) + ":" + std::to_string(palette.background.R) + ":" +
      std::to_string(palette.background.G) + ":" + std::to_string(palette.background.B) + ":" +
      std::to_string(palette.accent.R) + ":" + std::to_string(palette.accent.G) + ":" +
      std::to_string(palette.accent.B) + ":" + std::to_string(palette.rest.R) + ":" +
      std::to_string(palette.rest.G) + ":" + std::to_string(palette.rest.B) + ":" +
      std::to_string(palette.lunch.R) + ":" + std::to_string(palette.lunch.G) + ":" +
      std::to_string(palette.lunch.B) + ":" + std::to_string(palette.foreground.R) + ":" +
      std::to_string(palette.foreground.G) + ":" + std::to_string(palette.foreground.B);

  const bool expanded = previewHovered_ && presentation.autoHideAtEdge;
  const Canvas stages[2] = {previewStageA_, previewStageB_};
  const auto& latest = previewAppearance_[previewStage_];
  const auto& last = latest.presentation;
  if (previewDrawn_ && last.view == presentation.view && last.scope == presentation.scope &&
      last.edge == presentation.edge && last.visible == presentation.visible &&
      last.offsetX == presentation.offsetX && last.offsetY == presentation.offsetY &&
      last.monitorId == presentation.monitorId &&
      last.autoHideAtEdge == presentation.autoHideAtEdge &&
      last.flushWithEdge == presentation.flushWithEdge &&
      last.coverTaskbar == presentation.coverTaskbar &&
      last.screenEdgeColors == presentation.screenEdgeColors &&
      last.screenEdgeGradient == presentation.screenEdgeGradient &&
      last.screenEdgeHighlight == presentation.screenEdgeHighlight &&
      last.barLengthDip == presentation.barLengthDip &&
      last.barBackground == presentation.barBackground &&
      last.focusColor == presentation.focusColor &&
      last.restColor == presentation.restColor &&
      last.lunchColor == presentation.lunchColor && latest.expanded == expanded &&
      latest.thickness == thickness && previewStateSignature_ == stateSignature) return;
  previewStateSignature_ = stateSignature;
  Motion::Stop(previewHighlightMotion_);
  Storyboard highlightPulse;
  if (previewDetail_) DrawPreviewDetail(previewDetail_, presentation, state, plan.segments,
                                         now, palette, expanded, thickness, highlightPulse);

  // A new click starts from whichever layer is still most visible.
  int sourceIndex = previewStage_;
  if (previewMotion_ && stages[1 - sourceIndex].Opacity() > stages[sourceIndex].Opacity()) {
    sourceIndex = 1 - sourceIndex;
  }
  const int targetIndex = previewDrawn_ ? 1 - sourceIndex : 0;
  const Canvas source = previewDrawn_ ? stages[sourceIndex] : nullptr;
  const Canvas target = stages[targetIndex];
  const auto previous = previewAppearance_[sourceIndex];
  const auto from = PreviewAnchor(previous.presentation, previous.expanded);
  const auto to = PreviewAnchor(presentation, expanded);
  auto sourceTransform = source ? source.RenderTransform().try_as<CompositeTransform>() : nullptr;
  const double fromX = from.x + (sourceTransform ? sourceTransform.TranslateX() : 0);
  const double fromY = from.y + (sourceTransform ? sourceTransform.TranslateY() : 0);
  const double sourceOpacity = source ? source.Opacity() : 0;
  Motion::Stop(previewMotion_);
  target.Children().Clear();
  auto children = target.Children();

  const auto color = [](uint8_t r, uint8_t g, uint8_t b) {
    return SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, r, g, b));
  };
  const auto accent = SolidColorBrush(state.snapshot.current
      ? WidgetAppearance::KindColor(palette, state.snapshot.current->kind) : palette.accent);
  const auto track = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(172, 100, 117, 137));
  const auto screenText = color(242, 247, 253);
  const auto ink = SolidColorBrush(palette.foreground);
  const auto accentSurface = WidgetAppearance::Surface(palette.background);
  const auto place = [&](const UIElement& item, double x, double y) {
    Canvas::SetLeft(item, x);
    Canvas::SetTop(item, y);
    children.Append(item);
  };
  const auto rect = [&](double x, double y, double width, double height,
                        const Brush& brush, double radius = 0) {
    Shapes::Rectangle shape;
    shape.Width(width);
    shape.Height(height);
    const double corner = std::min({radius, width / 2, height / 2});
    shape.RadiusX(corner);
    shape.RadiusY(corner);
    shape.Fill(brush);
    place(shape, x, y);
  };
  const auto panel = [&](double x, double y, double width, double height, CornerRadius corners,
                         const Brush& surface) {
    Border shell;
    shell.Width(width);
    shell.Height(height);
    shell.CornerRadius(corners);
    shell.Background(surface);
    shell.BorderThickness(Thickness{0.75, 0.75, 0.75, 0.75});
    shell.BorderBrush(SolidColorBrush(WidgetAppearance::WithAlpha(palette.foreground, 26)));
    place(shell, x, y);
  };
  const auto edgeFill = [&](double x, double y, double width, double height,
                            Presentation::Edge from, bool hasTip) {
    Border fill;
    fill.Width(width);
    fill.Height(height);
    fill.Background(accent);
    const double radius = hasTip ? std::min(width, height) / 2 : 0;
    fill.CornerRadius(WidgetAppearance::DockCorners(radius, from, true, 0));
    place(fill, x, y);
  };
  const auto text = [&](const wchar_t* label, double size, const Brush& brush, double y) {
    auto block = MakeText(label, size, brush);
    block.Width(kPreviewWidth);
    block.TextAlignment(TextAlignment::Center);
    place(block, 0, y);
  };
  const auto textAt = [&](const std::wstring& label, double size, const Brush& brush, double x,
                          double y, double width) {
    auto block = MakeText(label, size, brush);
    block.Width(width);
    block.TextAlignment(TextAlignment::Center);
    place(block, x, y);
  };
  constexpr double left = kPreviewScreenLeft;
  constexpr double top = kPreviewScreenTop;
  constexpr double width = kPreviewScreenWidth;
  const double height =
      kPreviewScreenHeight + (presentation.coverTaskbar ? kPreviewTaskbarHeight : 0.0);
  const double center = to.x;
  const double middleY = to.y;

  const auto draw = [&] {
  if (!presentation.visible) {
    text(L"悬浮窗已隐藏", 14, screenText, 66);
    return;
  }
  const bool allDay = presentation.scope == Presentation::Scope::AllDay;
  const bool dockTop = presentation.edge == Presentation::Edge::Top;
  const bool dockLeft = presentation.edge == Presentation::Edge::Left;
  const bool side = dockLeft || presentation.edge == Presentation::Edge::Right;
  const bool flush = presentation.flushWithEdge;
  if (presentation.view == View::ScreenEdge) {
    const double line = thickness * 0.58;
    if (allDay && presentation.screenEdgeColors == Presentation::ScreenEdgeColors::Schedule &&
        DrawScheduleScreenEdge(target, left, top, width, height, line, plan.segments, now, palette,
                               presentation.screenEdgeGradient)) {
      if (presentation.screenEdgeHighlight) {
        DrawScreenEdgeHighlight(target, left, top, width, height, line, state.edgeProgress,
                                highlightPulse);
      }
      return;
    }
    rect(left, top, width, line, track);
    rect(left + width - line, top + line, line, height - line, track);
    rect(left, top + height - line, width - line, line, track);
    rect(left, top + line, line, height - 2 * line, track);
    const double perimeter = 2 * (width + height - 2 * line);
    double filled = perimeter * state.edgeProgress;
    if (filled > 0) edgeFill(left, top, std::min(filled, width), line,
                             Presentation::Edge::Left, filled < width);
    filled -= width;
    if (filled > 0) {
      edgeFill(left + width - line, top + line, line, std::min(filled, height - line),
               Presentation::Edge::Top, filled < height - line);
      filled -= height - line;
    }
    if (filled > 0) {
      const double amount = std::min(filled, width - line);
      edgeFill(left + width - line - amount, top + height - line, amount, line,
               Presentation::Edge::Right, filled < width - line);
      filled -= width - line;
    }
    if (filled > 0) {
      const double amount = std::min(filled, height - 2 * line);
      edgeFill(left, top + height - line - amount, line, amount,
               Presentation::Edge::Bottom, true);
    }
    if (presentation.screenEdgeHighlight && !plan.segments.empty()) {
      DrawScreenEdgeHighlight(target, left, top, width, height, line, state.edgeProgress,
                              highlightPulse);
    }
    return;
  }

  const bool ring = presentation.view == View::Ring;
  const double segmentProgress = state.snapshot.current ? state.snapshot.progress : 0;
  const auto segmentAccent = accent;
  if (presentation.autoHideAtEdge && !previewHovered_) {
    const auto gripTrack = color(202, 216, 231);
    constexpr double gripLength = 39;
    constexpr double gripOffset = (52 - gripLength) / 2;
    if (side) {
      const double x = dockLeft ? left + 3 : left + width - 12;
      const double y = middleY - 26;
      panel(x, y, 9, 52, CornerRadius{4.5, 4.5, 4.5, 4.5}, accentSurface);
      rect(x + 3, y + gripOffset, 3, gripLength, gripTrack, 1.5);
      rect(x + 3, y + gripOffset, 3, gripLength * segmentProgress, segmentAccent, 1.5);
    } else {
      const double y = dockTop ? top : top + height - 9;
      panel(center - 26, y, 52, 9, CornerRadius{4.5, 4.5, 4.5, 4.5}, accentSurface);
      rect(center - gripLength / 2, y + 3, gripLength, 3, gripTrack, 1.5);
      rect(center - gripLength / 2, y + 3, gripLength * segmentProgress, 3, segmentAccent, 1.5);
    }
    text(L"悬停展开", 12, screenText, 67);
    return;
  }
  if (presentation.view == View::Bar) {
    // 长条长度按设置等比映射到预览（680 → 默认形状），自定义背景色优先。
    const Brush barSurface = !presentation.barBackground.empty()
                                 ? SolidColorBrush(WidgetAppearance::ParseHexColor(
                                       presentation.barBackground))
                                 : Brush(accentSurface);
    const auto drawSegments = [&](bool vertical, double x, double y, double length) {
      const auto& segments = state.visibleSegments;
      if (segments.empty()) {
        auto foreground = palette.foreground;
        if (!presentation.barBackground.empty()) {
          const auto background = WidgetAppearance::ParseHexColor(presentation.barBackground);
          const bool dark = 299 * background.R + 587 * background.G + 114 * background.B < 128000;
          foreground = dark ? PreviewColor(255, 255, 255) : PreviewColor(0, 0, 0);
        }
        textAt(vertical ? L"无安排" : L"今天暂无安排", vertical ? 6 : 9,
               SolidColorBrush(WidgetAppearance::WithAlpha(foreground, 130)),
               vertical ? x : x + 3, vertical ? y + length / 2 - 4 : y + 4,
               vertical ? 21 : length - 6);
        return;
      }
      const double inner = std::max(1.0, length - (vertical ? 16.0 : 20.0));
      const double count = static_cast<double>(segments.size());
      const double gap = std::min(4.0, inner / (2.0 * count));
      const double available = inner - gap * (count - 1);
      const double minimum = std::min(vertical ? 2.5 : 3.5, available / (2.0 * count));
      const double weighted = available - minimum * count;
      int total = 0;
      for (const auto& segment : segments) total += std::max(1, segment.DurationMinutes());
      double cursor = vertical ? y + 8 : x + 10;
      for (const auto& segment : segments) {
        const double segmentLength = minimum + weighted * segment.DurationMinutes() / total;
        const double line = segment.kind == classtime::SegmentKind::Break ? 3.0 : 5.0;
        const auto kind = WidgetAppearance::KindColor(palette, segment.kind);
        const auto unfilled = SolidColorBrush(WidgetAppearance::WithAlpha(kind, 39));
        const auto filled = SolidColorBrush(kind);
        const double progress = WidgetSegmentProgress(segment, now, state.snapshot);
        if (vertical) {
          rect(x + (21 - line) / 2, cursor, line, segmentLength, unfilled, line / 2);
          if (progress > 0) rect(x + (21 - line) / 2, cursor, line,
                                 segmentLength * progress, filled, line / 2);
        } else {
          rect(cursor, y + (21 - line) / 2, segmentLength, line, unfilled, line / 2);
          if (progress > 0) rect(cursor, y + (21 - line) / 2,
                                 segmentLength * progress, line, filled, line / 2);
        }
        cursor += segmentLength + gap;
      }
    };
    if (side) {
      const double fraction = std::clamp((presentation.EffectiveBarLength() - 240) / 1360.0, 0.0, 1.0);
      const double barLen = 50 + 86 * fraction;
      const double inset = flush ? 2 : 8;
      const double barX = dockLeft ? left + inset : left + width - inset - 21;
      const double barY = middleY - barLen / 2;
      panel(barX, barY, 21, barLen,
            WidgetAppearance::DockCorners(10.5, presentation.edge, flush, 3), barSurface);
      drawSegments(true, barX, barY, barLen);
      return;
    }
    const double y = dockTop ? top + (flush ? 2 : 9) : top + height - (flush ? 2 : 9) - 21;
    const double fraction = std::clamp((presentation.EffectiveBarLength() - 240) / 1360.0, 0.0, 1.0);
    const double barLen = 96 + 234 * fraction;
    const double barX = center - barLen / 2;
    panel(barX, y, barLen, 21,
          WidgetAppearance::DockCorners(10.5, presentation.edge, flush, 3), barSurface);
    drawSegments(false, barX, y, barLen);
    return;
  }

  constexpr float radius = 36.5f;
  const float y = static_cast<float>(side ? middleY
                                          : (dockTop ? top + (flush ? 46 : 51)
                                                     : top + height - (flush ? 46 : 51)));
  const double cx = side ? (dockLeft ? left + (flush ? 46 : 55) : left + width - (flush ? 46 : 55))
                         : center;
  Shapes::Ellipse dial;
  dial.Width(88);
  dial.Height(88);
  dial.Fill(accentSurface);
  place(dial, cx - 44, y - 44);
  Shapes::Ellipse bezel;
  bezel.Width(84);
  bezel.Height(84);
  bezel.Stroke(SolidColorBrush(WidgetAppearance::WithAlpha(palette.foreground, 25)));
  bezel.StrokeThickness(0.75);
  place(bezel, cx - 42, y - 42);
  Shapes::Ellipse outline;
  outline.Width(2 * radius + 4);
  outline.Height(2 * radius + 4);
  outline.Stroke(SolidColorBrush(WidgetAppearance::WithAlpha(palette.foreground, 24)));
  outline.StrokeThickness(4);
  place(outline, cx - radius - 2, y - radius - 2);
  if (segmentProgress > 0) {
    PathGeometry geometry;
    PathFigure figure;
    figure.StartPoint({static_cast<float>(cx), y - radius});
    ArcSegment arc;
    arc.Size({radius, radius});
    arc.SweepDirection(SweepDirection::Clockwise);
    const double sweep = segmentProgress * 2 * 3.14159265358979323846;
    arc.IsLargeArc(segmentProgress > 0.5);
    arc.Point({static_cast<float>(cx + radius * std::sin(sweep)),
               static_cast<float>(y - radius * std::cos(sweep))});
    figure.Segments().Append(arc);
    geometry.Figures().Append(figure);
    Shapes::Path progress;
    progress.Data(geometry);
    progress.Stroke(segmentAccent);
    progress.StrokeThickness(4);
    progress.StrokeStartLineCap(PenLineCap::Round);
    progress.StrokeEndLineCap(PenLineCap::Round);
    children.Append(progress);
  }
  std::wstring ringLabel;
  std::wstring ringTime;
  if (state.snapshot.current) {
    const auto& current = *state.snapshot.current;
    ringLabel = current.kind == classtime::SegmentKind::Focus
                    ? L"第 " + std::to_wstring(current.focusOrdinal) + L" 节剩余"
                    : SegmentKindLabel(current.kind) + L"剩余";
    const int remaining = (current.end.TotalMinutes() - now.MinutesSinceMidnight()) * 60 - now.second;
    wchar_t digits[16]{};
    swprintf_s(digits, L"%02d:%02d", (remaining % 3600) / 60, remaining % 60);
    ringTime = remaining >= 3600 ? std::to_wstring(remaining / 3600) + L":" + digits : digits;
  } else if (state.snapshot.next) {
    const bool started = std::any_of(plan.segments.begin(), plan.segments.end(),
        [&now](const auto& segment) { return segment.start.TotalMinutes() <= now.MinutesSinceMidnight(); });
    ringLabel = started ? L"空档 · 下一段" : L"首段开始";
    ringTime = FormatClock(state.snapshot.next->start);
  } else {
    ringTime = state.snapshot.hasSchedule ? L"今日已结束" : L"今天没有安排";
  }
  if (!ringLabel.empty()) textAt(ringLabel, 7, ink, cx - 44, y - 22, 88);
  textAt(ringTime, ringLabel.empty() ? 10 : 16, ink, cx - 44, y - 8, 88);
  if (allDay && state.focusTotal > 0) {
    textAt(L"今日 " + std::to_wstring(state.focusCompleted) + L" / " +
               std::to_wstring(state.focusTotal) + L" 节", 7, ink, cx - 44, y + 13, 88);
  }
  };
  draw();
  if (highlightPulse.Children().Size() > 0) {
    previewHighlightMotion_ = highlightPulse;
    previewHighlightMotion_.Begin();
  }

  previewDrawn_ = true;
  previewAppearance_[targetIndex] = {presentation, expanded, thickness};
  previewStage_ = targetIndex;

  const auto& old = previous.presentation;
  const bool shapeChanged = old.view != presentation.view ||
      old.visible != presentation.visible || old.autoHideAtEdge != presentation.autoHideAtEdge ||
      previous.expanded != expanded;
  const bool docked = old.visible && presentation.visible &&
      old.view != View::ScreenEdge && presentation.view != View::ScreenEdge;
  const bool moved = docked && (old.edge != presentation.edge ||
      old.flushWithEdge != presentation.flushWithEdge ||
      old.coverTaskbar != presentation.coverTaskbar || fromX != to.x || fromY != to.y);
  const auto side = [](Presentation::Edge edge) {
    return edge == Presentation::Edge::Left || edge == Presentation::Edge::Right;
  };
  const bool rotates = side(old.edge) != side(presentation.edge);
  const bool crossfade = shapeChanged || (rotates && (presentation.view == View::Bar ||
      (presentation.autoHideAtEdge && !expanded)));
  const auto settle = [this, target, source] {
    target.Opacity(1);
    target.RenderTransform(nullptr);
    if (source) {
      source.Opacity(0);
      source.RenderTransform(nullptr);
      source.Children().Clear();
    }
    Motion::Stop(previewMotion_);
  };
  if (!source || !Motion::Enabled() || !(moved || shapeChanged)) {
    settle();
    return;
  }

  const int duration = moved ? 320 : 225;
  Canvas::SetZIndex(target, 1);
  Canvas::SetZIndex(source, 0);
  Storyboard storyboard;
  SineEase fade;
  fade.EasingMode(EasingMode::EaseInOut);
  CubicEase travel;
  travel.EasingMode(EasingMode::EaseInOut);

  const auto translate = [&](const Canvas& layer, PreviewPoint anchor, bool incoming) {
    CompositeTransform pose;
    pose.CenterX(anchor.x);
    pose.CenterY(anchor.y);
    layer.RenderTransform(pose);
    if (moved) {
      const double endX = to.x - anchor.x;
      const double endY = to.y - anchor.y;
      Motion::Animate(storyboard, pose, L"TranslateX", fromX - anchor.x, endX, duration, travel);
      Motion::Animate(storyboard, pose, L"TranslateY", fromY - anchor.y, endY, duration, travel);
    }
    if (crossfade) {
      const double fromScale = incoming ? 0.92 : 1.0;
      const double toScale = incoming ? 1.0 : 0.94;
      Motion::Animate(storyboard, pose, L"ScaleX", fromScale, toScale, duration, fade);
      Motion::Animate(storyboard, pose, L"ScaleY", fromScale, toScale, duration, fade);
    }
  };
  target.Opacity(1);
  translate(target, to, true);
  if (crossfade) {
    translate(source, from, false);
    Motion::Animate(storyboard, target, L"Opacity", 0, 1, duration, fade);
    Motion::Animate(storyboard, source, L"Opacity", sourceOpacity, 0, duration, fade);
  } else {
    source.Opacity(0);
    source.Children().Clear();
  }
  storyboard.Completed([this, settle](auto const& sender, auto const&) {
    if (sender == previewMotion_) settle();
  });
  previewMotion_ = storyboard;
  previewMotion_.Begin();
}

void SettingsWindow::RefreshNotificationControls() {
  const bool previous = syncing_;
  syncing_ = true;
  const auto& settings = Services::Instance().config().notifications;
  notificationsToggle_.IsOn(settings.enabled);
  muteToggle_.IsOn(settings.muted);
  focusToggle_.IsOn(settings.focusBoundaries);
  restToggle_.IsOn(settings.restBoundaries);
  lunchToggle_.IsOn(settings.lunchBoundaries);
  screenEdgeFlashToggle_.IsOn(settings.screenEdgeFlash);
  dropClockToggle_.IsOn(settings.dropClockAlert);
  dropClockOptionsPanel_.Visibility(settings.dropClockAlert ? Visibility::Visible
                                                            : Visibility::Collapsed);
  SelectSegment(dropClockStyleSegments_, static_cast<int>(settings.dropClockStyle));
  SelectSegment(dropClockScaleSegments_, static_cast<int>(
      std::find(std::begin(kClockScales), std::end(kClockScales), settings.dropClockScalePercent) - kClockScales));
  SelectSegment(dropClockHoldSegments_, static_cast<int>(
      std::find(std::begin(kClockHolds), std::end(kClockHolds), settings.dropClockHoldSeconds) - kClockHolds));
  const int clockPosIndex =
      settings.dropClockPosition ==
              classtime::NotificationSettings::AlertClockPosition::TopRight
          ? 1
      : settings.dropClockPosition ==
              classtime::NotificationSettings::AlertClockPosition::TopLeft
          ? 2 : 0;
  SelectSegment(dropClockPositionSegments_, clockPosIndex);
  int skinIndex = 0;
  {
    using Skin = classtime::NotificationSettings::AlertClockSkin;
    switch (settings.dropClockSkin) {
      case Skin::Ivory: skinIndex = 1; break;
      case Skin::VintageGold: skinIndex = 2; break;
      case Skin::CyberNeon: skinIndex = 3; break;
      case Skin::RetroMatrix: skinIndex = 4; break;
      case Skin::OceanBlue: skinIndex = 5; break;
      default: skinIndex = 0; break;
    }
  }
  SelectSegment(dropClockSkinGrid_, skinIndex);
  const bool systemEnabled = QuerySystemNotificationsEnabled();
  // 有效提醒 = 主开关开启 且 未暂停 且 系统允许投递；顶部一行写清楚。
  if (!settings.enabled) {
    notificationStateText_.Text(L"边界提醒已关闭");
    notificationStateText_.Foreground(
        ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
  } else if (settings.muted) {
    notificationStateText_.Text(L"提醒已暂停——时间进度仍然继续，可随时恢复。");
    notificationStateText_.Foreground(
        ResourceBrush(L"SystemFillColorCautionBrush",
                      Windows::UI::ColorHelper::FromArgb(255, 157, 93, 0)));
  } else if (!systemEnabled) {
    notificationStateText_.Text(L"系统通知未授权——请到系统设置允许 ClassTime 通知。");
    notificationStateText_.Foreground(
        ResourceBrush(L"SystemFillColorCriticalBrush",
                      Windows::UI::ColorHelper::FromArgb(255, 196, 43, 28)));
  } else {
    notificationStateText_.Text(L"提醒运行中：到边界时会主动请求系统通知。");
    notificationStateText_.Foreground(
        ResourceBrush(L"SystemFillColorSuccessBrush",
                      Windows::UI::ColorHelper::FromArgb(255, 15, 123, 15)));
  }
  const std::wstring label = QueryNotificationPermissionLabel();
  permissionValueText_.Text(hstring(label));
  syncing_ = previous;
  RefreshAlertColorEditor();
  RefreshVisualPreview();
}

void SettingsWindow::RefreshAlertColorEditor() {
  if (!alertColorTargetSegments_ || !alertColorToggle_ || !alertColorPicker_) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  const int target = std::clamp(alertColorTarget_, 0, 4);
  SelectSegment(alertColorTargetSegments_, target);
  const auto& notify = Services::Instance().config().notifications;
  const std::string savedHex =
      target == 0 ? notify.dropClockBodyColor
                  : (target == 1 ? notify.dropClockFaceColor
                                 : (target == 2 ? notify.dropClockTextColor
                                                : (target == 3 ? notify.dropClockAccentColor
                                                               : notify.screenEdgeFlashColor)));
  const std::string activeHex = pendingAlertColors_[target].value_or(savedHex);
  const bool custom = !activeHex.empty();
  alertColorToggle_.IsOn(custom);
  alertColorPicker_.Visibility(custom ? Visibility::Visible : Visibility::Collapsed);
  if (custom) {
    alertColorPicker_.Color(WidgetAppearance::ParseHexColor(activeHex));
    alertColorHint_.Text(hstring(L"当前使用自定义颜色：#" + ToWide(activeHex)));
  } else {
    alertColorHint_.Text(
        target == 0
            ? L"当前为默认：跟随所选皮肤的机身色（深/浅色会自动适配高光与立体侧壁）"
            : (target == 1
                   ? L"当前为默认：跟随所选皮肤的盘面色（控制翻页数字牌面与机械表盘底色）"
                   : (target == 2
                          ? L"当前为默认：按盘面与皮肤自动匹配（控制数字、刻度、指针与文字）"
                          : (target == 3
                                 ? L"当前为默认：跟随到点时所处时段颜色（控制冒号、秒针、灯带与徽标）"
                                 : L"当前为默认：跟随到点时所处时段颜色（控制四边柔光呼吸光带）"))));
  }
  syncing_ = wasSyncing;
}

void SettingsWindow::CommitAlertColors() {
  if (alertColorTimer_) alertColorTimer_.Stop();
  if (!pendingAlertColors_[0] && !pendingAlertColors_[1] && !pendingAlertColors_[2] &&
      !pendingAlertColors_[3] && !pendingAlertColors_[4]) return;
  auto settings = Services::Instance().config().notifications;
  bool changed = false;
  if (auto hex = std::exchange(pendingAlertColors_[0], std::nullopt)) {
    if (settings.dropClockBodyColor != *hex) {
      settings.dropClockBodyColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingAlertColors_[1], std::nullopt)) {
    if (settings.dropClockFaceColor != *hex) {
      settings.dropClockFaceColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingAlertColors_[2], std::nullopt)) {
    if (settings.dropClockTextColor != *hex) {
      settings.dropClockTextColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingAlertColors_[3], std::nullopt)) {
    if (settings.dropClockAccentColor != *hex) {
      settings.dropClockAccentColor = *hex;
      changed = true;
    }
  }
  if (auto hex = std::exchange(pendingAlertColors_[4], std::nullopt)) {
    if (settings.screenEdgeFlashColor != *hex) {
      settings.screenEdgeFlashColor = *hex;
      changed = true;
    }
  }
  if (changed) Services::Instance().UpdateNotifications(settings);
}

void SettingsWindow::RefreshVisualPreview() {
  if (!visualPreviewCanvas_) return;
  visualPreviewCanvas_.Children().Clear();

  const auto& config = Services::Instance().config();
  auto notify = config.notifications;
  if (pendingAlertColors_[0]) notify.dropClockBodyColor = *pendingAlertColors_[0];
  if (pendingAlertColors_[1]) notify.dropClockFaceColor = *pendingAlertColors_[1];
  if (pendingAlertColors_[2]) notify.dropClockTextColor = *pendingAlertColors_[2];
  if (pendingAlertColors_[3]) notify.dropClockAccentColor = *pendingAlertColors_[3];
  if (pendingAlertColors_[4]) notify.screenEdgeFlashColor = *pendingAlertColors_[4];

  const auto now = NowClock();
  const auto plan = Services::Instance().PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  classtime::SegmentKind kind = classtime::SegmentKind::Focus;
  std::wstring badge = L"第 1 节";
  std::wstring title = L"阶段到点预览";
  std::wstring body = L"到点时将以这种效果提醒你";
  classtime::TimeOfDay clockTime{now.hour, now.minute};
  if (snapshot.current) {
    const auto& seg = *snapshot.current;
    kind = seg.kind;
    badge = seg.kind == classtime::SegmentKind::Focus
                ? L"第 " + std::to_wstring(seg.focusOrdinal) + L" 节"
                : SegmentKindLabel(seg.kind);
    body = ToWide(seg.label) + L" · " + FormatClock(seg.start) + L"–" + FormatClock(seg.end);
  } else if (snapshot.next) {
    const auto& seg = *snapshot.next;
    kind = seg.kind;
    clockTime = seg.start;
    badge = seg.kind == classtime::SegmentKind::Focus ? L"即将上课" : SegmentKindLabel(seg.kind);
    body = ToWide(seg.label) + L" · " + FormatClock(seg.start) + L"–" + FormatClock(seg.end);
  }

  const auto palette = WidgetAppearance::ResolvePalette(config.presentation);
  const auto phaseAccent = WidgetAppearance::KindColor(palette, kind);
  const auto app = WidgetAppearance::ResolveClockAppearance(notify, phaseAccent);

  Canvas layer = visualPreviewCanvas_;
  const auto place = [&](UIElement const& item, double x, double y) {
    Canvas::SetLeft(item, x);
    Canvas::SetTop(item, y);
    layer.Children().Append(item);
  };
  const auto rect = [&](double x, double y, double w, double h, Brush const& fill,
                        double radius = 0.0, Brush const& stroke = nullptr,
                        double strokeWidth = 0.0) {
    Shapes::Rectangle r;
    r.Width(std::max(0.0, w));
    r.Height(std::max(0.0, h));
    const double cr = std::min({radius, w * 0.5, h * 0.5});
    r.RadiusX(cr);
    r.RadiusY(cr);
    r.Fill(fill);
    if (stroke && strokeWidth > 0.0) {
      r.Stroke(stroke);
      r.StrokeThickness(strokeWidth);
    }
    place(r, x, y);
  };
  const auto labelAt = [&](const std::wstring& text, double size, Windows::UI::Color color,
                           double x, double y, double w, double h,
                           TextAlignment align = TextAlignment::Center,
                           bool bold = false, const wchar_t* family = L"Microsoft YaHei UI") {
    TextBlock tb;
    tb.Text(hstring(text));
    tb.FontSize(size);
    tb.FontFamily(FontFamily(family));
    if (bold) tb.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
    tb.Foreground(SolidColorBrush(color));
    tb.Width(w);
    tb.Height(h);
    tb.TextAlignment(align);
    tb.TextWrapping(TextWrapping::NoWrap);
    tb.TextTrimming(TextTrimming::CharacterEllipsis);
    place(tb, x, y);
  };

  // 1) 示意桌面底框 + 任务栏
  constexpr double scrX = 16.0, scrY = 8.0, scrW = 388.0, scrH = 190.0;
  LinearGradientBrush desktopBrush;
  desktopBrush.StartPoint({0, 0});
  desktopBrush.EndPoint({1, 1});
  {
    GradientStop s0;
    s0.Offset(0.0);
    s0.Color(Windows::UI::ColorHelper::FromArgb(255, 29, 36, 52));
    GradientStop s1;
    s1.Offset(1.0);
    s1.Color(Windows::UI::ColorHelper::FromArgb(255, 20, 24, 35));
    desktopBrush.GradientStops().Append(s0);
    desktopBrush.GradientStops().Append(s1);
  }
  rect(scrX, scrY, scrW, scrH, desktopBrush, 8.0,
       SolidColorBrush(Windows::UI::ColorHelper::FromArgb(55, 255, 255, 255)), 1.0);
  rect(scrX + 1.0, scrY + scrH - 13.0, scrW - 2.0, 12.0,
       SolidColorBrush(Windows::UI::ColorHelper::FromArgb(145, 12, 16, 24)), 2.0);

  // 2) 全屏边缘呼吸闪光示意
  if (notify.screenEdgeFlash) {
    const auto softGlow = SolidColorBrush(WidgetAppearance::WithAlpha(app.flash, 120));
    const auto rimGlow = SolidColorBrush(WidgetAppearance::WithAlpha(app.flash, 235));
    constexpr double glowD = 12.0;
    rect(scrX, scrY, scrW, glowD, softGlow);
    rect(scrX, scrY + scrH - glowD, scrW, glowD, softGlow);
    rect(scrX, scrY + glowD, glowD, scrH - 2 * glowD, softGlow);
    rect(scrX + scrW - glowD, scrY + glowD, glowD, scrH - 2 * glowD, softGlow);

    Shapes::Rectangle rim;
    rim.Width(scrW);
    rim.Height(scrH);
    rim.RadiusX(8.0);
    rim.RadiusY(8.0);
    rim.Stroke(rimGlow);
    rim.StrokeThickness(2.2);
    place(rim, scrX, scrY);
  }

  // 3) 掉落 3D 时钟实时示意（与真实 3D 时钟共用 ResolveClockAppearance 配色）
  if (notify.dropClockAlert) {
    using Pos = classtime::NotificationSettings::AlertClockPosition;
    using Style = classtime::NotificationSettings::AlertClockStyle;
    const bool mech = notify.IsDial(notify.dropClockStyle);
    const double cardW = mech ? 136.0 : 210.0;
    const double cardH = mech ? 150.0 : 124.0;
    // Draw in face-local coordinates, scaling the entire face including hands and glyphs.
    const double scale = 0.84 * notify.dropClockScalePercent / 100.0;
    Canvas face;
    ScaleTransform zoom;
    zoom.ScaleX(scale);
    zoom.ScaleY(scale);
    face.RenderTransform(zoom);
    double left = scrX + (scrW - cardW * scale) / 2;
    if (notify.dropClockPosition == Pos::TopLeft) left = scrX + 16;
    if (notify.dropClockPosition == Pos::TopRight) left = scrX + scrW - cardW * scale - 16;
    place(face, left, scrY + 12);
    layer = face;
    constexpr double cardX = 0, cardY = 0;

    if (!mech) {
      // 投影与底部 3D 侧壁厚边
      rect(cardX + 3.0, cardY + 9.0, cardW, cardH,
           SolidColorBrush(Windows::UI::ColorHelper::FromArgb(88, 0, 0, 0)), 16.0);
      rect(cardX, cardY + 5.0, cardW, cardH, SolidColorBrush(app.sideWall), 16.0);

      LinearGradientBrush bodyBrush;
      bodyBrush.StartPoint({0, 0});
      bodyBrush.EndPoint({0, 1});
      GradientStop b0, b1, b2;
      b0.Offset(0.0);
      b0.Color(app.bodyTop);
      b1.Offset(0.55);
      b1.Color(app.bodyMid);
      b2.Offset(1.0);
      b2.Color(app.bodyBottom);
      bodyBrush.GradientStops().Append(b0);
      bodyBrush.GradientStops().Append(b1);
      bodyBrush.GradientStops().Append(b2);
      rect(cardX, cardY, cardW, cardH, bodyBrush, 16.0, SolidColorBrush(app.rim), 1.0);

      // 顶部强调灯带
      rect(cardX + 58.0, cardY + 5.0, cardW - 116.0, 3.2, SolidColorBrush(app.accent), 1.6);

      // 左右翻页牌（盘面）
      const auto drawTile = [&](double tx, const std::wstring& digits) {
        constexpr double tw = 76.0, th = 58.0;
        const double ty = cardY + 16.0;
        rect(tx, ty + th * 0.5, tw, th * 0.5, SolidColorBrush(app.tileBottom), 8.0);
        rect(tx, ty, tw, th * 0.5 + 4.0, SolidColorBrush(app.tileTop), 8.0);
        rect(tx, ty, tw, th, SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)),
             8.0, SolidColorBrush(app.tileBorder), 0.9);
        rect(tx + 2.0, ty + th * 0.5 - 0.6, tw - 4.0, 1.2, SolidColorBrush(app.hinge));
        for (const double dotX : {tx + 2.0, tx + tw - 5.0}) {
          Shapes::Ellipse hingeDot;
          hingeDot.Width(3.0);
          hingeDot.Height(4.5);
          hingeDot.Fill(SolidColorBrush(WidgetAppearance::WithAlpha(app.accent, 180)));
          place(hingeDot, dotX, ty + th * 0.5 - 2.2);
        }
        labelAt(digits, 33.0, app.digit, tx, ty + 6.0, tw, th - 10.0, TextAlignment::Center,
                true, L"Bahnschrift");
      };
      wchar_t hh[8]{}, mm[8]{};
      swprintf_s(hh, L"%02d", clockTime.hour);
      swprintf_s(mm, L"%02d", clockTime.minute);
      if (notify.dropClockStyle == Style::Panel3D) {
        rect(cardX + 18, cardY + 16, cardW - 36, 58, SolidColorBrush(app.tileBottom), 8,
             SolidColorBrush(app.tileBorder), 1);
        labelAt(std::wstring(hh) + L":" + mm, 33, app.digit, cardX + 18, cardY + 22,
                cardW - 36, 50, TextAlignment::Center, true, L"Bahnschrift");
      } else {
        drawTile(cardX + 18.0, hh);
        drawTile(cardX + cardW - 18.0 - 76.0, mm);

        // 冒号双 LED
        for (const double cy : {cardY + 36.0, cardY + 52.0}) {
          Shapes::Ellipse dot;
          dot.Width(5.5);
          dot.Height(5.5);
          dot.Fill(SolidColorBrush(app.accent));
          place(dot, cardX + cardW * 0.5 - 2.75, cy);
        }
      }

      // 底部徽标与文字
      rect(cardX + 18.0, cardY + 82.0, 56.0, 16.0,
           SolidColorBrush(WidgetAppearance::WithAlpha(app.accent, app.lightBody ? 40 : 56)), 8.0,
           SolidColorBrush(app.accent), 0.9);
      labelAt(badge, 9.5, app.badgeText, cardX + 20.0, cardY + 83.5, 52.0, 13.0,
              TextAlignment::Center, true);
      labelAt(title, 11.0, app.title, cardX + 80.0, cardY + 82.5, cardW - 96.0, 16.0,
              TextAlignment::Left, true);
      labelAt(body, 9.2, app.footer, cardX + 14.0, cardY + 102.0, cardW - 28.0, 14.0);
    } else {
      constexpr double dialD = 104.0;
      const double cx = cardX + cardW * 0.5;
      const double cy = cardY + dialD * 0.5 + 1.0;

      // 3D 表壳阴影、侧壁、金属外圈
      const auto circleAt = [&](double d, Brush const& fill, double offX = 0.0, double offY = 0.0,
                                Brush const& stroke = nullptr, double sw = 0.0) {
        Shapes::Ellipse e;
        e.Width(d);
        e.Height(d);
        e.Fill(fill);
        if (stroke && sw > 0.0) {
          e.Stroke(stroke);
          e.StrokeThickness(sw);
        }
        place(e, cx - d * 0.5 + offX, cy - d * 0.5 + offY);
      };
      circleAt(dialD, SolidColorBrush(Windows::UI::ColorHelper::FromArgb(88, 0, 0, 0)), 2.0, 7.0);
      circleAt(dialD, SolidColorBrush(app.sideWall), 0.0, 4.0);

      LinearGradientBrush bezelBrush;
      bezelBrush.StartPoint({0, 0});
      bezelBrush.EndPoint({1, 1});
      GradientStop bz0, bz1, bz2;
      bz0.Offset(0.0);
      bz0.Color(app.bezel0);
      bz1.Offset(0.45);
      bz1.Color(app.bezel1);
      bz2.Offset(1.0);
      bz2.Color(app.bezel2);
      bezelBrush.GradientStops().Append(bz0);
      bezelBrush.GradientStops().Append(bz1);
      bezelBrush.GradientStops().Append(bz2);
      circleAt(dialD, bezelBrush);
      circleAt(dialD - 5.5, SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)),
               0.0, 0.0, SolidColorBrush(app.accent), 1.6);

      LinearGradientBrush dialBrush;
      dialBrush.StartPoint({0.15, 0.1});
      dialBrush.EndPoint({0.85, 0.9});
      GradientStop d0, d1, d2;
      d0.Offset(0.0);
      d0.Color(app.dial0);
      d1.Offset(0.65);
      d1.Color(app.dial1);
      d2.Offset(1.0);
      d2.Color(app.dial2);
      dialBrush.GradientStops().Append(d0);
      dialBrush.GradientStops().Append(d1);
      dialBrush.GradientStops().Append(d2);
      constexpr double faceR = (dialD - 9.0) * 0.5;
      circleAt(faceR * 2.0, dialBrush);

      // 12 小时刻度
      constexpr double kPiPreview = 3.14159265358979323846;
      for (int i = 0; i < 12; ++i) {
        const double ang = (i * 30.0 - 90.0) * kPiPreview / 180.0;
        const float c = static_cast<float>(std::cos(ang));
        const float s = static_cast<float>(std::sin(ang));
        const double rOut = faceR - 3.5;
        const double rIn = faceR - ((i % 3 == 0) ? 9.5 : 6.5);
        Shapes::Line tick;
        tick.X1(cx + rIn * c);
        tick.Y1(cy + rIn * s);
        tick.X2(cx + rOut * c);
        tick.Y2(cy + rOut * s);
        tick.Stroke(SolidColorBrush(i % 3 == 0 ? app.tickMajor : app.tickMinor));
        tick.StrokeThickness(i % 3 == 0 ? 1.8 : 1.0);
        layer.Children().Append(tick);
      }
      labelAt(notify.dropClockStyle == Style::Roman3D ? L"XII" : L"12", 8.5, app.numeral, cx - 10.0, cy - faceR + 9.0, 20.0, 12.0,
              TextAlignment::Center, true, L"Bahnschrift");
      wchar_t smallTime[8]{};
      swprintf_s(smallTime, L"%02d:%02d", clockTime.hour, clockTime.minute);
      labelAt(smallTime, 8.0, app.digitalSmall, cx - 18.0, cy + 12.0, 36.0, 12.0,
              TextAlignment::Center, true, L"Bahnschrift");

      // 时针 / 分针 / 秒针
      const auto addHand = [&](double deg, double len, double width, Windows::UI::Color color) {
        const double rad = deg * kPiPreview / 180.0;
        Shapes::Line line;
        line.X1(cx - 5.0 * std::cos(rad));
        line.Y1(cy - 5.0 * std::sin(rad));
        line.X2(cx + len * std::cos(rad));
        line.Y2(cy + len * std::sin(rad));
        line.Stroke(SolidColorBrush(color));
        line.StrokeThickness(width);
        line.StrokeStartLineCap(PenLineCap::Round);
        line.StrokeEndLineCap(PenLineCap::Round);
        layer.Children().Append(line);
      };
      const double minDeg = clockTime.minute * 6.0 - 90.0;
      const double hourDeg = ((clockTime.hour % 12) + clockTime.minute / 60.0) * 30.0 - 90.0;
      addHand(hourDeg, faceR * 0.48, 2.8, app.handMain);
      addHand(minDeg, faceR * 0.72, 2.0, app.handMain);
      addHand(-90.0, faceR * 0.78, 1.2, app.accent);
      circleAt(6.0, SolidColorBrush(app.accent));
      circleAt(2.6, SolidColorBrush(app.tickMajor));

      // 下方铭牌
      const double plateY = cardY + dialD + 5.0;
      rect(cardX + 3.0, plateY + 3.5, cardW - 6.0, 38.0,
           SolidColorBrush(app.sideWall), 9.0);
      rect(cardX + 3.0, plateY, cardW - 6.0, 38.0, SolidColorBrush(app.plateBg), 9.0,
           SolidColorBrush(app.plateRim), 0.9);
      rect(cardX + 9.0, plateY + 8.0, 3.0, 22.0, SolidColorBrush(app.accent), 1.5);
      labelAt(badge + L" · " + title, 9.8, app.title, cardX + 16.0, plateY + 5.0, cardW - 26.0,
              14.0, TextAlignment::Left, true);
      labelAt(body, 8.6, app.footer, cardX + 16.0, plateY + 19.0, cardW - 26.0, 13.0,
              TextAlignment::Left);
    }
  } else if (!notify.screenEdgeFlash) {
    labelAt(L"开启上方“全屏边缘闪光”或“掉落 3D 放大时钟”即可在此实时预览", 12.0,
            Windows::UI::ColorHelper::FromArgb(185, 200, 212, 232), scrX + 16.0,
            scrY + scrH * 0.5 - 10.0, scrW - 32.0, 22.0);
  }

  if (visualPreviewCaption_) {
    std::wstring caption = L"实时效果预览：";
    if (notify.dropClockAlert) {
      caption += kClockNames[static_cast<int>(notify.dropClockStyle)];
      caption += L" · " + std::to_wstring(notify.dropClockScalePercent) + L"% · " +
                 std::to_wstring(notify.dropClockHoldSeconds) + L" 秒";
    } else {
      caption += notify.screenEdgeFlash ? L"仅边缘闪光" : L"视觉提醒已关闭";
    }
    caption += L" · " + badge + L"（" + body + L"）";
    visualPreviewCaption_.Text(hstring(caption));
  }
}

void SettingsWindow::RefreshGeneralControls() {
  startupToggle_.IsOn(Services::Instance().config().launchAtLogin);
  versionText_.Text(hstring(L"版本 " + std::wstring(kVersion)));
  const std::wstring path = Services::Instance().dataDirectory().wstring();
  configPathText_.Text(hstring(L"配置文件夹：" + path));
}

// —— 非阻断提示与确认操作 ——

void SettingsWindow::ShowScheduleNotice(const std::wstring& text, bool error) {
  scheduleNotice_.Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Success);
  scheduleNotice_.Message(hstring(text));
  scheduleNotice_.IsOpen(true);
}

void SettingsWindow::ShowAddTemplateDialog() {
  CommitTemplateName();
  ContentDialog dialog;
  dialog.Title(box_value(hstring(L"新增模板")));
  dialog.PrimaryButtonText(L"创建并使用");
  dialog.CloseButtonText(L"取消");
  dialog.DefaultButton(ContentDialogButton::Primary);

  StackPanel fields;
  fields.Spacing(12);
  TextBox name;
  name.Header(box_value(hstring(L"模板名称")));
  name.Text(L"新模板");
  name.MaxLength(80);
  Automation::AutomationProperties::SetName(name, L"新模板名称");
  Add(fields, name);

  RadioButtons source;
  source.Header(box_value(hstring(L"创建方式")));
  source.Items().Append(box_value(L"复制当前模板的时段和生效星期"));
  source.Items().Append(box_value(L"空白模板（默认周一至周五生效）"));
  source.SelectedIndex(0);
  Automation::AutomationProperties::SetName(source, L"模板创建方式");
  Add(fields, source);
  Add(fields, MakeSubtitle(L"创建后立即切换到新模板；若今天有临时调整，今天仍按临时计划执行。"));
  auto errorText = MakeText(L"请输入模板名称。", 13,
                            ResourceBrush(L"SystemFillColorCriticalBrush",
                                          Windows::UI::ColorHelper::FromArgb(255, 196, 43, 28)));
  errorText.Visibility(Visibility::Collapsed);
  Add(fields, errorText);
  dialog.Content(fields);
  dialog.PrimaryButtonClick([name, errorText](ContentDialog const&,
                                             ContentDialogButtonClickEventArgs const& args) {
    if (TrimTemplateName(TextOf(name)).empty()) {
      errorText.Visibility(Visibility::Visible);
      args.Cancel(true);
    }
  });

  try {
    dialog.XamlRoot(rootGrid_.XamlRoot());
    auto operation = dialog.ShowAsync();
    operation.Completed([this, name, source](IAsyncOperation<ContentDialogResult> const& async,
                                             AsyncStatus const&) {
      if (async.Status() != AsyncStatus::Completed ||
          async.GetResults() != ContentDialogResult::Primary) return;
      std::string error;
      if (!Services::Instance().AddTemplate(ToUtf8(TrimTemplateName(TextOf(name))),
                                            source.SelectedIndex() == 0, &error)) {
        ShowScheduleNotice(L"创建模板失败：" + ToWide(error), true);
        return;
      }
      planScope_.SelectedIndex(0);
      RefreshAll();
      ShowScheduleNotice(Services::Instance().usingTodayOverride()
                             ? L"新模板已创建；今天仍按临时计划执行。"
                             : L"新模板已创建并开始使用。");
    });
  } catch (...) {
    ShowScheduleNotice(L"无法打开新增模板窗口，请重试。", true);
  }
}

void SettingsWindow::ConfirmRestorePreset() {
  ContentDialog dialog;
  dialog.Title(box_value(hstring(L"恢复默认作息")));
  dialog.Content(box_value(
      hstring(L"是否将当前模板恢复为随包预设？常规模板的自定义时段会被覆盖；今天的临时调整不会被清除。")));
  dialog.PrimaryButtonText(L"恢复默认");
  dialog.CloseButtonText(L"取消");
  dialog.DefaultButton(ContentDialogButton::Primary);
  try {
    dialog.XamlRoot(rootGrid_.XamlRoot());
  } catch (...) {
    // 对话框不可用时退回直接执行，保持原有行为可用。
    PerformRestorePreset();
    return;
  }
  auto operation = dialog.ShowAsync();
  operation.Completed([this](IAsyncOperation<ContentDialogResult> const& async, AsyncStatus const&) {
    if (async.Status() != AsyncStatus::Completed) return;
    if (async.GetResults() != ContentDialogResult::Primary) return;
    PerformRestorePreset();
  });
}

void SettingsWindow::PerformRestorePreset() {
  std::string error;
  if (!Services::Instance().RestorePreset(&error)) {
    ShowScheduleNotice(L"恢复失败：" + ToWide(error), true);
    return;
  }
  ShowScheduleNotice(L"模板已恢复为随包预设。");
  RefreshAll();
}

void SettingsWindow::ConfirmEditorDelete() {
  const bool permanent = editorScope_.SelectedIndex() == 1;
  const std::wstring name = TextOf(editorName_);
  ContentDialog dialog;
  dialog.Title(box_value(hstring(L"删除时段")));
  dialog.Content(box_value(hstring(
      L"删除“" + (name.empty() ? std::wstring(L"未命名时段") : name) + L"”？此操作会写入" +
      (permanent ? std::wstring(L"常规模板") : std::wstring(L"今天的计划")) + L"，且无法撤销。")));
  dialog.PrimaryButtonText(L"删除");
  dialog.CloseButtonText(L"取消");
  dialog.DefaultButton(ContentDialogButton::Primary);
  try {
    dialog.XamlRoot(rootGrid_.XamlRoot());
  } catch (...) {
    PerformEditorDelete();
    return;
  }
  auto operation = dialog.ShowAsync();
  operation.Completed([this](IAsyncOperation<ContentDialogResult> const& async, AsyncStatus const&) {
    if (async.Status() != AsyncStatus::Completed) return;
    if (async.GetResults() != ContentDialogResult::Primary) return;
    PerformEditorDelete();
  });
}

void SettingsWindow::PerformEditorDelete() {
  if (!editorOpen_ || newSegment_) return;
  const bool permanent = editorScope_.SelectedIndex() == 1;
  auto base = EditorBaseSegments(permanent);
  const auto outcome = classtime::RemoveSegment(base, editingId_);
  if (!outcome.ok()) {
    editorError_.Message(hstring(ToWide(outcome.message)));
    editorError_.IsOpen(true);
    return;
  }
  std::string error;
  const auto scope = permanent ? Services::SaveScope::Permanent : Services::SaveScope::TodayOnly;
  if (!Services::Instance().ApplyScheduleEdit(outcome.segments, scope, &error)) {
    editorError_.Message(hstring(ToWide(error)));
    editorError_.IsOpen(true);
    return;
  }
  ShowScheduleNotice(permanent ? L"时段已从模板中删除。" : L"时段已从今天的计划中删除。");
  CloseEditor();
  RefreshAll();
}

// —— 与时间引擎同源的摘要（禁止 UI 另立计时器） ——

void SettingsWindow::RefreshTodayOverview() {
  const auto now = NowClock();
  scheduleDateText_.Text(hstring(L"今天 " + std::to_wstring(now.month) + L"/" +
                                 std::to_wstring(now.day)));

  const auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();

  // 顶栏“当前”徽标。
  std::wstring nowLabel = L"空闲";
  if (snapshot.current) {
    const auto& segment = *snapshot.current;
    nowLabel = (segment.kind == classtime::SegmentKind::Focus && segment.focusOrdinal > 0)
                   ? L"第 " + std::to_wstring(segment.focusOrdinal) + L" 节"
                   : SegmentKindLabel(segment.kind);
    nowLabel += L" " + FormatClock(segment.start) + L"–" + FormatClock(segment.end);
  } else if (snapshot.next) {
    nowLabel = L"下一节 " + FormatClock(snapshot.next->start);
  }
  scheduleNowText_.Text(hstring(nowLabel));

  // 概览卡：当前节次 / 进度 / 下一段 / 今天的时段列表。
  todayOverview_.Children().Clear();
  const auto& segments = services.effectiveSegments();
  if (segments.empty()) {
    const auto* schedule = classtime::SelectedTemplate(services.config());
    if (services.usingTodayOverride()) {
      Add(todayOverview_, MakeSubtitle(L"今天的计划为空。"));
    } else if (schedule && !schedule->IsActiveOn(now.Weekday())) {
      Add(todayOverview_, MakeSubtitle(L"今天未启用此模板（休息日）。"));
    } else {
      Add(todayOverview_, MakeSubtitle(L"今天没有安排的时段。"));
    }
    return;
  }

  auto currentName = [](const classtime::SegmentRef& segment) {
    if (segment.kind == classtime::SegmentKind::Focus && segment.focusOrdinal > 0) {
      return L"第 " + std::to_wstring(segment.focusOrdinal) + L" 节 · " + ToWide(segment.label);
    }
    return ToWide(segment.label);
  };

  if (snapshot.current) {
    const auto& segment = *snapshot.current;
    const int total = segment.DurationMinutes();
    const int elapsed = static_cast<int>(std::lround(total * snapshot.progress));
    Add(todayOverview_, MakeText(L"当前：" + currentName(segment), 14));
    Add(todayOverview_,
        MakeSubtitle(L"进行 " + FormatMinutes(elapsed) + L" / " + FormatMinutes(total) + L"（" +
                     FormatPercent(snapshot.progress) + L"），结束于 " + FormatClock(segment.end)));
  } else if (snapshot.next) {
    Add(todayOverview_,
        MakeText(L"未在时段内 · 下一个 " + ToWide(snapshot.next->label) + L" " +
                     FormatClock(snapshot.next->start) + L"–" + FormatClock(snapshot.next->end),
                 14));
  } else {
    Add(todayOverview_, MakeText(L"今天到此结束。", 14));
  }

  Border divider;
  divider.Height(1);
  divider.Background(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                   Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0)));
  Add(todayOverview_, divider);

  Add(todayOverview_, MakeSubtitle(L"今天的时段列表"));
  auto sorted = classtime::SortedByStart(segments);
  for (const auto& segment : sorted) {
    auto row = Border();
    row.CornerRadius(CornerRadius{6, 6, 6, 6});
    row.Padding(Thickness{8, 4, 8, 4});
    if (snapshot.current && snapshot.current->id == segment.id) {
      auto soft = SegmentColor(segment.kind);
      soft.A = 38;
      row.Background(SolidColorBrush(soft));
    } else {
      row.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
    }

    auto grid = Grid();
    grid.ColumnSpacing(8);
    auto timeColumn = ColumnDefinition();
    timeColumn.Width(GridLengthHelper::FromValueAndType(52, GridUnitType::Pixel));
    auto dotColumn = ColumnDefinition();
    dotColumn.Width(GridLengthHelper::Auto());
    auto nameColumn = ColumnDefinition();
    nameColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(timeColumn);
    grid.ColumnDefinitions().Append(dotColumn);
    grid.ColumnDefinitions().Append(nameColumn);

    auto time = MakeSubtitle(FormatClock(segment.start));
    time.VerticalAlignment(VerticalAlignment::Center);
    grid.Children().Append(time);

    Shapes::Ellipse dot;
    dot.Width(8);
    dot.Height(8);
    dot.Fill(SolidColorBrush(SegmentColor(segment.kind)));
    dot.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(dot, 1);
    grid.Children().Append(dot);

    auto name = MakeText(ToWide(segment.label), 12);
    name.VerticalAlignment(VerticalAlignment::Center);
    name.TextTrimming(TextTrimming::CharacterEllipsis);
    Grid::SetColumn(name, 2);
    grid.Children().Append(name);

    row.Child(grid);
    Add(todayOverview_, row);
  }
}

void SettingsWindow::RefreshPreviewSummary() {
  if (previewSummaryText_ == nullptr) return;
  const auto now = NowClock();
  const auto plan = Services::Instance().PlanFor(now);
  const auto snapshot = classtime::Evaluate(plan.segments, now);
  if (!snapshot.current && !snapshot.next) {
    previewSummaryText_.Text(snapshot.hasSchedule ? L"今日计划已结束。" : L"今天没有安排的时段。");
    return;
  }
  if (snapshot.current) {
    const auto& segment = *snapshot.current;
    const int total = segment.DurationMinutes();
    const int elapsed = static_cast<int>(std::lround(total * snapshot.progress));
    const std::wstring name =
        (segment.kind == classtime::SegmentKind::Focus && segment.focusOrdinal > 0)
            ? L"第 " + std::to_wstring(segment.focusOrdinal) + L" 节"
            : SegmentKindLabel(segment.kind);
    previewSummaryText_.Text(hstring(L"当前：" + name + L" · 已过 " + FormatPercent(snapshot.progress) +
                                     L"（" + FormatMinutes(elapsed) + L" / " + FormatMinutes(total) +
                                     L"）"));
    return;
  }
  previewSummaryText_.Text(hstring(L"未在时段内 · 下一个 " + ToWide(snapshot.next->label) + L" " +
                                   FormatClock(snapshot.next->start) + L"–" +
                                   FormatClock(snapshot.next->end)));
}

void SettingsWindow::ShowSection(const std::wstring& tag) {
  const ScrollViewer selected = tag == L"schedule" ? scheduleScroll_
                              : tag == L"display" ? displayScroll_
                              : tag == L"notifications" ? notificationsScroll_
                                                        : generalScroll_;
  if (selected == requestedSection_) return;
  requestedSection_ = selected;
  if (selected == scheduleScroll_) {
    const auto& pres = Services::Instance().config().presentation;
    const std::array<std::string, 3> currentColors = {pres.focusColor, pres.restColor,
                                                      pres.lunchColor};
    if (currentColors != lastSchedulePhaseColors_) {
      lastSchedulePhaseColors_ = currentColors;
      RefreshSegments();
      RefreshTodayOverview();
    }
  } else if (selected == notificationsScroll_) {
    RefreshVisualPreview();
  }
  if (selected == displayScroll_ && hwnd_ && IsWindowVisible(hwnd_)) StartPreviewRefresh();
  else StopPreviewRefresh();
  if (!activeSection_ || !Motion::Enabled()) {
    RevealSection();
    return;
  }
  const bool returning = selected == activeSection_;
  const auto from = Motion::ReadPose(activeSection_, activeSection_);
  Motion::Stop(sectionMotion_);
  activeSection_.IsHitTestVisible(returning);
  sectionMotion_ = Motion::Slide(activeSection_, activeSection_, from,
                               returning ? Motion::Pose{} : Motion::Pose{0, -6}, 135);
  if (!returning) {
    sectionMotion_.Completed([this](auto const& sender, auto const&) {
      if (sender == sectionMotion_) RevealSection();
    });
  }
  sectionMotion_.Begin();
}

void SettingsWindow::RevealSection() {
  if (activeSection_) activeSection_.Visibility(Visibility::Collapsed);
  Motion::Stop(sectionMotion_);
  activeSection_ = requestedSection_;
  activeSection_.IsHitTestVisible(true);
  if (Motion::Enabled()) {
    sectionMotion_ = Motion::Slide(activeSection_, activeSection_, {0, 6}, {}, 185);
  } else {
    activeSection_.Opacity(1);
    activeSection_.RenderTransform(nullptr);
  }
  activeSection_.Visibility(Visibility::Visible);
  if (sectionMotion_) sectionMotion_.Begin();
}

void SettingsWindow::CommitWeekdays() {
  std::vector<int> weekdays;
  for (const auto& child : weekdayPanel_.Children()) {
    if (auto check = child.try_as<CheckBox>()) {
      const auto checked = check.IsChecked();
      if (checked != nullptr && checked.Value()) weekdays.push_back(unbox_value<int>(check.Tag()));
    }
  }
  Services::Instance().SetActiveWeekdays(weekdays);
}

std::vector<classtime::Segment> SettingsWindow::EditorBaseSegments(bool permanent) const {
  const auto& config = Services::Instance().config();
  const auto* schedule = classtime::SelectedTemplate(config);
  if (permanent) return schedule != nullptr ? schedule->segments : std::vector<classtime::Segment>{};
  return Services::Instance().effectiveSegments();
}

void SettingsWindow::OpenNewEditor() { OpenNewEditorAt(8 * 60); }

void SettingsWindow::OpenNewEditorAt(int minute) {
  newSegment_ = true;
  editingId_ = "seg-" + std::to_string(GetTickCount64());
  syncing_ = true;
  editorTitle_.Text(L"新增时段");
  editorName_.Text(L"");
  editorKind_.SelectedIndex(0);
  const int startMinute = std::clamp(minute, 0, 1438);
  const int duration = std::min(45, 1439 - startMinute);
  const auto start = classtime::TimeOfDay::FromTotalMinutes(startMinute);
  const auto end = classtime::TimeOfDay::FromTotalMinutes(startMinute + duration);
  editorStart_.Text(hstring(FormatClock(start.value_or(classtime::TimeOfDay{8, 0}))));
  editorEnd_.Text(hstring(FormatClock(end.value_or(classtime::TimeOfDay{8, 45}))));
  editorSlider_.Maximum(180);
  SyncDurationControls(duration);
  ShowEditor();
}

void SettingsWindow::OpenDuplicateEditor(const classtime::Segment& segment) {
  newSegment_ = true;
  editingId_ = "seg-" + std::to_string(GetTickCount64());
  syncing_ = true;
  editorTitle_.Text(L"复制时段");
  editorName_.Text(hstring(ToWide(segment.label)));
  editorKind_.SelectedIndex(KindToIndex(segment.kind));
  const int duration = std::clamp(segment.DurationMinutes(), 1, 1439);
  // 优先接在原时段之后；当天放不下则退回与原时段相同的起点。
  int startMinute = segment.end.TotalMinutes();
  if (startMinute + duration > 1439) startMinute = segment.start.TotalMinutes();
  startMinute = std::clamp(startMinute, 0, 1439 - duration);
  const auto start = classtime::TimeOfDay::FromTotalMinutes(startMinute);
  const auto end = classtime::TimeOfDay::FromTotalMinutes(startMinute + duration);
  if (!start || !end) {
    OpenNewEditor();
    return;
  }
  editorStart_.Text(hstring(FormatClock(*start)));
  editorEnd_.Text(hstring(FormatClock(*end)));
  editorSlider_.Maximum(segment.kind == classtime::SegmentKind::Focus ? 180 : 240);
  SyncDurationControls(duration);
  ShowEditor();
}

void SettingsWindow::OpenEditor(const classtime::Segment& segment) {
  newSegment_ = false;
  editingId_ = segment.id;
  syncing_ = true;
  editorTitle_.Text(L"编辑时段");
  editorName_.Text(hstring(ToWide(segment.label)));
  editorKind_.SelectedIndex(KindToIndex(segment.kind));
  editorStart_.Text(hstring(FormatClock(segment.start)));
  editorEnd_.Text(hstring(FormatClock(segment.end)));
  editorSlider_.Maximum(segment.kind == classtime::SegmentKind::Focus ? 180 : 240);
  SyncDurationControls(segment.DurationMinutes());
  ShowEditor();
}

void SettingsWindow::ShowEditor() {
  editorScope_.SelectedIndex(planScope_.SelectedIndex() == 1 ? 0 : 1);
  editorStrategy_.SelectedIndex(1);
  editorStrategy_.Visibility(newSegment_ ? Visibility::Collapsed : Visibility::Visible);
  editorError_.IsOpen(false);
  deleteButton_.Visibility(newSegment_ ? Visibility::Collapsed : Visibility::Visible);
  editorOpen_ = true;
  syncing_ = false;
  nav_.IsEnabled(false);
  UpdateEditorHints();
  Motion::Stop(editorMotion_);
  if (Motion::Enabled()) {
    editorMotion_ = Motion::Slide(editorOverlay_, editorWorkspace_, {0, 10}, {}, 230, true);
  } else {
    editorOverlay_.Opacity(1);
    editorWorkspace_.RenderTransform(nullptr);
  }
  editorOverlay_.IsHitTestVisible(true);
  editorOverlay_.Visibility(Visibility::Visible);
  if (editorMotion_) editorMotion_.Begin();
  editorName_.Focus(FocusState::Programmatic);
}

void SettingsWindow::CloseEditor() {
  if (!editorOpen_) return;
  editorOpen_ = false;
  editorOverlay_.IsHitTestVisible(false);
  if (!Motion::Enabled()) {
    FinishEditorClose();
    return;
  }
  const auto from = Motion::ReadPose(editorOverlay_, editorWorkspace_);
  Motion::Stop(editorMotion_);
  editorMotion_ = Motion::Slide(editorOverlay_, editorWorkspace_, from, {0, 8}, 170);
  editorMotion_.Completed([this](auto const& sender, auto const&) {
    if (sender == editorMotion_) FinishEditorClose();
  });
  editorMotion_.Begin();
}

void SettingsWindow::FinishEditorClose() {
  editorOverlay_.Visibility(Visibility::Collapsed);
  Motion::Stop(editorMotion_);
  editorError_.IsOpen(false);
  nav_.IsEnabled(true);
  planScope_.Focus(FocusState::Programmatic);
}

void SettingsWindow::UpdateEditorEnd() {
  if (!editorOpen_ || syncing_) return;
  const auto start = ParseBox(editorStart_);
  const auto minutes = ParseDuration(editorMinutes_);
  if (!start || !minutes) {
    RefreshEditorPreview();
    return;
  }
  const auto end = classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + *minutes);
  if (!end) {
    editorError_.Message(L"结束时间会超过 23:59，请缩短时长。");
    editorError_.IsOpen(true);
    RefreshEditorPreview();
    return;
  }
  syncing_ = true;
  SetText(editorEnd_, FormatClock(*end));
  editorSlider_.Value(std::clamp<double>(*minutes, editorSlider_.Minimum(),
                                        editorSlider_.Maximum()));
  syncing_ = false;
  editorError_.IsOpen(false);
  UpdateEditorHints();
}

void SettingsWindow::SyncDurationControls(int minutes) {
  const bool previous = syncing_;
  syncing_ = true;
  SetText(editorMinutes_, std::to_wstring(minutes));
  editorSlider_.Value(std::clamp<double>(minutes, editorSlider_.Minimum(), editorSlider_.Maximum()));
  syncing_ = previous;
}

void SettingsWindow::UpdateEditorHints() {
  const auto minutes = ParseDuration(editorMinutes_);
  const auto minimum = static_cast<int>(editorSlider_.Minimum());
  const auto maximum = static_cast<int>(editorSlider_.Maximum());
  if (minutes && (*minutes < minimum || *minutes > maximum || *minutes % 5 != 0)) {
    editorSliderHint_.Text(hstring(L"将按 " + std::to_wstring(*minutes) +
                                   L" 分钟保存。拖动滑块可按 5 分钟调整。"));
  } else {
    editorSliderHint_.Text(L"滑块每格 5 分钟，数字框精确到 1 分钟。");
  }
  if (editorScope_.SelectedIndex() == 1 && Services::Instance().usingTodayOverride()) {
    editorScopeHint_.Text(hstring(L"今天仍按临时方案执行；永久修改只影响之后的常规模板。"));
  } else if (editorScope_.SelectedIndex() == 1) {
    editorScopeHint_.Text(hstring(L"修改会写入当前模板，并按生效星期重复。"));
  } else {
    editorScopeHint_.Text(hstring(L"只影响今天，不改动常规模板。"));
  }
  RefreshEditorPreview();
}

classtime::EditOutcome SettingsWindow::EvaluateDraft(
    const std::vector<classtime::Segment>& base, const classtime::Segment& draft) const {
  if (newSegment_) return classtime::InsertSegment(base, draft);
  classtime::EditRequest request;
  request.segmentId = draft.id;
  request.start = draft.start;
  request.end = draft.end;
  request.label = draft.label;
  request.kind = draft.kind;
  request.strategy = editorStrategy_.SelectedIndex() == 1 ? classtime::ShiftStrategy::Keep
                                                           : classtime::ShiftStrategy::Shift;
  return classtime::ApplyEdit(base, request);
}

void SettingsWindow::RefreshEditorPreview() {
  editorPreview_.Children().Clear();
  auto title = MakeText(L"课表预览", 18);
  title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  Add(editorPreview_, title);

  const auto start = ParseBox(editorStart_);
  const auto end = ParseBox(editorEnd_);
  const auto duration = ParseDuration(editorMinutes_);
  if (!start || !end || !duration || end->TotalMinutes() - start->TotalMinutes() != *duration) {
    Add(editorPreview_, MakeSubtitle(L"请输入有效的起止时间和整分钟时长。"));
    return;
  }

  const bool permanent = editorScope_.SelectedIndex() == 1;
  const auto base = EditorBaseSegments(permanent);
  const auto name = TextOf(editorName_);
  const classtime::Segment draft{editingId_, ToUtf8(name.empty() ? L"未命名时段" : name),
                                 IndexToKind(editorKind_.SelectedIndex()), *start, *end};
  auto outcome = EvaluateDraft(base, draft);
  const auto scope = permanent ? L"常规模板" : L"今天的计划";
  Add(editorPreview_, MakeSubtitle(std::wstring(scope) + L" · 尚未保存"));

  if (!outcome.ok()) {
    InfoBar warning;
    warning.Severity(InfoBarSeverity::Warning);
    warning.IsClosable(false);
    warning.IsOpen(true);
    warning.Message(hstring(ToWide(outcome.message)));
    Add(editorPreview_, warning);
    // 即使冲突也显示草稿和受影响的原时段，便于比较重叠区间。
    if (outcome.segments.empty()) {
      outcome.segments = base;
      auto sameId = std::find_if(outcome.segments.begin(), outcome.segments.end(),
                                 [this](const auto& segment) { return segment.id == editingId_; });
      if (sameId == outcome.segments.end()) {
        outcome.segments.push_back(draft);
      } else {
        *sameId = draft;
      }
    }
    outcome.segments = classtime::SortedByStart(outcome.segments);
  }

  const auto& segments = outcome.segments;
  const auto edited = std::find_if(segments.begin(), segments.end(),
                                  [this](const auto& segment) { return segment.id == editingId_; });
  if (edited == segments.end()) return;
  const auto first = edited == segments.begin() ? edited : edited - 1;
  const auto last = edited + std::min<std::ptrdiff_t>(4, segments.end() - edited);
  const int rangeStart = first->start.TotalMinutes();
  int rangeEnd = rangeStart + 1;
  for (auto it = first; it != last; ++it) rangeEnd = std::max(rangeEnd, it->end.TotalMinutes());

  for (auto it = first; it != last; ++it) {
    const auto& segment = *it;
    const auto* original = classtime::FindSegmentById(base, segment.id);
    const bool changed = original && (original->start != segment.start || original->end != segment.end);
    const bool overlap = outcome.status == classtime::EditStatus::Overlap &&
        std::any_of(segments.begin(), segments.end(), [&segment](const auto& other) {
          return segment.id != other.id && segment.start < other.end && other.start < segment.end;
        });
    const bool conflict = overlap ||
        std::find(outcome.conflictIds.begin(), outcome.conflictIds.end(), segment.id) !=
            outcome.conflictIds.end();
    const bool isDraft = it == edited;
    StackPanel row;
    row.Spacing(3);

    const auto label = ToWide(segment.label);
    std::wstring caption = isDraft ? L"草稿 · " + label : label;
    if (conflict) caption += L" · 冲突";
    else if (!isDraft && changed) caption += L" · 已顺延";
    auto rowTitle = MakeText(caption, 13);
    rowTitle.TextWrapping(TextWrapping::NoWrap);
    rowTitle.TextTrimming(TextTrimming::CharacterEllipsis);
    Add(row, rowTitle);
    const auto range = FormatRange(segment);
    Add(row, MakeSubtitle(changed ? FormatRange(*original) + L"  →  " + range : range));

    Grid timeline;
    timeline.Height(6);
    timeline.Background(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                       Windows::UI::ColorHelper::FromArgb(42, 0, 0, 0)));
    for (int minutes : {segment.start.TotalMinutes() - rangeStart, segment.DurationMinutes(),
                        rangeEnd - segment.end.TotalMinutes()}) {
      ColumnDefinition column;
      column.Width(GridLengthHelper::FromValueAndType(minutes,
          minutes == 0 ? GridUnitType::Pixel : GridUnitType::Star));
      timeline.ColumnDefinitions().Append(column);
    }
    Border bar;
    bar.CornerRadius(CornerRadius{3, 3, 3, 3});
    bar.Background(SolidColorBrush(conflict ? Windows::UI::ColorHelper::FromArgb(255, 188, 53, 66)
                                               : SegmentColor(segment.kind)));
    Grid::SetColumn(bar, 1);
    timeline.Children().Append(bar);
    Add(row, timeline);
    Add(editorPreview_, row);
  }
}

}  // namespace ClassTimeApp
