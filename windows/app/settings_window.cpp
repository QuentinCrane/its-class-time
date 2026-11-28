#include "pch.h"
#include "settings_window.h"

#include "notification_service.h"
#include "login_startup.h"
#include "resource.h"
#include "services.h"
#include "ui_motion.h"
#include "widget_appearance.h"
#include "widget_state.h"

#include <initializer_list>
#include <cwctype>
#include <limits>
#include <robuffer.h>
#include <wincodec.h>

#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
using namespace winrt::Windows::Foundation;

namespace ClassTimeApp {
namespace Windows = winrt::Windows;
namespace {

constexpr wchar_t kVersion[] = L"0.1.2";
using Presentation = classtime::PresentationSettings;
using WidgetStyle = classtime::WidgetStyle;
using WidgetColors = classtime::WidgetColors;
using WidgetOutfit = classtime::WidgetOutfit;
using WidgetView = Presentation::View;
constexpr double kPreviewWidth = 420;
constexpr double kPreviewHeight = 172;
using ClockStyle = classtime::NotificationSettings::AlertClockStyle;
constexpr const wchar_t* kClockNames[] = {L"翻页数字", L"机械表盘", L"电子面板", L"罗马表盘"};
constexpr int kClockScales[] = {75, 100, 125};
constexpr int kClockHolds[] = {2, 4, 8};
constexpr const wchar_t* kWeekdayLabels[] = {L"周一", L"周二", L"周三", L"周四", L"周五", L"周六", L"周日"};

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

void LoadBrandIcon(const Image& image) {
  try {
    com_ptr<IWICImagingFactory> factory;
    check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IWICImagingFactory), factory.put_void()));
    // Use the same transparent icon resource as the EXE and the tray.
    const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_CLASSTIME), IMAGE_ICON, 80, 80, LR_DEFAULTCOLOR));
    if (!icon) {
      image.Visibility(Visibility::Collapsed);
      return;
    }
    com_ptr<IWICBitmap> source;
    const HRESULT decoded = factory->CreateBitmapFromHICON(icon, source.put());
    DestroyIcon(icon);
    check_hresult(decoded);
    com_ptr<IWICFormatConverter> converter;
    check_hresult(factory->CreateFormatConverter(converter.put()));
    check_hresult(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    UINT width{}, height{};
    check_hresult(converter->GetSize(&width, &height));
    Imaging::WriteableBitmap picture(static_cast<int>(width), static_cast<int>(height));
    const UINT stride = width * 4;
    BYTE* pixels{};
    auto buffer = picture.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>();
    check_hresult(buffer->Buffer(&pixels));
    check_hresult(converter->CopyPixels(nullptr, stride, stride * height, pixels));
    picture.Invalidate();
    image.Source(picture);
  } catch (const hresult_error&) {
    image.Visibility(Visibility::Collapsed);
  }
}

TextBlock MakeCardTitle(const std::wstring& text) {
  auto block = MakeText(text, 16);
  block.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  return block;
}

Border MakeSettingsCard(const wchar_t* title, const StackPanel& rows) {
  auto card = MakeCard();
  rows.Children().InsertAt(0, MakeCardTitle(title));
  card.Child(rows);
  return card;
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

// 窄窗口将右列放到下一行。
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

std::wstring WeekdaysLabel(const std::vector<int>& weekdays) {
  std::wstring label;
  for (int day = 1; day <= 7; ++day) {
    if (std::find(weekdays.begin(), weekdays.end(), day) == weekdays.end()) continue;
    if (!label.empty()) label += L"、";
    label += kWeekdayLabels[day - 1];
  }
  return label;
}

StackPanel MakeTimeField(const TextBox& input, const wchar_t* label, const wchar_t* inputName,
                         const wchar_t* pickerName, const wchar_t* placeholder) {
  StackPanel field;
  field.Spacing(5);
  Add(field, MakeText(label, 14));

  Grid row;
  row.ColumnDefinitions().Append(ColumnDefinition());
  ColumnDefinition actionColumn;
  actionColumn.Width(GridLengthHelper::Auto());
  row.ColumnDefinitions().Append(actionColumn);
  row.ColumnSpacing(6);

  input.PlaceholderText(placeholder);
  input.FontSize(22);
  input.MinHeight(46);
  Automation::AutomationProperties::SetName(input, inputName);
  row.Children().Append(input);

  TimePickerFlyout picker;
  picker.ClockIdentifier(L"24HourClock");
  picker.MinuteIncrement(1);
  Primitives::PickerFlyoutBase::SetTitle(picker, label);
  picker.TimePicked([input](const TimePickerFlyout&, const TimePickedEventArgs& args) {
    const auto minute = std::chrono::duration_cast<std::chrono::minutes>(args.NewTime()).count();
    if (const auto time = classtime::TimeOfDay::FromTotalMinutes(static_cast<int>(minute))) {
      SetText(input, FormatClock(*time));
    }
  });

  Button choose;
  FontIcon clock;
  clock.Glyph(L"\uE823");
  clock.FontSize(18);
  choose.Content(clock);
  StyleAction(choose);
  choose.VerticalAlignment(VerticalAlignment::Stretch);
  choose.Click([input, picker](const IInspectable& sender, const RoutedEventArgs&) {
    const auto current = ParseBox(input);
    const int minute = current ? current->TotalMinutes() : NowClock().MinutesSinceMidnight();
    picker.Time(std::chrono::minutes(minute));
    picker.ShowAt(sender.as<FrameworkElement>());
  });
  Automation::AutomationProperties::SetName(choose, pickerName);
  ToolTipService::SetToolTip(choose, box_value(pickerName));
  Grid::SetColumn(choose, 1);
  row.Children().Append(choose);
  Add(field, row);
  return field;
}

thread_local bool syncingChoiceSelection = false;

struct ChoiceSelectionGuard {
  ChoiceSelectionGuard() : previous(std::exchange(syncingChoiceSelection, true)) {}
  ~ChoiceSelectionGuard() { syncingChoiceSelection = previous; }
  bool previous;
};

void BindSegmentChoice(const Primitives::ToggleButton& button, int index,
                       const std::function<void(int)>& select) {
  // UI Automation toggles IsChecked directly, while pointer and keyboard also raise Click.
  button.RegisterPropertyChangedCallback(
      Primitives::ToggleButton::IsCheckedProperty(),
      [select, index](const DependencyObject& sender, const DependencyProperty&) {
        if (syncingChoiceSelection) return;
        const auto choice = sender.as<Primitives::ToggleButton>();
        const auto checked = choice.IsChecked();
        if (checked && checked.Value()) {
          select(index);
        } else {
          // A selected option remains selected until another option is chosen.
          const ChoiceSelectionGuard guard;
          choice.IsChecked(true);
        }
      });
}

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
    BindSegmentChoice(button, index, select);
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
    choice.MinWidth(0);
    choice.Padding(Thickness{4, 9, 4, 9});
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
    Viewbox icon;
    icon.Width(70);
    icon.Height(28);
    icon.Child(preview);
    body.Children().Append(icon);
    auto label = MakeText(labels[index], 14);
    label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    label.HorizontalAlignment(HorizontalAlignment::Center);
    body.Children().Append(label);
    choice.Content(body);
    BindSegmentChoice(choice, index, select);
    Grid::SetColumn(choice, index);
    grid.Children().Append(choice);
  }
  grid.SizeChanged([](IInspectable const& sender, SizeChangedEventArgs const& args) {
    const auto host = sender.as<Grid>();
    const bool stacked = args.NewSize().Width < 272;
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
  const ChoiceSelectionGuard guard;
  int index = 0;
  for (const auto& child : grid.Children()) {
    const auto button = child.as<Primitives::ToggleButton>();
    const bool active = index++ == selected;
    button.IsChecked(active);
    if (button.Tag() && unbox_value_or<hstring>(button.Tag(), L"") == L"view-choice") {
      const auto iconBrush = active
          ? ResourceBrush(L"TextOnAccentFillColorPrimaryBrush",
                          Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
          : Brush(SolidColorBrush(AccentColor()));
      const auto body = button.Content().as<StackPanel>();
      const auto preview = body.Children().GetAt(0).as<Viewbox>().Child().as<Canvas>();
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

// —— 显示预览：模拟屏幕在对比度主题切换时更新 ——
constexpr double kPreviewScreenLeft = 42;
constexpr double kPreviewScreenTop = 5;
constexpr double kPreviewScreenWidth = kPreviewWidth - 2 * kPreviewScreenLeft;
constexpr double kPreviewScreenHeight = 143;
constexpr double kPreviewTaskbarHeight = 12;
constexpr double kPreviewScreenCenterX = kPreviewWidth / 2;

Windows::UI::Color PreviewColor(uint8_t r, uint8_t g, uint8_t b) {
  return Windows::UI::ColorHelper::FromArgb(255, r, g, b);
}

void DrawPreviewChrome(const Canvas& canvas, const WidgetAppearance::Palette& palette) {
  canvas.Children().Clear();
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
       kPreviewScreenHeight + 21, palette.highContrast ? palette.foreground : PreviewColor(24, 35, 50), 9);
  rect(kPreviewScreenLeft, kPreviewScreenTop, kPreviewScreenWidth, kPreviewScreenHeight,
       palette.highContrast ? palette.background : PreviewColor(35, 48, 65));
  rect(kPreviewScreenLeft, kPreviewScreenTop + kPreviewScreenHeight, kPreviewScreenWidth, 12,
       palette.highContrast ? palette.background : PreviewColor(57, 73, 94));
  if (palette.highContrast) {
    rect(kPreviewScreenLeft, kPreviewScreenTop + kPreviewScreenHeight, kPreviewScreenWidth, 1,
         palette.foreground);
  }
  for (double x : {kPreviewScreenCenterX - 15, kPreviewScreenCenterX - 4,
                   kPreviewScreenCenterX + 7}) {
    rect(x, kPreviewScreenTop + kPreviewScreenHeight + 3, 7, 6,
         palette.highContrast ? palette.foreground : PreviewColor(147, 167, 190), 1);
  }
}

// The preview keeps its existing elements when only wall-clock progress changes.
class PreviewLayer {
 public:
  explicit PreviewLayer(const Canvas& canvas) : children_(canvas.Children()) {}

  template <typename Element>
  Element Next() {
    const auto slot = next_++;
    if (slot < children_.Size()) {
      if (auto existing = children_.GetAt(slot).try_as<Element>()) return existing;
      Element replacement;
      children_.SetAt(slot, replacement);
      return replacement;
    }
    Element added;
    children_.Append(added);
    return added;
  }

  void Use(const UIElement& item, double x, double y) {
    const auto slot = next_++;
    if (slot == children_.Size()) children_.Append(item);
    else if (children_.GetAt(slot) != item) children_.SetAt(slot, item);
    Place(item, x, y);
  }

  void Trim() const {
    while (children_.Size() > next_) children_.RemoveAtEnd();
  }

  static void Place(const UIElement& item, double x, double y) {
    if (Canvas::GetLeft(item) != x) Canvas::SetLeft(item, x);
    if (Canvas::GetTop(item) != y) Canvas::SetTop(item, y);
  }

 private:
  UIElementCollection children_;
  uint32_t next_ = 0;
};

void SetPreviewText(const TextBlock& block, const std::wstring& value, double size,
                    const Brush& brush, double width) {
  const hstring text(value);
  if (block.Text() != text) block.Text(text);
  block.FontSize(size);
  block.TextWrapping(TextWrapping::Wrap);
  block.Foreground(brush);
  block.Width(width);
  block.TextAlignment(TextAlignment::Center);
}

struct PreviewRect {
  double left, top, width, height;
};

struct PreviewEdge {
  PreviewRect bounds;
  double line;

  std::array<double, 4> Lengths() const {
    return {bounds.width, bounds.height - line, bounds.width - line, bounds.height - 2 * line};
  }

  void Draw(PreviewLayer& layer, WidgetStyle::ScreenEdgeStyle style, size_t side,
            double begin, double end, const Brush& brush, double crossStart = 0,
            double crossEnd = 1,
            const std::function<void(const Shapes::Rectangle&, double)>& animate = {}) const {
    const bool horizontal = side % 2 == 0;
    const double span = end - begin;
    if (span <= 0) return;
    const double offset = crossStart * line;
    const double cross = (crossEnd - crossStart) * line;
    double x = bounds.left, y = bounds.top;
    switch (side) {
      case 0: x += begin; y += offset; break;
      case 1: x += bounds.width - line + offset; y += line + begin; break;
      case 2: x += bounds.width - line - end; y += bounds.height - line + offset; break;
      default: x += offset; y += bounds.height - line - end; break;
    }
    const ScreenEdgeRect run{x, y, horizontal ? span : cross, horizontal ? cross : span};
    const auto drawPart = [&](const ScreenEdgeRect& visible, double opacity) {
      if (visible.width <= 0 || visible.height <= 0) return;
      auto shape = layer.Next<Shapes::Rectangle>();
      shape.Width(run.width);
      shape.Height(run.height);
      shape.Fill(brush);
      shape.Opacity(opacity);
      if (visible.width == run.width && visible.height == run.height) {
        shape.Clip(nullptr);
      } else {
        // The full strip stays the brush's bounds so gradients stay continuous across clips.
        RectangleGeometry clip;
        clip.Rect({static_cast<float>(visible.x - x), static_cast<float>(visible.y - y),
                   static_cast<float>(visible.width), static_cast<float>(visible.height)});
        shape.Clip(clip);
      }
      layer.Place(shape, x, y);
      if (animate) animate(shape, opacity);
    };
    if (style == WidgetStyle::ScreenEdgeStyle::Solid) {
      drawPart(run, 1);
      return;
    }
    const ScreenEdgeRect frame{bounds.left, bounds.top, bounds.width, bounds.height};
    for (const auto& band : ScreenEdgeBands(style)) {
      if (band.opacity <= 0) continue;
      for (const auto& part : ScreenEdgeBandRects(frame, band.start * line, band.end * line)) {
        drawPart(IntersectScreenEdgeRects(run, part), band.opacity);
      }
    }
  }
};

bool DrawScheduleScreenEdge(PreviewLayer& layer, const PreviewEdge& edge, const WidgetStyle& style,
                            const std::vector<classtime::Segment>& plan,
                            const classtime::WallClock& now, const WidgetAppearance::Palette& palette) {
  const auto runs = BuildScreenEdgeRuns(plan, now);
  if (runs.empty()) return false;
  const auto lengths = edge.Lengths();
  const double perimeter = std::accumulate(lengths.begin(), lengths.end(), 0.0);
  if (!style.screenEdgeGradient || palette.highContrast) {
    for (const auto& run : ProjectScreenEdgeRuns(runs, lengths)) {
      const auto color = WidgetAppearance::ScreenEdgeRunColor(palette, run.kind, run.elapsed);
      edge.Draw(layer, style.screenEdgeStyle, run.side, run.begin, run.end, SolidColorBrush(color));
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
    constexpr Point starts[] = {{0, 0}, {0, 0}, {1, 0}, {0, 1}};
    constexpr Point ends[] = {{1, 0}, {0, 1}, {0, 0}, {0, 0}};
    brush.StartPoint(starts[side]);
    brush.EndPoint(ends[side]);
    edge.Draw(layer, style.screenEdgeStyle, side, 0, lengths[side], brush);
    sideStart += lengths[side];
  }
  return true;
}

void DrawScreenEdgeHighlight(PreviewLayer& layer, const PreviewEdge& edge,
                             WidgetStyle::ScreenEdgeStyle style, double progress,
                             const Storyboard& pulseStoryboard) {
  if (progress <= 0.0001 || progress >= 0.9999) return;
  const double line = edge.line;
  const auto lengths = edge.Lengths();
  const double perimeter = std::accumulate(lengths.begin(), lengths.end(), 0.0);
  if (perimeter <= 0) return;
  const double head = progress * perimeter;
  const auto spec = BuildScreenEdgeHighlightSpec(perimeter, line);
  const double hlBegin = std::max(0.0, head - spec.tailLength);
  const double hlEnd = std::min(perimeter, head + spec.leadLength);
  if (hlEnd <= hlBegin) return;

  const double pad = style == WidgetStyle::ScreenEdgeStyle::Solid ? 0.22 : 0.08;
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
      edge.Draw(layer, style, side, lipBegin - sideStart, lipEnd - sideStart, lipBrush);
    }

    // 2) 居中内缩的白光条芯：保留彩色外缘不呈“断层”，并按 680ms AutoReverse 呼吸闪烁。
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
      edge.Draw(layer, style, side, localBegin, localEnd, brush, pad, 1 - pad,
                [&](const Shapes::Rectangle& coreShape, double opacity) {
        if (!animate) return;
        DoubleAnimation pulse;
        pulse.From(0.30 * opacity);
        pulse.To(opacity);
        pulse.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(680)));
        pulse.AutoReverse(true);
        pulse.RepeatBehavior(RepeatBehaviorHelper::Forever());
        pulse.EasingFunction(pulseEase);
        Storyboard::SetTarget(pulse, coreShape);
        Storyboard::SetTargetProperty(pulse, L"Opacity");
        pulseStoryboard.Children().Append(pulse);
      });
    }
    sideStart = sideEnd;
  }
}

struct PreviewPoint {
  double x = 0;
  double y = 0;
};

Size WidgetPreviewSize(const Presentation& presentation, bool expanded, bool detail) {
  const auto& style = presentation.style;
  const bool vertical = presentation.IsVertical();
  if (presentation.autoHideAtEdge && !expanded) {
    const float length = detail ? 84.0f : 52.0f, thickness = detail ? 12.0f : 8.0f;
    return vertical ? Size{thickness, length} : Size{length, thickness};
  }
  if (presentation.view == WidgetView::Ring) {
    const double ratio = (style.ringDiameterDip - WidgetStyle::kMinRingDiameter) /
                         double(WidgetStyle::kMaxRingDiameter - WidgetStyle::kMinRingDiameter);
    const float diameter = static_cast<float>(detail ? 91 + 47 * ratio : 62 + 52 * ratio);
    return {diameter, diameter};
  }
  const double ratio = (style.EffectiveBarLength(vertical) - WidgetStyle::kMinBarLength) /
                       double(WidgetStyle::kMaxBarLength - WidgetStyle::kMinBarLength);
  const float length = static_cast<float>(vertical ? 54 + 80 * ratio :
      (detail ? 140 + 260 * ratio : 96 + 230 * ratio));
  const float thickness = static_cast<float>(style.EffectiveBarThickness(vertical) *
                                             (detail ? 0.95 : 0.63));
  return vertical ? Size{thickness, length} : Size{length, thickness};
}

PreviewPoint PreviewAnchor(const Presentation& presentation, bool expanded) {
  using Edge = Presentation::Edge;
  const auto edge = presentation.edge;
  const double height =
      kPreviewScreenHeight + (presentation.coverTaskbar ? kPreviewTaskbarHeight : 0.0);
  if (!presentation.visible || presentation.view == WidgetView::ScreenEdge) {
    return {kPreviewScreenCenterX, kPreviewScreenTop + height / 2};
  }
  const auto size = WidgetPreviewSize(presentation, expanded, false);
  const double inset = presentation.autoHideAtEdge || presentation.flushWithEdge ? 1 : 8;
  if (presentation.IsVertical()) {
    const double x = edge == Edge::Left ? kPreviewScreenLeft + inset + size.Width / 2
        : kPreviewScreenLeft + kPreviewScreenWidth - inset - size.Width / 2;
    const double half = size.Height / 2.0;
    const double y = std::clamp(kPreviewScreenTop + height / 2 + presentation.offsetY * height / 1080,
                                kPreviewScreenTop + half, kPreviewScreenTop + height - half);
    return {x, y};
  }
  const double half = size.Width / 2.0;
  const double x = std::clamp(kPreviewScreenCenterX +
                                  presentation.offsetX * kPreviewScreenWidth / 1920.0,
                              kPreviewScreenLeft + half,
                              kPreviewScreenLeft + kPreviewScreenWidth - half);
  const double y = edge == Edge::Top ? kPreviewScreenTop + inset + size.Height / 2
                                     : kPreviewScreenTop + height - inset - size.Height / 2;
  return {x, y};
}

void DrawWidgetPreview(PreviewLayer& layer, WidgetAppearance::RingFace& dial,
                       const Presentation& presentation, const WidgetState& state,
                       const std::vector<classtime::Segment>& plan, const classtime::WallClock& now,
                       const WidgetAppearance::Palette& palette, bool expanded, bool detail,
                       const Storyboard& pulseStoryboard) {
  using WidgetAppearance::WithAlpha;
  const auto& style = presentation.style;
  const auto accent = state.snapshot.current
      ? WidgetAppearance::KindColor(palette, state.snapshot.current->kind) : palette.accent;
  const double progress = state.snapshot.current ? state.snapshot.progress : 0;
  const bool vertical = presentation.IsVertical();
  const bool ring = presentation.view == WidgetView::Ring;
  const auto screenInk = palette.highContrast ? palette.foreground : PreviewColor(243, 247, 253);
  const auto center = detail ? PreviewPoint{210, 70} : PreviewAnchor(presentation, expanded);
  const auto text = [&](const std::wstring& value, double size, const Brush& brush, double x,
                        double y, double width) {
    auto block = layer.Next<TextBlock>();
    SetPreviewText(block, value, size, brush, width);
    layer.Place(block, x, y);
  };
  if (!presentation.visible) {
    const auto ink = detail ? ResourceBrush(L"TextFillColorSecondaryBrush", palette.foreground)
                             : Brush(SolidColorBrush(screenInk));
    text(L"悬浮窗已隐藏", 14, ink, center.x - 150, center.y - 9, 300);
    return;
  }
  if (presentation.view == WidgetView::ScreenEdge) {
    const double inset = style.screenEdgeInsetDip * (detail ? 0.65 : 0.50);
    const double height =
        kPreviewScreenHeight + (presentation.coverTaskbar ? kPreviewTaskbarHeight : 0.0);
    const PreviewEdge edge{
        detail ? PreviewRect{50 + inset, 12 + inset, 320 - 2 * inset, 116 - 2 * inset}
               : PreviewRect{kPreviewScreenLeft + inset, kPreviewScreenTop + inset,
                             kPreviewScreenWidth - 2 * inset, height - 2 * inset},
        style.screenEdgeThickness * (detail ? 0.65 : 0.55)};
    const bool colored = presentation.scope == Presentation::Scope::AllDay &&
        style.screenEdgeColors == WidgetStyle::ScreenEdgeColors::Schedule &&
        DrawScheduleScreenEdge(layer, edge, style, plan, now, palette);
    if (!colored) {
      const auto lengths = edge.Lengths();
      double remaining = state.edgeProgress * std::accumulate(lengths.begin(), lengths.end(), 0.0);
      const auto track = SolidColorBrush(WidgetAppearance::EdgeTrackColor(palette));
      const auto fill = SolidColorBrush(accent);
      for (size_t side = 0; side < lengths.size(); ++side) {
        const double split = std::clamp(remaining, 0.0, lengths[side]);
        edge.Draw(layer, style.screenEdgeStyle, side, 0, split, fill);
        edge.Draw(layer, style.screenEdgeStyle, side, split, lengths[side], track);
        remaining -= lengths[side];
      }
    }
    if (style.screenEdgeHighlight && !palette.highContrast && !plan.empty()) {
      DrawScreenEdgeHighlight(layer, edge, style.screenEdgeStyle, state.edgeProgress,
                              pulseStoryboard);
    }
    return;
  }

  const auto size = WidgetPreviewSize(presentation, expanded, detail);
  const double x = center.x - size.Width / 2, y = center.y - size.Height / 2;
  if (ring && (!presentation.autoHideAtEdge || expanded)) {
    WidgetAppearance::UpdateRingReadout(dial, presentation, palette, state, plan, now);
    const auto viewbox = dial.Element();
    viewbox.Width(size.Width);
    viewbox.Height(size.Height);
    layer.Use(viewbox, x, y);
    return;
  }

  const auto surface = WidgetAppearance::SurfaceColor(
      palette, ring ? style.colors.ringBackground : style.colors.barBackground);
  const auto ink = WidgetAppearance::SurfaceInk(palette, surface, style.colors.inkColor);
  const bool grip = presentation.autoHideAtEdge && !expanded;
  const double scale = detail ? 0.95 : 0.63;
  const double radius = grip ? (detail ? 6 : 4) : style.EffectiveBarCorner(vertical) * scale;
  auto shell = layer.Next<Border>();
  shell.Width(size.Width);
  shell.Height(size.Height);
  shell.Background(WidgetAppearance::Surface(surface));
  shell.BorderThickness({1, 1, 1, 1});
  shell.BorderBrush(SolidColorBrush(WithAlpha(ink, palette.highContrast ? 255 : 50)));
  shell.CornerRadius(WidgetAppearance::DockCorners(
      radius, presentation.edge, !grip && presentation.flushWithEdge, std::min(4 * scale, radius)));
  layer.Place(shell, x, y);

  const auto drawSegment = [&](double along, double span, double line, double fraction,
                               Windows::UI::Color track, Windows::UI::Color fill) {
    const auto part = [&](double length, Windows::UI::Color color) {
      if (length <= 0) return;
      auto shape = layer.Next<Shapes::Rectangle>();
      shape.Width(vertical ? line : length);
      shape.Height(vertical ? length : line);
      const double corner = WidgetAppearance::BarSegmentCorner(style, length / scale, line / scale)
                            * scale;
      shape.RadiusX(corner);
      shape.RadiusY(corner);
      shape.Fill(SolidColorBrush(color));
      layer.Place(shape, vertical ? center.x - line / 2 : along,
                  vertical ? along : center.y - line / 2);
    };
    part(span, track);
    part(span * fraction, fill);
  };
  if (grip) {
    const double length = detail ? 64 : 40;
    drawSegment((vertical ? center.y : center.x) - length / 2, length, detail ? 4 : 3, progress,
                WidgetAppearance::TrackColor(palette, ink, 55), accent);
    if (!detail) text(L"悬停展开", 12, SolidColorBrush(screenInk),
                      110, 67, 200);
    return;
  }
  if (state.visibleSegments.empty()) {
    text(vertical ? L"无安排" : L"今天暂无安排", vertical ? 8 : (detail ? 12 : 9),
         SolidColorBrush(ink), x, center.y - 6, size.Width);
    return;
  }

  const double count = static_cast<double>(state.visibleSegments.size());
  const double padding = 17 * scale;
  const double inner = std::max(1.0, (vertical ? size.Height : size.Width) - 2 * padding);
  const double gap = std::min(style.barGapDip * scale, inner / (2 * count));
  const double available = inner - gap * (count - 1);
  const double minimum = std::min((vertical ? 4 : 6) * scale, available / (2 * count));
  const double weighted = available - minimum * count;
  int total = 0;
  for (const auto& segment : state.visibleSegments) total += std::max(1, segment.DurationMinutes());
  double cursor = (vertical ? y : x) + padding;
  for (const auto& segment : state.visibleSegments) {
    const double span = minimum + weighted * segment.DurationMinutes() / total;
    const double line = WidgetAppearance::BarSegmentThickness(style, segment.kind, vertical) * scale;
    const auto kind = WidgetAppearance::KindColor(palette, segment.kind);
    drawSegment(cursor, span, line, WidgetSegmentProgress(segment, now, state.snapshot),
                WidgetAppearance::TrackColor(palette, kind, 39), kind);
    cursor += span + gap;
  }
}

const std::vector<classtime::Segment>& SamplePreviewPlan() {
  using Kind = classtime::SegmentKind;
  static const std::vector<classtime::Segment> plan{
      {"a", "Morning", Kind::Focus, {8, 0}, {8, 45}},
      {"b", "Break", Kind::Break, {8, 45}, {8, 55}},
      {"c", "Second period", Kind::Focus, {8, 55}, {9, 40}},
      {"d", "Long break", Kind::LongBreak, {9, 40}, {10, 5}},
      {"e", "Third period", Kind::Focus, {10, 5}, {10, 50}},
      {"f", "Lunch", Kind::Lunch, {10, 55}, {12, 15}},
      {"g", "Afternoon", Kind::Focus, {12, 15}, {13, 0}},
  };
  return plan;
}

classtime::WallClock SamplePreviewTime() {
  return {2026, 1, 5, 9, 20, 0};
}


struct WidgetSkin {
  const wchar_t* name;
  WidgetColors colors;
};

const std::array<WidgetSkin, 8>& WidgetSkins() {
  static const std::array<WidgetSkin, 8> skins{{
      {L"系统经典", {}},
      {L"午夜蓝", {"15243A", "15243A", "79ACFF", "53D6BD", "A89AFB", "F4C377", "42536D", "EEF4FF"}},
      {L"薄荷",   {"EBF7F1", "EBF7F1", "087D67", "408CBB", "7663A7", "AC5A1E", "ABC7BB", "19372D"}},
      {L"砂岩",   {"FAF0E3", "FAF0E3", "A54E38", "39766E", "81649A", "BD7723", "DCCCBD", "442C23"}},
      {L"鸢尾",   {"281F3C", "281F3C", "C1A5FF", "6ED2CB", "F59DC1", "F4CA78", "5E4A76", "FCF3FF"}},
      {L"极光",   {"142A30", "142A30", "54DEB4", "7BBFFF", "AFA6FC", "FFCD82", "45616A", "F3FFFD"}},
      {L"铅笔",   {"252C35", "252C35", "EFF3F8", "B5C1CF", "9CA9BC", "D9C489", "5D6775", "F5F7FA"}},
      {L"樱雾",   {"FFF1F5", "FFF1F5", "A94172", "326F8A", "6B60AB", "AA602C", "DABCC9", "421F35"}},
  }};
  return skins;
}

const std::array<WidgetOutfit, 6>& BuiltinOutfits() {
  static const auto outfits = [] {
    const auto& skins = WidgetSkins();
    WidgetStyle studio;
    studio.colors = skins[1].colors;
    studio.barStyle = WidgetStyle::BarStyle::Blocks;
    studio.barLengthDip = 760;
    studio.barThicknessDip = 46;
    studio.barCornerDip = 10;
    studio.barGapDip = 7;

    WidgetStyle note;
    note.colors = skins[2].colors;
    note.barStyle = WidgetStyle::BarStyle::FineLine;
    note.barLengthDip = 460;
    note.barThicknessDip = 25;
    note.barCornerDip = 5;

    WidgetStyle gauge;
    gauge.colors = skins[3].colors;
    gauge.ringStyle = WidgetStyle::RingStyle::Instrument;
    gauge.ringDiameterDip = 178;
    gauge.ringStrokeDip = 6;
    gauge.ringReadout = WidgetStyle::RingReadout::Progress;

    WidgetStyle moon;
    moon.colors = skins[4].colors;
    moon.ringStyle = WidgetStyle::RingStyle::RoundedSquare;
    moon.ringDiameterDip = 186;
    moon.ringStrokeDip = 9;

    WidgetStyle orbit;
    orbit.colors = skins[5].colors;
    orbit.screenEdgeStyle = WidgetStyle::ScreenEdgeStyle::DoubleRail;
    orbit.screenEdgeThickness = 14;
    orbit.screenEdgeInsetDip = 8;
    return std::array<WidgetOutfit, 6>{{
        {"系统经典", WidgetView::Bar, {}},
        {"蓝调长条", WidgetView::Bar, studio},
        {"薄荷细线", WidgetView::Bar, note},
        {"砂岩仪表", WidgetView::Ring, gauge},
        {"鸢尾方盘", WidgetView::Ring, moon},
        {"极光双轨", WidgetView::ScreenEdge, orbit},
    }};
  }();
  return outfits;
}

struct WidgetColorField {
  const wchar_t* name;
  std::string WidgetColors::*field;
};

constexpr WidgetColorField kWidgetColorFields[] = {
    {L"上课", &WidgetColors::focusColor},
    {L"短课间", &WidgetColors::restColor},
    {L"长休息", &WidgetColors::longRestColor},
    {L"午休", &WidgetColors::lunchColor},
    {L"长条底色", &WidgetColors::barBackground},
    {L"圆环底色", &WidgetColors::ringBackground},
    {L"进度底轨", &WidgetColors::trackColor},
    {L"组件文字", &WidgetColors::inkColor},
};

using GalleryItem = std::pair<std::wstring, FrameworkElement>;

Grid MakeAppearanceGallery(const wchar_t* name, const std::vector<GalleryItem>& items,
                           int maxColumns, std::function<void(int)> select) {
  Grid gallery;
  gallery.ColumnSpacing(8);
  gallery.RowSpacing(8);
  Automation::AutomationProperties::SetName(gallery, name);
  for (int i = 0; i < maxColumns; ++i) gallery.ColumnDefinitions().Append(ColumnDefinition());
  const auto addRow = [](const Grid& grid) {
    RowDefinition row;
    row.Height(GridLengthHelper::Auto());
    grid.RowDefinitions().Append(row);
  };
  const int firstRows = (static_cast<int>(items.size()) + maxColumns - 1) / maxColumns;
  for (int i = 0; i < firstRows; ++i) addRow(gallery);
  int index = 0;
  for (const auto& [title, thumbnail] : items) {
    Primitives::ToggleButton tile;
    tile.HorizontalAlignment(HorizontalAlignment::Stretch);
    tile.HorizontalContentAlignment(HorizontalAlignment::Center);
    tile.MinWidth(0);
    tile.MinHeight(54);
    tile.Padding({6, 8, 6, 8});
    tile.CornerRadius({8, 8, 8, 8});
    Automation::AutomationProperties::SetName(tile, hstring(title));
    StackPanel body;
    body.Spacing(5);
    thumbnail.HorizontalAlignment(HorizontalAlignment::Center);
    Add(body, thumbnail);
    auto label = MakeText(title, 13);
    label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    label.TextAlignment(TextAlignment::Center);
    Add(body, label);
    tile.Content(body);
    BindSegmentChoice(tile, index, select);
    Grid::SetRow(tile, index / maxColumns);
    Grid::SetColumn(tile, index % maxColumns);
    ++index;
    gallery.Children().Append(tile);
  }
  gallery.SizeChanged([maxColumns, addRow](auto const& sender, const SizeChangedEventArgs& args) {
    const auto grid = sender.template as<Grid>();
    const int columns = std::clamp(static_cast<int>((args.NewSize().Width + 8) / 116),
                                   1, maxColumns);
    const auto rows = (grid.Children().Size() + columns - 1) / columns;
    while (grid.RowDefinitions().Size() < rows) addRow(grid);
    for (int i = 0; i < maxColumns; ++i) {
      const auto width = GridLengthHelper::FromValueAndType(
          i < columns ? 1 : 0, i < columns ? GridUnitType::Star : GridUnitType::Pixel);
      grid.ColumnDefinitions().GetAt(i).Width(width);
    }
    int slot = 0;
    for (const auto& child : grid.Children()) {
      const auto tile = child.template as<FrameworkElement>();
      Grid::SetRow(tile, slot / columns);
      Grid::SetColumn(tile, slot % columns);
      ++slot;
    }
    while (grid.RowDefinitions().Size() > rows) grid.RowDefinitions().RemoveAtEnd();
  });
  return gallery;
}

Border ColorSwatch(double width, double height) {
  Border chip;
  chip.Width(width);
  chip.Height(height);
  chip.CornerRadius({4, 4, 4, 4});
  chip.BorderThickness({1, 1, 1, 1});
  chip.BorderBrush(ResourceBrush(L"CardStrokeColorDefaultBrush", PreviewColor(132, 140, 148)));
  return chip;
}

StackPanel SkinThumbnail(const WidgetColors& colors) {
  const auto palette = WidgetAppearance::ResolvePalette(colors);
  StackPanel swatches;
  swatches.Orientation(Orientation::Horizontal);
  swatches.Spacing(3);
  for (const auto tint : {palette.accent, palette.rest, palette.longRest, palette.lunch}) {
    auto chip = ColorSwatch(15, 17);
    chip.Background(SolidColorBrush(tint));
    Add(swatches, chip);
  }
  return swatches;
}

Viewbox WidgetThumbnail(WidgetView view, const WidgetStyle& style) {
  const auto palette = WidgetAppearance::ResolvePalette(style.colors);
  Canvas drawing;
  drawing.Width(116);
  drawing.Height(48);
  PreviewLayer layer(drawing);
  if (view == WidgetView::Ring) {
    WidgetAppearance::RingFace dial;
    dial.Update(style, palette, palette.accent, 0.62, L"第 2 节", L"62%", L"");
    auto face = dial.Element();
    face.Width(48);
    face.Height(48);
    layer.Use(face, 34, 0);
  } else if (view == WidgetView::Bar) {
    const auto surface = WidgetAppearance::SurfaceColor(palette, style.colors.barBackground);
    const auto ink = WidgetAppearance::SurfaceInk(palette, surface, style.colors.inkColor);
    const double height = style.EffectiveBarThickness(false) * 0.68;
    const double radius = style.EffectiveBarCorner(false) * 0.68;
    auto bar = layer.Next<Border>();
    bar.Width(110);
    bar.Height(height);
    bar.Background(SolidColorBrush(surface));
    bar.BorderThickness({1, 1, 1, 1});
    bar.BorderBrush(SolidColorBrush(WidgetAppearance::WithAlpha(ink, palette.highContrast ? 255 : 55)));
    bar.CornerRadius({radius, radius, radius, radius});
    layer.Place(bar, 3, (48 - height) / 2);
    using Kind = classtime::SegmentKind;
    constexpr Kind kinds[] = {Kind::Focus, Kind::Break, Kind::Focus, Kind::Lunch, Kind::Focus};
    double x = 12;
    for (int i = 0; i < 5; ++i) {
      const double width = i == 1 ? 8 : 17;
      const double line = WidgetAppearance::BarSegmentThickness(style, kinds[i], false) * 0.64;
      const double corner = WidgetAppearance::BarSegmentCorner(style, width, line);
      const auto color = WidgetAppearance::KindColor(palette, kinds[i]);
      auto part = layer.Next<Shapes::Rectangle>();
      part.Width(width);
      part.Height(line);
      part.RadiusX(corner);
      part.RadiusY(corner);
      part.Fill(SolidColorBrush(i < 4 ? color : WidgetAppearance::TrackColor(palette, color, 48)));
      layer.Place(part, x, (48 - line) / 2);
      x += width + 4;
    }
  } else {
    const PreviewEdge edge{{20, 5, 76, 38}, std::max(3.0, style.screenEdgeThickness * 0.45)};
    const auto lengths = edge.Lengths();
    const auto track = SolidColorBrush(WidgetAppearance::EdgeTrackColor(palette));
    const auto accent = SolidColorBrush(palette.accent);
    edge.Draw(layer, style.screenEdgeStyle, 0, 0, lengths[0], accent);
    edge.Draw(layer, style.screenEdgeStyle, 1, 0, lengths[1], SolidColorBrush(palette.rest));
    edge.Draw(layer, style.screenEdgeStyle, 2, 0, lengths[2], track);
    edge.Draw(layer, style.screenEdgeStyle, 3, 0, lengths[3], track);
  }
  Viewbox scale;
  scale.Width(98);
  scale.Height(40);
  scale.Child(drawing);
  return scale;
}

const wchar_t* WidgetViewName(WidgetView view) {
  switch (view) {
    case WidgetView::Ring: return L"圆环";
    case WidgetView::ScreenEdge: return L"屏幕边框";
    default: return L"分节长条";
  }
}

Canvas ClockStyleThumbnail(ClockStyle style) {
  const auto palette = WidgetAppearance::CurrentPalette();
  Canvas canvas;
  canvas.Width(96);
  canvas.Height(39);
  PreviewLayer layer(canvas);
  const auto ink = SolidColorBrush(palette.foreground);
  const auto outline = SolidColorBrush(
      WidgetAppearance::WithAlpha(palette.foreground, palette.highContrast ? 255 : 145));
  const auto text = [&](const wchar_t* label, double size, double x, double y, double width) {
    const auto block = layer.Next<TextBlock>();
    SetPreviewText(block, label, size, ink, width);
    layer.Place(block, x, y);
  };
  const auto shell = [&](double x, double y, double width, double height, double radius) {
    const auto casing = layer.Next<Border>();
    casing.Width(width);
    casing.Height(height);
    casing.Background(SolidColorBrush(palette.background));
    casing.BorderBrush(outline);
    casing.BorderThickness({1, 1, 1, 1});
    casing.CornerRadius({radius, radius, radius, radius});
    layer.Place(casing, x, y);
  };
  if (classtime::NotificationSettings::IsDial(style)) {
    shell(30, 1, 36, 36, 18);
    if (style == ClockStyle::Roman3D) text(L"XII", 7, 37, 4, 22);
    else text(L"12", 7, 37, 4, 22);
    const auto hands = layer.Next<Shapes::Polyline>();
    PointCollection points;
    points.Append({42, 15});
    points.Append({48, 20});
    points.Append({55, 17});
    hands.Points(points);
    hands.Stroke(ink);
    hands.StrokeThickness(1.6);
    return canvas;
  }
  if (style == ClockStyle::Panel3D) {
    shell(13, 6, 70, 27, 4);
    text(L"09:20", 16, 13, 8, 70);
    return canvas;
  }
  shell(17, 6, 29, 27, 4);
  shell(50, 6, 29, 27, 4);
  text(L"09", 16, 17, 8, 29);
  text(L"20", 16, 50, 8, 29);
  return canvas;
}

Button AppearanceButton(const wchar_t* name) {
  Button button;
  button.Content(box_value(name));
  StyleAction(button);
  return button;
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
  CommitWidgetStyle();
  CommitAlertColors();
  CommitTemplateName();
  Motion::Stop(editorMotion_);
  Motion::Stop(sectionMotion_);
  Motion::Stop(previewMotion_);
  Motion::Stop(previewHighlightMotion_);
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
  window_.SystemBackdrop(MicaBackdrop());

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
  const int width = static_cast<int>(1120 * scale);
  const int height = static_cast<int>(760 * scale);
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
    ReleasePreviewResources();
    Motion::Stop(sectionMotion_);
    if (activeSection_) {
      activeSection_.Opacity(1);
      activeSection_.RenderTransform(nullptr);
    }
    if (editorOverlay_) {
      Motion::Stop(editorMotion_);
      if (editorOpen_) {
        editorOverlay_.Opacity(1);
        editorWorkspace_.RenderTransform(nullptr);
      } else {
        editorOverlay_.Visibility(Visibility::Collapsed);
      }
    }
    CommitWidgetStyle();
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
  nav_.OpenPaneLength(200);
  nav_.CompactPaneLength(52);
  nav_.CompactModeThresholdWidth(0);
  nav_.ExpandedModeThresholdWidth(640);
  nav_.IsPaneToggleButtonVisible(nav_.DisplayMode() != NavigationViewDisplayMode::Expanded);
  nav_.DisplayModeChanged([](const NavigationView& nav,
                            const NavigationViewDisplayModeChangedEventArgs& args) {
    nav.IsPaneToggleButtonVisible(args.DisplayMode() != NavigationViewDisplayMode::Expanded);
  });

  StackPanel brand;
  brand.Orientation(Orientation::Horizontal);
  brand.Spacing(10);
  brand.Margin(Thickness{12, 12, 8, 16});
  Image brandIcon;
  brandIcon.Width(32);
  brandIcon.Height(32);
  brandIcon.Stretch(Stretch::Uniform);
  Add(brand, brandIcon);
  LoadBrandIcon(brandIcon);
  StackPanel brandText;
  brandText.VerticalAlignment(VerticalAlignment::Center);
  auto brandTitle = MakeText(L"“上课”时间到！", 15);
  brandTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
  brandTitle.TextWrapping(TextWrapping::NoWrap);
  Add(brandText, brandTitle);
  Add(brand, brandText);
  nav_.PaneHeader(brand);

  root.Children().Append(nav_);

  struct Section {
    SettingsPage page;
    const wchar_t* title;
    const wchar_t* glyph;
    bool newGroup = false;
  };
  const Section sections[] = {
      {SettingsPage::Schedule, L"作息计划", L"\uE787"},
      {SettingsPage::Timeline, L"时间轴", L"\uE823"},
      {SettingsPage::Timetable, L"课表预览", L"\uE8C0"},
      {SettingsPage::Templates, L"模板管理", L"\uE8B7"},
      {SettingsPage::Display, L"显示与位置", L"\uE7F4", true},
      {SettingsPage::Appearance, L"组件外观", L"\uE790"},
      {SettingsPage::Notifications, L"通知", L"\uEA8F", true},
      {SettingsPage::VisualAlerts, L"视觉提醒", L"\uE793"},
      {SettingsPage::General, L"常规", L"\uE713", true},
  };
  for (const auto& section : sections) {
    if (section.newGroup) nav_.MenuItems().Append(NavigationViewItemSeparator());
    NavigationViewItem item;
    item.Content(box_value(hstring(section.title)));
    item.Tag(box_value(static_cast<int>(section.page)));
    FontIcon icon;
    icon.Glyph(section.glyph);
    item.Icon(icon);
    nav_.MenuItems().Append(item);
  }

  Grid content;
  content.MaxWidth(1100);
  content.Margin(Thickness{24, 28, 24, 16});
  for (int i = 0; i < 3; ++i) {
    RowDefinition row;
    row.Height(i < 2 ? GridLengthHelper::Auto()
                     : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    content.RowDefinitions().Append(row);
  }
  pageTitle_ = MakeSectionTitle(L"");
  pageTitle_.Margin(Thickness{0, 0, 0, 14});
  Automation::AutomationProperties::SetHeadingLevel(
      pageTitle_, Automation::Peers::AutomationHeadingLevel::Level1);
  content.Children().Append(pageTitle_);

  BuildScheduleContext();
  Grid::SetRow(scheduleContext_, 1);
  content.Children().Append(scheduleContext_);
  displayPreviewHost_ = Border();
  displayPreviewHost_.Margin(Thickness{0, 0, 0, 12});
  displayPreviewHost_.Visibility(Visibility::Collapsed);
  appearanceTabsHost_ = Border();
  appearanceTabsHost_.Margin(Thickness{0, 0, 0, 12});
  appearanceTabsHost_.Visibility(Visibility::Collapsed);
  StackPanel displayContext;
  Add(displayContext, displayPreviewHost_);
  Add(displayContext, appearanceTabsHost_);
  Grid::SetRow(displayContext, 1);
  content.Children().Append(displayContext);

  pagesHost_ = Grid();
  Grid::SetRow(pagesHost_, 2);
  content.Children().Append(pagesHost_);
  nav_.Content(content);

  nav_.SelectionChanged([this](auto const&, NavigationViewSelectionChangedEventArgs const& args) {
    if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
      ShowSection(static_cast<SettingsPage>(unbox_value<int>(item.Tag())),
                  unbox_value<hstring>(item.Content()));
    }
  });
  window_.Activated([this](auto const&, WindowActivatedEventArgs const& args) {
    if (args.WindowActivationState() == WindowActivationState::Deactivated || !activeSection_) return;
    if (activeSection_ == notificationsScroll_) RefreshNotificationPermission();
    if (ShowsDisplayPreview()) StartPreviewRefresh();
  });
  nav_.SelectedItem(nav_.MenuItems().GetAt(0));
  if (!activeSection_) ShowSection(SettingsPage::Schedule, L"作息计划");
}

StackPanel SettingsWindow::MakePage(ScrollViewer& scroll) {
  ScrollViewer viewer;
  viewer.Visibility(Visibility::Collapsed);
  viewer.VerticalScrollMode(ScrollMode::Enabled);
  viewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  viewer.HorizontalScrollMode(ScrollMode::Disabled);
  viewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  viewer.HorizontalContentAlignment(HorizontalAlignment::Stretch);
  auto stack = StackPanel();
  stack.Spacing(12);
  viewer.Content(stack);
  pagesHost_.Children().Append(viewer);
  scroll = viewer;
  return stack;
}

void SettingsWindow::BuildScheduleContext() {
  scheduleContext_ = StackPanel();
  scheduleContext_.Spacing(8);
  scheduleContext_.Margin(Thickness{0, 0, 0, 12});

  Grid templateRow;
  ColumnDefinition labelColumn;
  labelColumn.Width(GridLengthHelper::Auto());
  templateRow.ColumnDefinitions().Append(labelColumn);
  templateRow.ColumnDefinitions().Append(ColumnDefinition());
  templateRow.ColumnSpacing(12);
  templateRow.HorizontalAlignment(HorizontalAlignment::Left);
  auto label = MakeText(L"模板", 14);
  label.VerticalAlignment(VerticalAlignment::Center);
  templateRow.Children().Append(label);
  templateCombo_ = ComboBox();
  templateCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
  templateCombo_.MinWidth(220);
  templateCombo_.MaxWidth(340);
  Grid::SetColumn(templateCombo_, 1);
  templateRow.Children().Append(templateCombo_);
  Automation::AutomationProperties::SetName(templateCombo_, L"模板");

  planScope_ = RadioButtons();
  planScope_.MaxColumns(2);
  planScope_.Items().Append(box_value(L"常规模板"));
  planScope_.Items().Append(box_value(L"今天的计划"));
  planScope_.SelectedIndex(0);
  Automation::AutomationProperties::SetName(planScope_, L"查看的作息计划");
  Add(scheduleContext_, MakeAdaptiveColumns(templateRow, planScope_, 250, 560));
  templateStatus_ = MakeSubtitle(L"");
  Add(scheduleContext_, templateStatus_);

  scheduleNotice_ = InfoBar();
  scheduleNotice_.Severity(InfoBarSeverity::Success);
  scheduleNotice_.IsClosable(true);
  Add(scheduleContext_, scheduleNotice_);

  templateCombo_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    const int index = templateCombo_.SelectedIndex();
    const auto& config = Services::Instance().config();
    if (index < 0 || index >= static_cast<int>(config.templates.size())) return;
    const std::string templateId = config.templates[index].id;
    CommitTemplateName();
    std::string error;
    if (!Services::Instance().SelectTemplate(templateId, &error)) return;
    RefreshCurrentPage();
  });
  planScope_.SelectionChanged([this](auto const&, auto const&) {
    if (!syncing_) RefreshSegments();
  });
}

void SettingsWindow::BuildSchedulePage() {
  auto page = MakePage(scheduleScroll_);
  BuildWeekdayControls(page);
  Grid actions;
  actions.ColumnDefinitions().Append(ColumnDefinition());
  ColumnDefinition addColumn;
  addColumn.Width(GridLengthHelper::Auto());
  actions.ColumnDefinitions().Append(addColumn);
  actions.ColumnSpacing(12);
  scheduleSummary_ = MakeSubtitle(L"");
  scheduleSummary_.VerticalAlignment(VerticalAlignment::Center);
  actions.Children().Append(scheduleSummary_);

  Button add;
  add.Content(box_value(L"新增时段"));
  StyleAction(add, true);
  add.Click([this](auto const&, auto const&) { OpenNewEditor(); });
  Grid::SetColumn(add, 1);
  actions.Children().Append(add);
  Add(page, actions);

  StackPanel editing;
  editing.Spacing(8);
  segmentList_ = StackPanel();
  segmentList_.Spacing(4);
  Add(editing, segmentList_);
  clearTodayButton_ = Button();
  clearTodayButton_.Content(box_value(L"撤销今日调整"));
  StyleAction(clearTodayButton_, false, true);
  clearTodayButton_.Click([this](auto const&, auto const&) {
    Services::Instance().ClearTodayOverride();
    ShowScheduleNotice(L"已撤销今天的临时调整，恢复按常规模板执行。");
    RefreshCurrentPage();
  });
  Add(editing, clearTodayButton_);
  Add(page, MakeSettingsCard(L"时段安排", editing));
}

void SettingsWindow::BuildWeekdayControls(const StackPanel& page) {
  StackPanel controls;
  controls.Spacing(8);
  weekdayPanel_ = Grid();
  weekdayPanel_.ColumnSpacing(5);
  weekdayPanel_.MaxWidth(412);
  weekdayPanel_.HorizontalAlignment(HorizontalAlignment::Left);
  for (int day = 1; day <= 7; ++day) {
    weekdayPanel_.ColumnDefinitions().Append(ColumnDefinition());
    Primitives::ToggleButton button;
    button.Content(box_value(hstring(kWeekdayLabels[day - 1] + 1)));
    button.Tag(box_value(day));
    button.MinWidth(0);
    button.MinHeight(34);
    button.Padding(Thickness{5, 5, 5, 5});
    button.HorizontalAlignment(HorizontalAlignment::Stretch);
    Automation::AutomationProperties::SetName(button, kWeekdayLabels[day - 1]);
    ToolTipService::SetToolTip(button, box_value(hstring(kWeekdayLabels[day - 1])));
    const auto changed = [this](auto const&, auto const&) {
      if (!syncing_) CommitWeekdays();
    };
    button.Checked(changed);
    button.Unchecked(changed);
    Grid::SetColumn(button, day - 1);
    weekdayPanel_.Children().Append(button);
  }
  Add(controls, weekdayPanel_);

  StackPanel shortcuts;
  shortcuts.Spacing(6);
  const auto addPreset = [this, &shortcuts](const wchar_t* label, std::vector<int> weekdays) {
    Button button;
    button.Content(box_value(label));
    StyleAction(button, false, true);
    button.Click([this, weekdays = std::move(weekdays)](auto const&, auto const&) {
      SaveWeekdays(weekdays);
    });
    Add(shortcuts, button);
  };
  addPreset(L"周一至周五", {1, 2, 3, 4, 5});
  addPreset(L"周末", {6, 7});
  addPreset(L"每天", {1, 2, 3, 4, 5, 6, 7});
  BindActionRow(shortcuts, 250);
  Add(controls, shortcuts);
  weekdayCard_ = MakeSettingsCard(L"模板重复星期", controls);
  Add(page, weekdayCard_);
}

void SettingsWindow::BuildTemplatesPage() {
  auto page = MakePage(templatesScroll_);
  StackPanel fields;
  fields.Spacing(10);
  templateNameBox_ = TextBox();
  templateNameBox_.Header(box_value(L"模板名称"));
  templateNameBox_.PlaceholderText(L"例如：我的高中课表");
  Automation::AutomationProperties::SetName(templateNameBox_, L"模板名称");
  Add(fields, templateNameBox_);
  Add(page, MakeSettingsCard(L"常规模板", fields));

  StackPanel actions;
  actions.Spacing(8);
  Button addTemplate;
  addTemplate.Content(box_value(L"新增模板"));
  StyleAction(addTemplate, true);
  addTemplate.Click([this](auto const&, auto const&) { ShowAddTemplateDialog(); });
  Add(actions, addTemplate);
  restorePresetButton_ = Button();
  restorePresetButton_.Content(box_value(L"恢复默认…"));
  StyleAction(restorePresetButton_, false, true);
  ToolTipService::SetToolTip(restorePresetButton_, box_value(L"仅内置高中和大学模板可恢复预设"));
  restorePresetButton_.Click([this](auto const&, auto const&) { ConfirmRestorePreset(); });
  Add(actions, restorePresetButton_);
  BindActionRow(actions, 300);
  Add(page, actions);

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
}

void SettingsWindow::BuildTimelinePage() {
  auto page = MakePage(timelinePageScroll_);
  auto card = MakeCard();
  card.Child(BuildTimeline());
  Add(page, card);
}

StackPanel SettingsWindow::BuildTimeline() {
  StackPanel timelineStack;
  timelineStack.Spacing(10);
  Add(timelineStack, MakeSubtitle(L"拖动时段或两端调整时间。双击时段可编辑，双击空白处可新增。"));
  StackPanel zoomRow;
  zoomRow.Orientation(Orientation::Horizontal);
  zoomRow.Spacing(12);
  auto zoomLabel = MakeText(L"缩放", 14);
  zoomLabel.VerticalAlignment(VerticalAlignment::Center);
  Add(zoomRow, zoomLabel);
  timelineZoomSlider_ = Slider();
  timelineZoomSlider_.Minimum(40);
  timelineZoomSlider_.Maximum(200);
  timelineZoomSlider_.StepFrequency(10);
  timelineZoomSlider_.Value(timelinePxPerHour_);
  timelineZoomSlider_.Width(160);
  timelineZoomSlider_.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetName(timelineZoomSlider_, L"时间轴缩放");
  Add(zoomRow, timelineZoomSlider_);

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
  StackPanel toolbar;
  toolbar.Spacing(20);
  Add(toolbar, snapRow);
  Add(toolbar, zoomRow);
  BindActionRow(toolbar, 380);
  Add(timelineStack, toolbar);

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
    const int previous = presentation.timelineSnapMinutes;
    presentation.timelineSnapMinutes = index == 0 ? 1 : index == 2 ? 15 : 5;
    if (!Services::Instance().UpdatePresentation(presentation)) {
      const bool wasSyncing = std::exchange(syncing_, true);
      timelineSnapCombo_.IsDropDownOpen(false);
      timelineSnapCombo_.SelectedIndex(previous == 1 ? 0 : previous == 15 ? 2 : 1);
      syncing_ = wasSyncing;
      timelineStatusText_.Text(L"拖动步进保存失败，已恢复上次设置。");
    } else {
      RefreshTimelineScopeLabel();
    }
  });
  // 时间轴拖动：指针事件统一挂在画布上，块只负责发起。
  const auto updateDragRange = [this](const Input::PointerRoutedEventArgs& args) {
    const int minute = TimelineMinute(args.GetCurrentPoint(timelineCanvas_).Position().X);
    const int step = Services::Instance().config().presentation.timelineSnapMinutes;
    const auto range = DraggedTimelineRange({timelineDragStart_, timelineDragEnd_}, timelineDragMode_,
                                             minute - timelineDragAnchorMinute_, step);
    timelineDragCurrentStart_ = range.start;
    timelineDragCurrentEnd_ = range.end;
  };
  timelineCanvas_.PointerMoved([this, updateDragRange](auto const&, Input::PointerRoutedEventArgs const& args) {
    if (timelineDragMode_ == TimelineDragMode::None || !timelineDragPointer_ ||
        args.Pointer().PointerId() != *timelineDragPointer_) return;
    updateDragRange(args);
    const int newStart = timelineDragCurrentStart_;
    const int newEnd = timelineDragCurrentEnd_;

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
                 : L"正在调整（步进 " + std::to_wstring(Services::Instance().config().presentation.timelineSnapMinutes) + L" 分钟）：") +
        FormatClock(rangeStart) + L"–" + FormatClock(rangeEnd)));
    args.Handled(true);
  });
  timelineCanvas_.PointerReleased([this, updateDragRange](auto const&, Input::PointerRoutedEventArgs const& args) {
    if (!timelineDragPointer_ || args.Pointer().PointerId() != *timelineDragPointer_) return;
    updateDragRange(args);
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
  return timelineStack;
}

void SettingsWindow::BuildTimetablePage() {
  auto page = MakePage(timetablePageScroll_);
  StackPanel timetable;
  timetable.Spacing(8);
  Add(timetable, MakeSubtitle(L"选择课表中的时段以打开编辑器。"));
  timetableEmpty_ = MakeSubtitle(L"");
  timetableEmpty_.Visibility(Visibility::Collapsed);
  Add(timetable, timetableEmpty_);
  timetableScroll_ = ScrollViewer();
  timetableScroll_.HorizontalScrollMode(ScrollMode::Enabled);
  timetableScroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
  timetableScroll_.VerticalScrollMode(ScrollMode::Enabled);
  timetableScroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  timetableScroll_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
  timetableScroll_.MaxHeight(430);
  Automation::AutomationProperties::SetName(timetableScroll_, L"课表预览");
  Add(timetable, timetableScroll_);
  auto timetableCard = MakeCard();
  timetableCard.Child(timetable);
  Add(page, timetableCard);

  StackPanel overview;
  overview.Spacing(6);
  todayOverview_ = StackPanel();
  todayOverview_.Spacing(6);
  Add(overview, todayOverview_);
  Add(page, MakeSettingsCard(L"今日概览", overview));
}

void SettingsWindow::BuildDisplayPage() {
  auto page = MakePage(displayScroll_);
  displayNotice_ = InfoBar();
  displayNotice_.Severity(InfoBarSeverity::Error);
  displayNotice_.IsClosable(true);
  displayNotice_.Message(L"显示设置保存失败，已恢复上次设置。");
  Add(page, displayNotice_);
  StackPanel viewStack;
  viewStack.Spacing(10);
  visibleToggle_ = ToggleSwitch();
  Add(viewStack, MakeToggleRow(visibleToggle_, L"显示桌面组件"));
  Add(viewStack, MakeDivider());
  Add(viewStack, MakeText(L"显示形式", 14));
  Add(viewStack, MakeViewChoices(viewSegments_, [this](int index) {
    SelectSegment(viewSegments_, index);
    constexpr Presentation::View views[] = {
        Presentation::View::Bar, Presentation::View::Ring, Presentation::View::ScreenEdge};
    auto presentation = Services::Instance().config().presentation;
    if (presentation.view == views[index]) return;
    presentation.view = views[index];
    SaveDisplaySettings(presentation);
  }));
  Add(viewStack, MakeDivider());
  Add(viewStack, MakeText(L"显示范围", 14));
  Add(viewStack, MakeSegments(scopeSegments_, {L"全天", L"当前时段"}, [this](int index) {
    SelectSegment(scopeSegments_, index);
    const auto scope = index == 0 ? Presentation::Scope::AllDay : Presentation::Scope::CurrentBlock;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.scope == scope) return;
    presentation.scope = scope;
    SaveDisplaySettings(presentation);
  }));
  Add(page, MakeSettingsCard(L"桌面组件", viewStack));

  auto locationStack = StackPanel();
  locationStack.Spacing(10);
  monitorCombo_ = ComboBox();
  monitorCombo_.Header(box_value(L"组件所在显示器"));
  monitorCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
  Automation::AutomationProperties::SetName(monitorCombo_, L"组件所在显示器");
  Add(locationStack, monitorCombo_);
  monitorHint_ = MakeSubtitle(L"");
  Add(locationStack, monitorHint_);
  dockControls_ = StackPanel();
  dockControls_.Spacing(8);
  Add(dockControls_, MakeDivider());
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
    SaveDisplaySettings(presentation);
  }));
  reanchorButton_ = Button();
  reanchorButton_.Content(box_value(hstring(L"恢复居中位置")));
  StyleAction(reanchorButton_, false, true);
  reanchorButton_.HorizontalAlignment(HorizontalAlignment::Left);
  reanchorButton_.Click([this](auto const&, auto const&) {
    auto presentation = Services::Instance().config().presentation;
    presentation.visible = true;
    presentation.offsetX = 0;
    presentation.offsetY = 0;
    SaveDisplaySettings(presentation);
  });
  Add(dockControls_, reanchorButton_);
  Add(locationStack, dockControls_);
  Add(page, MakeSettingsCard(L"位置设置", locationStack));

  StackPanel behaviorStack;
  behaviorStack.Spacing(10);
  hideInFullscreenToggle_ = ToggleSwitch();
  Add(behaviorStack, MakeToggleRow(
      hideInFullscreenToggle_, L"全屏应用时自动隐藏",
      L"目标显示器有全屏应用时临时隐藏组件，不影响后台提醒"));
  Add(behaviorStack, MakeDivider());
  coverTaskbarToggle_ = ToggleSwitch();
  Add(behaviorStack, MakeToggleRow(
      coverTaskbarToggle_, L"允许覆盖任务栏",
      L"贴合完整屏幕四边或将组件覆于任务栏上方；仍遵循开始菜单与系统层级"));
  dockBehavior_ = StackPanel();
  dockBehavior_.Spacing(10);
  Add(dockBehavior_, MakeDivider());
  flushToggle_ = ToggleSwitch();
  Add(dockBehavior_, MakeToggleRow(flushToggle_, L"紧贴边缘", L"关闭后保留一点间距"));
  Add(dockBehavior_, MakeDivider());
  edgeAutoHideToggle_ = ToggleSwitch();
  Add(dockBehavior_, MakeToggleRow(edgeAutoHideToggle_, L"贴边自动隐藏", L"鼠标靠近时展开组件"));
  Add(behaviorStack, dockBehavior_);
  Add(page, MakeSettingsCard(L"全屏与贴边行为", behaviorStack));

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
    SaveDisplaySettings(presentation);
  });
  visibleToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.visible = visibleToggle_.IsOn();
    SaveDisplaySettings(presentation);
  });
  hideInFullscreenToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                          [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.hideInFullscreen == hideInFullscreenToggle_.IsOn()) return;
    presentation.hideInFullscreen = hideInFullscreenToggle_.IsOn();
    SaveDisplaySettings(presentation);
  });
  coverTaskbarToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    if (presentation.coverTaskbar == coverTaskbarToggle_.IsOn()) return;
    presentation.coverTaskbar = coverTaskbarToggle_.IsOn();
    SaveDisplaySettings(presentation);
  });
  edgeAutoHideToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                                      [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.autoHideAtEdge = edgeAutoHideToggle_.IsOn();
    SaveDisplaySettings(presentation);
  });
  flushToggle_.RegisterPropertyChangedCallback(ToggleSwitch::IsOnProperty(),
                                               [this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.flushWithEdge = flushToggle_.IsOn();
    SaveDisplaySettings(presentation);
  });
}

void SettingsWindow::BuildAppearancePage() {
  auto page = MakePage(appearanceScroll_);
  page.Spacing(12);
  Grid navigation;
  auto sections = std::make_shared<std::array<StackPanel, 3>>();
  auto navigationHost = std::make_shared<weak_ref<Grid>>();
  appearanceTabsHost_.Child(MakeSegments(navigation, {L"款式与尺寸", L"皮肤与配色", L"套装"},
      [this, navigationHost, sections](int index) {
        if (const auto grid = navigationHost->get()) SelectSegment(grid, index);
        for (size_t i = 0; i < sections->size(); ++i) {
          (*sections)[i].Visibility(i == static_cast<size_t>(index)
                                        ? Visibility::Visible : Visibility::Collapsed);
        }
        appearanceScroll_.ChangeView(nullptr, box_value(0.0).as<IReference<double>>(), nullptr,
                                     true);
      }));
  *navigationHost = make_weak(navigation);
  SelectSegment(navigation, 0);
  appearanceNotice_ = InfoBar();
  appearanceNotice_.IsClosable(true);
  Add(page, appearanceNotice_);
  for (size_t i = 0; i < sections->size(); ++i) {
    auto& section = (*sections)[i];
    section.Spacing(12);
    section.Visibility(i == 0 ? Visibility::Visible : Visibility::Collapsed);
    Add(page, section);
  }

  BuildWidgetShapeOptions((*sections)[0]);
  BuildWidgetPaletteOptions((*sections)[1]);
  BuildWidgetOutfits((*sections)[2]);
}

void SettingsWindow::BuildWidgetShapeOptions(const StackPanel& page) {
  Add(page, MakeSettingsCard(L"组件形态", [&] {
    StackPanel shapes;
    Add(shapes, MakeViewChoices(widgetViewChoices_, [this](int index) {
      CommitWidgetStyle(static_cast<WidgetView>(index));
    }));
    return shapes;
  }()));

  const auto dimension = [this](const StackPanel& body, const wchar_t* name,
                               int WidgetStyle::*field, int minimum, int maximum) {
    auto label = MakeText(name, 14);
    Add(body, label);
    Grid row;
    row.ColumnSpacing(12);
    ColumnDefinition flexible;
    flexible.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition fixed;
    fixed.Width(GridLengthHelper::FromValueAndType(96, GridUnitType::Pixel));
    row.ColumnDefinitions().Append(flexible);
    row.ColumnDefinitions().Append(fixed);
    Slider slider;
    slider.Minimum(minimum);
    slider.Maximum(maximum);
    slider.StepFrequency(1);
    slider.HorizontalAlignment(HorizontalAlignment::Stretch);
    slider.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetLabeledBy(slider, label);
    row.Children().Append(slider);
    NumberBox value;
    value.Minimum(minimum);
    value.Maximum(maximum);
    value.SmallChange(1);
    value.LargeChange(10);
    value.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
    Grid::SetColumn(value, 1);
    Automation::AutomationProperties::SetName(value, hstring(std::wstring(name) + L"（DIP）"));
    row.Children().Append(value);
    Add(body, row);
    const auto change = [this, field](double number) {
      if (syncing_ || !std::isfinite(number)) return;
      auto style = EditableWidgetStyle();
      const int next = static_cast<int>(std::lround(number));
      if (style.*field == next) {
        if (number != next) RefreshAppearanceControls();
        return;
      }
      style.*field = next;
      EditWidgetStyle(std::move(style), true);
    };
    slider.ValueChanged([change](auto const&, const auto& args) { change(args.NewValue()); });
    value.ValueChanged([change](auto const&, const auto& args) { change(args.NewValue()); });
    widgetDimensions_.push_back({field, slider, value});
  };
  const auto variants = [this](const StackPanel& body, WidgetView view,
                              const std::array<const wchar_t*, 3>& names) {
    std::vector<GalleryItem> tiles;
    for (size_t i = 0; i < names.size(); ++i) {
      WidgetStyle style;
      if (view == WidgetView::Bar) style.barStyle = static_cast<WidgetStyle::BarStyle>(i);
      if (view == WidgetView::Ring) style.ringStyle = static_cast<WidgetStyle::RingStyle>(i);
      if (view == WidgetView::ScreenEdge) {
        style.screenEdgeStyle = static_cast<WidgetStyle::ScreenEdgeStyle>(i);
      }
      tiles.emplace_back(names[i], WidgetThumbnail(view, style));
    }
    auto gallery = MakeAppearanceGallery(L"组件款式", tiles, 3, [this, view](int index) {
      auto style = EditableWidgetStyle();
      if (view == WidgetView::Bar) style.barStyle = static_cast<WidgetStyle::BarStyle>(index);
      if (view == WidgetView::Ring) style.ringStyle = static_cast<WidgetStyle::RingStyle>(index);
      if (view == WidgetView::ScreenEdge) {
        style.screenEdgeStyle = static_cast<WidgetStyle::ScreenEdgeStyle>(index);
      }
      EditWidgetStyle(std::move(style));
    });
    widgetStyleChoices_.push_back(gallery);
    Add(body, gallery);
  };
  const auto addGroup = [&](const StackPanel& body, WidgetView view) {
    auto reset = AppearanceButton(L"恢复此形态的默认设置");
    reset.Click([this, view](auto const&, auto const&) {
      auto style = EditableWidgetStyle();
      const WidgetStyle defaults;
      switch (view) {
        case WidgetView::Bar:
          style.barStyle = defaults.barStyle;
          style.barLengthDip = defaults.barLengthDip;
          style.barThicknessDip = defaults.barThicknessDip;
          style.barCornerDip = defaults.barCornerDip;
          style.barGapDip = defaults.barGapDip;
          break;
        case WidgetView::Ring:
          style.ringStyle = defaults.ringStyle;
          style.ringReadout = defaults.ringReadout;
          style.ringDiameterDip = defaults.ringDiameterDip;
          style.ringStrokeDip = defaults.ringStrokeDip;
          break;
        case WidgetView::ScreenEdge:
          style.screenEdgeStyle = defaults.screenEdgeStyle;
          style.screenEdgeThickness = defaults.screenEdgeThickness;
          style.screenEdgeInsetDip = defaults.screenEdgeInsetDip;
          style.screenEdgeColors = defaults.screenEdgeColors;
          style.screenEdgeGradient = defaults.screenEdgeGradient;
          style.screenEdgeHighlight = defaults.screenEdgeHighlight;
          break;
      }
      EditWidgetStyle(std::move(style));
    });
    Add(body, reset);
    auto card = MakeSettingsCard(WidgetViewName(view), body);
    Add(page, card);
    widgetShapeGroups_.push_back(card);
  };

  StackPanel bar;
  bar.Spacing(9);
  variants(bar, WidgetView::Bar, {L"经典分节", L"色块", L"细线"});
  dimension(bar, L"长条长度", &WidgetStyle::barLengthDip, WidgetStyle::kMinBarLength,
            WidgetStyle::kMaxBarLength);
  dimension(bar, L"长条厚度", &WidgetStyle::barThicknessDip, WidgetStyle::kMinBarThickness,
            WidgetStyle::kMaxBarThickness);
  dimension(bar, L"外壳圆角", &WidgetStyle::barCornerDip, 0, WidgetStyle::kMaxBarCorner);
  dimension(bar, L"分节间距", &WidgetStyle::barGapDip, 0, WidgetStyle::kMaxBarGap);
  addGroup(bar, WidgetView::Bar);

  StackPanel ring;
  ring.Spacing(9);
  variants(ring, WidgetView::Ring, {L"经典圆环", L"刻度仪表", L"圆角方盘"});
  dimension(ring, L"盘面大小", &WidgetStyle::ringDiameterDip, WidgetStyle::kMinRingDiameter,
            WidgetStyle::kMaxRingDiameter);
  dimension(ring, L"环线粗细", &WidgetStyle::ringStrokeDip, WidgetStyle::kMinRingStroke,
            WidgetStyle::kMaxRingStroke);
  Add(ring, MakeText(L"中心读数", 14));
  Add(ring, MakeSegments(ringReadoutChoices_, {L"剩余时间", L"进度百分比", L"结束时间"},
      [this](int index) {
        auto style = EditableWidgetStyle();
        style.ringReadout = static_cast<WidgetStyle::RingReadout>(index);
        EditWidgetStyle(std::move(style));
      }));
  addGroup(ring, WidgetView::Ring);

  StackPanel edge;
  edge.Spacing(9);
  variants(edge, WidgetView::ScreenEdge, {L"实线", L"双轨", L"柔光"});
  dimension(edge, L"边框粗细", &WidgetStyle::screenEdgeThickness, WidgetStyle::kMinScreenEdgeThickness,
            WidgetStyle::kMaxScreenEdgeThickness);
  dimension(edge, L"离屏幕边缘", &WidgetStyle::screenEdgeInsetDip, 0, WidgetStyle::kMaxScreenEdgeInset);
  screenEdgeColorPanel_ = StackPanel();
  screenEdgeColorPanel_.Spacing(8);
  Add(screenEdgeColorPanel_, MakeText(L"全天配色", 14));
  Add(screenEdgeColorPanel_, MakeSegments(screenEdgeColorSegments_, {L"按时段分色", L"跟随当前时段"},
      [this](int index) {
        auto style = EditableWidgetStyle();
        style.screenEdgeColors = static_cast<WidgetStyle::ScreenEdgeColors>(index);
        EditWidgetStyle(std::move(style));
      }));
  const auto toggle = [this](const StackPanel& host, ToggleSwitch& control, const wchar_t* name,
                             const wchar_t* detail, bool WidgetStyle::*field) {
    control = ToggleSwitch();
    auto row = MakeToggleRow(control, name, detail);
    Add(host, row);
    control.Toggled([this, field](auto const& sender, auto const&) {
      if (syncing_) return;
      auto style = EditableWidgetStyle();
      style.*field = sender.template as<ToggleSwitch>().IsOn();
      EditWidgetStyle(std::move(style));
    });
    return row;
  };
  screenEdgeGradientRow_ = toggle(screenEdgeColorPanel_, screenEdgeGradientToggle_,
      L"平滑颜色交界", L"在相邻时段之间渐变", &WidgetStyle::screenEdgeGradient);
  Add(edge, screenEdgeColorPanel_);
  toggle(edge, screenEdgeHighlightToggle_, L"高亮进度前沿",
         L"用柔光标出当前位置", &WidgetStyle::screenEdgeHighlight);
  addGroup(edge, WidgetView::ScreenEdge);
}

void SettingsWindow::BuildWidgetPaletteOptions(const StackPanel& page) {
  StackPanel skins;
  skins.Spacing(8);
  widgetSkinSummary_ = MakeSubtitle(L"");
  Add(skins, widgetSkinSummary_);
  std::vector<GalleryItem> tiles;
  for (const auto& skin : WidgetSkins()) tiles.emplace_back(skin.name, SkinThumbnail(skin.colors));
  widgetSkinChoices_ = MakeAppearanceGallery(L"组件皮肤", tiles, 4, [this](int index) {
    auto style = EditableWidgetStyle();
    style.colors = WidgetSkins()[index].colors;
    EditWidgetStyle(std::move(style));
  });
  Add(skins, widgetSkinChoices_);
  Add(page, MakeSettingsCard(L"组件皮肤", skins));

  StackPanel colors;
  colors.Spacing(8);
  tiles.clear();
  for (const auto& field : kWidgetColorFields) tiles.emplace_back(field.name, ColorSwatch(26, 17));
  widgetColorChoices_ = MakeAppearanceGallery(L"选择要调整的颜色", tiles, 4, [this](int index) {
    widgetColorTarget_ = static_cast<size_t>(index);
    RefreshWidgetColors();
  });
  Add(colors, widgetColorChoices_);
  widgetColorLabel_ = MakeSubtitle(L"");
  Add(colors, widgetColorLabel_);
  widgetColorPicker_ = ColorPicker();
  widgetColorPicker_.IsAlphaEnabled(false);
  widgetColorPicker_.IsHexInputVisible(true);
  widgetColorPicker_.IsColorPreviewVisible(true);
  widgetColorPicker_.IsMoreButtonVisible(false);
  widgetColorPicker_.HorizontalAlignment(HorizontalAlignment::Left);
  auto picker = Expander();
  picker.Header(box_value(L"打开取色器"));
  picker.HorizontalAlignment(HorizontalAlignment::Stretch);
  picker.Content(widgetColorPicker_);
  Add(colors, picker);
  widgetColorPicker_.ColorChanged([this](auto const&, const auto& args) {
    if (syncing_) return;
    auto style = EditableWidgetStyle();
    style.colors.*kWidgetColorFields[widgetColorTarget_].field =
        WidgetAppearance::ColorToHex(args.NewColor());
    EditWidgetStyle(std::move(style), true);
  });
  auto reset = AppearanceButton(L"恢复所选颜色默认值");
  reset.Click([this](auto const&, auto const&) {
    auto style = EditableWidgetStyle();
    (style.colors.*kWidgetColorFields[widgetColorTarget_].field).clear();
    EditWidgetStyle(std::move(style));
  });
  Add(colors, reset);
  Add(page, MakeSettingsCard(L"逐项配色", colors));
}

void SettingsWindow::BuildWidgetOutfits(const StackPanel& page) {
  StackPanel builtins;
  builtins.Spacing(8);
  Add(builtins, MakeSubtitle(L"套装同时切换组件形态、款式、尺寸与配色。"));
  std::vector<GalleryItem> tiles;
  for (const auto& outfit : BuiltinOutfits()) {
    tiles.emplace_back(ToWide(outfit.name), WidgetThumbnail(outfit.view, outfit.style));
  }
  outfitChoices_ = MakeAppearanceGallery(L"内置套装", tiles, 3, [this](int index) {
    ApplyOutfit(BuiltinOutfits()[index]);
  });
  Add(builtins, outfitChoices_);
  Add(page, MakeSettingsCard(L"内置套装", builtins));

  StackPanel saved;
  saved.Spacing(8);
  auto save = AppearanceButton(L"把当前外观存为套装");
  save.Click([this](auto const&, auto const&) { EditSavedOutfitName(false); });
  Add(saved, save);
  savedOutfitCombo_ = ComboBox();
  savedOutfitCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
  savedOutfitCombo_.PlaceholderText(L"还没有保存的套装");
  Automation::AutomationProperties::SetName(savedOutfitCombo_, L"我的套装");
  savedOutfitCombo_.SelectionChanged([this](auto const&, auto const&) {
    if (!syncing_) RefreshSavedOutfitPreview();
  });
  Add(saved, savedOutfitCombo_);
  savedOutfitPreview_ = MakeCard();
  savedOutfitPreview_.Padding({10, 8, 10, 8});
  Add(saved, savedOutfitPreview_);
  savedOutfitActions_ = StackPanel();
  savedOutfitActions_.Spacing(6);
  auto apply = AppearanceButton(L"应用所选套装");
  apply.Click([this](auto const&, auto const&) {
    const auto& outfits = Services::Instance().config().widgetOutfits;
    const int index = savedOutfitCombo_.SelectedIndex();
    if (index >= 0 && static_cast<size_t>(index) < outfits.size()) ApplyOutfit(outfits[index]);
  });
  Add(savedOutfitActions_, apply);
  auto rename = AppearanceButton(L"重命名所选套装");
  rename.Click([this](auto const&, auto const&) { EditSavedOutfitName(true); });
  Add(savedOutfitActions_, rename);
  auto remove = AppearanceButton(L"删除所选套装");
  remove.Click([this](auto const&, auto const&) { RemoveSavedOutfit(); });
  Add(savedOutfitActions_, remove);
  Add(saved, savedOutfitActions_);
  Add(page, MakeSettingsCard(L"我的套装", saved));
  RefreshSavedOutfits();
}

void SettingsWindow::BuildDisplayPreview() {
  auto previewCard = MakeCard();
  previewCard.Padding(Thickness{16, 12, 16, 12});
  previewCard.VerticalAlignment(VerticalAlignment::Top);
  auto previewContent = StackPanel();
  previewContent.Spacing(6);
  Grid heading;
  ColumnDefinition space;
  space.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ColumnDefinition action;
  action.Width(GridLengthHelper::Auto());
  heading.ColumnDefinitions().Append(space);
  heading.ColumnDefinitions().Append(action);
  auto title = MakeCardTitle(L"实时预览");
  title.VerticalAlignment(VerticalAlignment::Center);
  heading.Children().Append(title);
  Primitives::ToggleButton sample;
  sample.Content(box_value(L"示例进度"));
  sample.Padding({8, 5, 8, 5});
  Grid::SetColumn(sample, 1);
  heading.Children().Append(sample);
  Automation::AutomationProperties::SetHelpText(sample, L"用示例课表预览外观");
  sample.RegisterPropertyChangedCallback(
      Primitives::ToggleButton::IsCheckedProperty(),
      [this](const DependencyObject& sender, const DependencyProperty&) {
    const auto checked = sender.as<Primitives::ToggleButton>().IsChecked();
    previewSample_ = checked && checked.Value();
    RefreshDisplayPreview(Services::Instance().config().presentation);
    RefreshPreviewSummary();
  });
  Add(previewContent, heading);
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
  previewDetailScale.MaxHeight(102);
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
  const auto makeStage = [] {
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
  previewScale.MaxHeight(102);
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
  displayPreviewHost_.Child(previewCard);
}

void SettingsWindow::BuildNotificationPage() {
  auto notificationsStack = MakePage(notificationsScroll_);
  {
    auto card = MakeCard();
    StackPanel stack;
    stack.Spacing(10);
    muteToggle_ = ToggleSwitch();
    Add(stack, MakeToggleRow(muteToggle_, L"临时暂停提醒",
                             L"暂停时作息进度继续。", L"\uE769"));
    Add(stack, MakeDivider());
    notificationsToggle_ = ToggleSwitch();
    Add(stack, MakeToggleRow(notificationsToggle_, L"启用作息提醒", L"", L"\uEA8F"));
    notificationStateText_ = MakeSubtitle(L"");
    Add(stack, notificationStateText_);
    card.Child(stack);
    Add(notificationsStack, card);

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
    BindActionRow(buttons, 470);
    Add(checkStack, buttons);

    testResultText_ = MakeSubtitle(L"尚未发送测试通知。");
    Add(checkStack, testResultText_);
    Add(checkStack, MakeSubtitle(L"系统是否显示横幅取决于 Windows 的通知与勿扰设置。"));

    checkCard.Child(checkStack);

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
    Add(notificationsStack, MakeSettingsCard(L"提醒时机", timingStack));
    Add(notificationsStack, checkCard);
  }

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
}

void SettingsWindow::BuildVisualAlertsPage() {
  auto page = MakePage(visualAlertsScroll_);
  StackPanel effects;
  effects.Spacing(8);
  screenEdgeFlashToggle_ = ToggleSwitch();
  Add(effects, MakeToggleRow(
      screenEdgeFlashToggle_, L"全屏边缘闪光提醒",
      L"到点时在目标显示器四边发出 3 次柔和阶段色呼吸光带，鼠标点击完全穿透", L"\uE793"));
  Add(effects, MakeDivider());
  dropClockToggle_ = ToggleSwitch();
  Add(effects, MakeToggleRow(
      dropClockToggle_, L"掉落 3D 放大时钟提醒",
      L"到点时从屏幕顶部/边角掉落并放大 3D 立体时钟，可调整大小和停留时间，悬停暂留、点击收回", L"\uE823"));

  Add(page, MakeSettingsCard(L"到点效果", effects));

  StackPanel preview;
  preview.Spacing(8);
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
  visualPreviewFit.MaxHeight(170);
  visualPreviewFit.Stretch(Stretch::Uniform);
  visualPreviewCanvas_ = Canvas();
  visualPreviewCanvas_.Width(420);
  visualPreviewCanvas_.Height(206);
  visualPreviewFit.Child(visualPreviewCanvas_);
  visualPreviewFrame.Child(visualPreviewFit);
  Add(preview, visualPreviewFrame);
  visualPreviewCaption_ = MakeSubtitle(L"");
  Add(preview, visualPreviewCaption_);

  auto testVisual = Button();
  testVisual.Content(box_value(hstring(L"测试视觉提醒")));
  StyleAction(testVisual);
  testVisual.Click([this](auto const&, auto const&) {
    CommitWidgetStyle();
    CommitAlertColors();
    const bool triggered = testVisualAlertRequested_ && testVisualAlertRequested_();
    visualTestResultText_.Text(hstring(
        triggered ? L"已触发预览：边缘柔光不遮挡画面，悬停 3D 时钟可暂停收回、点击可立即关闭。"
                  : L"请先开启“全屏边缘闪光提醒”或“掉落 3D 放大时钟提醒”后再预览。"));
  });
  Add(preview, testVisual);
  visualTestResultText_ = MakeSubtitle(
      L"作为系统通知的补充呈现，遵守“启用作息提醒”“临时暂停”及全屏隐藏设置。");
  Add(preview, visualTestResultText_);
  Add(page, MakeSettingsCard(L"预览与测试", preview));

  StackPanel clockOptions;
  clockOptions.Spacing(10);
  Add(clockOptions, MakeText(L"3D 时钟样式", 14));
  std::vector<GalleryItem> clockStyles;
  for (int index = 0; index < static_cast<int>(std::size(kClockNames)); ++index) {
    clockStyles.emplace_back(kClockNames[index], ClockStyleThumbnail(static_cast<ClockStyle>(index)));
  }
  dropClockStyleSegments_ = MakeAppearanceGallery(L"3D 时钟样式", clockStyles, 4,
      [this](int index) {
        SelectSegment(dropClockStyleSegments_, index);
        const auto style = static_cast<ClockStyle>(index);
        auto settings = Services::Instance().config().notifications;
        if (settings.dropClockStyle == style) return;
        settings.dropClockStyle = style;
        Services::Instance().UpdateNotifications(settings);
      });
  Add(clockOptions, dropClockStyleSegments_);
  Add(clockOptions, MakeText(L"时钟大小", 14));
  Add(clockOptions, MakeSegments(dropClockScaleSegments_, {L"75%", L"100%", L"125%"},
      [this](int index) {
        SelectSegment(dropClockScaleSegments_, index);
        auto settings = Services::Instance().config().notifications;
        if (settings.dropClockScalePercent == kClockScales[index]) return;
        settings.dropClockScalePercent = kClockScales[index];
        Services::Instance().UpdateNotifications(settings);
      }));
  Add(clockOptions, MakeText(L"停留时间", 14));
  Add(clockOptions, MakeSegments(dropClockHoldSegments_, {L"2 秒", L"4 秒", L"8 秒"},
      [this](int index) {
        SelectSegment(dropClockHoldSegments_, index);
        auto settings = Services::Instance().config().notifications;
        if (settings.dropClockHoldSeconds == kClockHolds[index]) return;
        settings.dropClockHoldSeconds = kClockHolds[index];
        Services::Instance().UpdateNotifications(settings);
      }));
  Add(clockOptions, MakeText(L"掉落位置", 14));
  Add(clockOptions, MakeSegments(
      dropClockPositionSegments_, {L"顶部居中", L"右上角", L"左上角"}, [this](int index) {
        SelectSegment(dropClockPositionSegments_, index);
        using Pos = classtime::NotificationSettings::AlertClockPosition;
        constexpr Pos positions[] = {Pos::TopCenter, Pos::TopRight, Pos::TopLeft};
        auto settings = Services::Instance().config().notifications;
        if (settings.dropClockPosition == positions[index]) return;
        settings.dropClockPosition = positions[index];
        Services::Instance().UpdateNotifications(settings);
      }));
  Add(clockOptions, MakeText(L"3D 时钟皮肤", 14));
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
      BindSegmentChoice(btn, i, [this, chosenSkin](int index) {
        SelectSegment(dropClockSkinGrid_, index);
        auto settings = Services::Instance().config().notifications;
        if (settings.dropClockSkin == chosenSkin) return;
        settings.dropClockSkin = chosenSkin;
        Services::Instance().UpdateNotifications(settings);
      });
      Grid::SetRow(btn, i / 3);
      Grid::SetColumn(btn, i % 3);
      dropClockSkinGrid_.Children().Append(btn);
    }
    Add(clockOptions, dropClockSkinGrid_);
  }
  dropClockOptionsCard_ = MakeSettingsCard(L"放大时钟", clockOptions);
  Add(page, dropClockOptionsCard_);

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
  resetAlertColors.Content(box_value(hstring(L"恢复默认颜色")));
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
  Add(page, MakeSettingsCard(L"提醒配色", alertColorBody));

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

}

void SettingsWindow::BuildGeneralPage() {
  auto generalStack = MakePage(generalScroll_);
  {
    auto windowCard = MakeCard();
    auto windowStack = StackPanel();
    windowStack.Spacing(10);
    Add(windowStack, MakeCardTitle(L"启动与窗口"));
    startupToggle_ = ToggleSwitch();
    Add(windowStack, MakeToggleRow(startupToggle_, L"登录时自动启动"));
    startupStatus_ = MakeSubtitle(L"");
    startupStatus_.Visibility(Visibility::Collapsed);
    Add(windowStack, startupStatus_);
    Add(windowStack,
        MakeSubtitle(L"关闭设置窗口时应用继续后台运行，提醒照常；托盘图标可重新打开设置或退出程序。"));
    windowCard.Child(windowStack);
    Add(generalStack, windowCard);

    auto dataCard = MakeCard();
    auto dataStack = StackPanel();
    dataStack.Spacing(10);
    Add(dataStack, MakeCardTitle(L"本地数据"));
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
    Add(generalStack, dataCard);

    StackPanel aboutStack;
    versionText_ = MakeText(L"", 14);
    Add(aboutStack, versionText_);
    Add(generalStack, MakeSettingsCard(L"关于“上课”时间到！", aboutStack));

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

  startupToggle_.Toggled([this](auto const&, auto const&) {
    if (!syncing_) ChangeLoginStartup(startupToggle_.IsOn());
  });
}

void SettingsWindow::BuildEditor() {
  editorOverlay_ = Grid();
  editorOverlay_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(94, 0, 0, 0)));
  editorOverlay_.Visibility(Visibility::Collapsed);
  rootGrid_.Children().Append(editorOverlay_);

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

  Grid timeRow;
  timeRow.ColumnDefinitions().Append(ColumnDefinition());
  timeRow.ColumnDefinitions().Append(ColumnDefinition());
  timeRow.ColumnSpacing(12);
  editorStart_ = TextBox();
  editorEnd_ = TextBox();
  auto startField =
      MakeTimeField(editorStart_, L"开始时间", L"开始（HH:mm）", L"选择开始时间", L"08:00");
  auto endField =
      MakeTimeField(editorEnd_, L"结束时间", L"结束（HH:mm）", L"选择结束时间", L"08:45");
  Grid::SetColumn(endField, 1);
  timeRow.Children().Append(startField);
  timeRow.Children().Append(endField);
  Add(editorStack, timeRow);

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
    RefreshCurrentPage();
  });
  deleteButton_.Click([this](auto const&, auto const&) {
    if (!editorOpen_ || newSegment_) return;
    ConfirmEditorDelete();
  });
  cancel.Click([this](auto const&, auto const&) { CloseEditor(); });
}

void SettingsWindow::Show() {
  if (!editorOpen_) RefreshCurrentPage();
  window_.Activate();
  window_.Activate();
  if (hwnd_) {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
  }
  if (ShowsDisplayPreview()) StartPreviewRefresh();
}

void SettingsWindow::ApplyState(bool presentationOnly) {
  if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_) || editorOpen_) return;
  if (!presentationOnly || ShowsDisplayPreview()) RefreshCurrentPage();
}

void SettingsWindow::RefreshCurrentPage() {
  if (!activeSection_) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  if (ShowsScheduleContext()) {
    RefreshTemplates();
    if (activeSection_ != templatesScroll_) RefreshSegments();
  } else if (ShowsDisplayPreview()) {
    if (activeSection_ == displayScroll_) RefreshDisplayControls();
    else RefreshAppearanceControls();
    RefreshDisplayPreview(Services::Instance().config().presentation);
    RefreshPreviewSummary();
  } else if (activeSection_ == notificationsScroll_) {
    RefreshNotificationControls();
  } else if (activeSection_ == visualAlertsScroll_) {
    RefreshVisualAlertControls();
  } else if (activeSection_ == generalScroll_) {
    RefreshGeneralControls();
  }
  syncing_ = wasSyncing;
}

void SettingsWindow::RefreshTemplates() {
  const auto& config = Services::Instance().config();
  if (restorePresetButton_) {
    restorePresetButton_.IsEnabled(config.selectedTemplateId == "high-school" ||
                                   config.selectedTemplateId == "university");
  }
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
    templateStatus_.Text(L"今天按临时计划执行。");
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
  const bool visible = schedule && planScope_.SelectedIndex() != 1;
  weekdayCard_.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
  if (!visible) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  for (const auto& child : weekdayPanel_.Children()) {
    if (auto check = child.try_as<Primitives::ToggleButton>()) {
      const int day = unbox_value<int>(check.Tag());
      check.IsChecked(schedule->IsActiveOn(day));
    }
  }
  syncing_ = wasSyncing;
}

void SettingsWindow::RefreshSegments() {
  const bool permanent = planScope_.SelectedIndex() != 1;
  const auto sorted = classtime::SortedByStart(EditorBaseSegments(permanent));
  if (activeSection_ && activeSection_ == timelinePageScroll_) {
    const bool wasSyncing = std::exchange(syncing_, true);
    const int snap = Services::Instance().config().presentation.timelineSnapMinutes;
    timelineSnapCombo_.SelectedIndex(snap == 1 ? 0 : snap == 15 ? 2 : 1);
    syncing_ = wasSyncing;
    RefreshTimeline(sorted);
    return;
  }
  if (activeSection_ && activeSection_ == timetablePageScroll_) {
    RefreshTimetable(sorted, permanent);
    RefreshTodayOverview();
    return;
  }
  if (!activeSection_ || activeSection_ != scheduleScroll_) return;

  RefreshWeekdays();
  RefreshTodayOverview();
  segmentList_.Children().Clear();
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
      auto heading = MakeText(group, 14);
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
    badgeText.HorizontalAlignment(HorizontalAlignment::Center);
    badge.Child(badgeText);
    grid.Children().Append(badge);

    Grid editLabel;
    editLabel.ColumnDefinitions().Append(ColumnDefinition());
    ColumnDefinition editIconColumn;
    editIconColumn.Width(GridLengthHelper::Auto());
    editLabel.ColumnDefinitions().Append(editIconColumn);
    editLabel.ColumnSpacing(8);
    StackPanel editText;
    editText.Spacing(1);
    auto time = MakeText(FormatRange(segment), 20);
    time.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    Add(editText, time);
    Add(editText, MakeSubtitle(ToWide(segment.label) + L" · " + FormatMinutes(segment.DurationMinutes())));
    editLabel.Children().Append(editText);
    SymbolIcon editIcon;
    editIcon.Symbol(Symbol::Edit);
    editIcon.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(editIcon, 1);
    editLabel.Children().Append(editIcon);

    Button edit;
    edit.Content(editLabel);
    StyleAction(edit, false, true);
    edit.Padding(Thickness{8, 2, 8, 2});
    edit.HorizontalAlignment(HorizontalAlignment::Stretch);
    edit.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    edit.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(edit, hstring(L"编辑 " + ToWide(segment.label)));
    Automation::AutomationProperties::SetHelpText(edit, hstring(FormatRange(segment)));
    edit.Click([this, segment](auto const&, auto const&) { OpenEditor(segment); });
    Grid::SetColumn(edit, 1);
    grid.Children().Append(edit);
    auto duplicate = Button();
    duplicate.Content(box_value(hstring(L"复制")));
    StyleAction(duplicate, false, true);
    duplicate.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(duplicate,
                                             hstring(L"复制 " + ToWide(segment.label)));
    duplicate.Click([this, segment](auto const&, auto const&) { OpenDuplicateEditor(segment); });
    Grid::SetColumn(duplicate, 2);
    grid.Children().Append(duplicate);

    row.Child(grid);
    segmentList_.Children().Append(row);
  }
}

void SettingsWindow::RefreshTimetable(const std::vector<classtime::Segment>& segments,
                                      bool permanent) {
  if (segments.empty()) {
    timetableScroll_.Content(nullptr);
    timetableScroll_.Visibility(Visibility::Collapsed);
    timetableEmpty_.Text(permanent ? L"模板还没有安排时段。" : L"今天没有安排的时段。");
    timetableEmpty_.Visibility(Visibility::Visible);
    return;
  }
  timetableEmpty_.Visibility(Visibility::Collapsed);
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
  RefreshTimelineScopeLabel();

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
    block.label.TextWrapping(TextWrapping::NoWrap);
    block.label.TextTrimming(TextTrimming::CharacterEllipsis);
    content.Children().Append(block.label);
    block.sub = MakeText(FormatRange(segment), 11,
                         ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
    block.sub.TextWrapping(TextWrapping::NoWrap);
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

void SettingsWindow::RefreshTimelineScopeLabel() {
  timelineStatusText_.Text(planScope_.SelectedIndex() != 1
                               ? L"拖动后会保存到当前常规模板。"
                               : L"拖动只调整今天的计划。");
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
  if (!changed) {
    RefreshTimelineScopeLabel();
    return;
  }
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
  const int edgeIndex = presentation.edge == Presentation::Edge::Top ? 0
                        : presentation.edge == Presentation::Edge::Bottom ? 1
                        : presentation.edge == Presentation::Edge::Left ? 2
                                                                        : 3;
  SelectSegment(edgeSegments_, edgeIndex);
  RefreshDisplayMonitors();
  dockControls_.Visibility(screenEdge ? Visibility::Collapsed : Visibility::Visible);
  visibleToggle_.IsOn(presentation.visible);
  hideInFullscreenToggle_.IsOn(presentation.hideInFullscreen);
  coverTaskbarToggle_.IsOn(presentation.coverTaskbar);
  edgeAutoHideToggle_.IsOn(presentation.autoHideAtEdge);
  flushToggle_.IsOn(presentation.flushWithEdge);
  dockBehavior_.Visibility(screenEdge ? Visibility::Collapsed : Visibility::Visible);
}

void SettingsWindow::SaveDisplaySettings(const Presentation& presentation) {
  auto& services = Services::Instance();
  if (services.UpdatePresentation(presentation)) {
    displayNotice_.IsOpen(false);
    return;
  }
  const bool wasSyncing = std::exchange(syncing_, true);
  monitorCombo_.IsDropDownOpen(false);
  RefreshDisplayControls();
  RefreshDisplayPreview(services.config().presentation);
  syncing_ = wasSyncing;
  displayNotice_.IsOpen(true);
}

void SettingsWindow::RefreshAppearanceControls() {
  const auto& presentation = Services::Instance().config().presentation;
  const auto style = EditableWidgetStyle();
  const bool wasSyncing = std::exchange(syncing_, true);
  const auto view = static_cast<int>(presentation.view);
  SelectSegment(widgetViewChoices_, view);
  const int variants[] = {static_cast<int>(style.barStyle), static_cast<int>(style.ringStyle),
                          static_cast<int>(style.screenEdgeStyle)};
  for (size_t i = 0; i < widgetShapeGroups_.size(); ++i) {
    widgetShapeGroups_[i].Visibility(static_cast<int>(i) == view
                                        ? Visibility::Visible : Visibility::Collapsed);
    SelectSegment(widgetStyleChoices_[i], variants[i]);
  }
  SelectSegment(ringReadoutChoices_, static_cast<int>(style.ringReadout));

  const bool allDay = presentation.scope == Presentation::Scope::AllDay;
  const bool perSegment = style.screenEdgeColors == WidgetStyle::ScreenEdgeColors::Schedule;
  SelectSegment(screenEdgeColorSegments_, static_cast<int>(style.screenEdgeColors));
  screenEdgeColorPanel_.Visibility(allDay ? Visibility::Visible : Visibility::Collapsed);
  screenEdgeGradientRow_.Visibility(allDay && perSegment ? Visibility::Visible
                                                        : Visibility::Collapsed);
  screenEdgeGradientToggle_.IsOn(style.screenEdgeGradient);
  screenEdgeHighlightToggle_.IsOn(style.screenEdgeHighlight);

  for (const auto& control : widgetDimensions_) {
    int value = style.*control.field;
    if (control.field == &WidgetStyle::barLengthDip) {
      value = style.EffectiveBarLength(presentation.IsVertical());
    } else if (control.field == &WidgetStyle::barThicknessDip) {
      value = style.EffectiveBarThickness(presentation.IsVertical());
    } else if (control.field == &WidgetStyle::barCornerDip && value < 0) {
      value = static_cast<int>(style.EffectiveBarCorner(presentation.IsVertical()));
    }
    if (control.slider.Value() != value) control.slider.Value(value);
    if (control.value.Value() != value) control.value.Value(value);
  }
  int selectedOutfit = -1;
  const auto& outfits = BuiltinOutfits();
  for (size_t i = 0; i < outfits.size(); ++i) {
    if (outfits[i].view == presentation.view && outfits[i].style == style) {
      selectedOutfit = static_cast<int>(i);
      break;
    }
  }
  SelectSegment(outfitChoices_, selectedOutfit);
  RefreshWidgetColors();
  syncing_ = wasSyncing;
}

void SettingsWindow::RefreshWidgetColors() {
  if (!widgetColorPicker_) return;
  const bool wasSyncing = std::exchange(syncing_, true);
  const auto style = EditableWidgetStyle();
  const auto palette = WidgetAppearance::ResolvePalette(style.colors);
  const auto bar = WidgetAppearance::SurfaceColor(palette, style.colors.barBackground);
  const auto ring = WidgetAppearance::SurfaceColor(palette, style.colors.ringBackground);
  const auto ink = WidgetAppearance::SurfaceInk(palette, ring, style.colors.inkColor);
  const Windows::UI::Color effective[] = {palette.accent, palette.rest, palette.longRest,
                                         palette.lunch, bar, ring,
                                         WidgetAppearance::EdgeTrackColor(palette), ink};
  int skinIndex = -1;
  const auto& skins = WidgetSkins();
  for (size_t i = 0; i < skins.size(); ++i) {
    if (style.colors == skins[i].colors) {
      skinIndex = static_cast<int>(i);
      break;
    }
  }
  SelectSegment(widgetSkinChoices_, skinIndex);
  if (palette.highContrast) {
    widgetSkinSummary_.Text(L"Windows 对比度主题已启用，组件使用系统颜色。");
  } else {
    widgetSkinSummary_.Text(skinIndex >= 0 ? hstring(L"当前使用 " + std::wstring(skins[skinIndex].name))
                                         : hstring(L"当前使用自定义配色"));
  }
  SelectSegment(widgetColorChoices_, static_cast<int>(widgetColorTarget_));
  for (size_t i = 0; i < std::size(kWidgetColorFields); ++i) {
    const auto tile = widgetColorChoices_.Children().GetAt(static_cast<uint32_t>(i))
                          .as<Primitives::ToggleButton>();
    const auto chip = tile.Content().as<StackPanel>().Children().GetAt(0).as<Border>();
    const auto brush = chip.Background().try_as<SolidColorBrush>();
    if (!brush || brush.Color() != effective[i]) chip.Background(SolidColorBrush(effective[i]));
  }
  const auto& field = kWidgetColorFields[widgetColorTarget_];
  const auto& hex = style.colors.*field.field;
  const auto pickerColor = hex.empty() ? effective[widgetColorTarget_]
                                      : WidgetAppearance::ParseHexColor(hex);
  if (widgetColorPicker_.Color() != pickerColor) widgetColorPicker_.Color(pickerColor);
  Automation::AutomationProperties::SetName(widgetColorPicker_, hstring(std::wstring(field.name) + L"取色器"));
  std::wstring label = std::wstring(field.name) + (hex.empty() ? L" · 默认 #" : L" · #") +
                       ToWide(WidgetAppearance::ColorToHex(pickerColor));
  if (field.field == &WidgetColors::inkColor && !hex.empty() && !palette.highContrast &&
      (ink != pickerColor || !WidgetAppearance::HasTextContrast(pickerColor, bar))) {
    label += L" · 对比度较低时自动换用可读文字";
  }
  widgetColorLabel_.Text(label);
  syncing_ = wasSyncing;
}

void SettingsWindow::RefreshSavedOutfits(const std::string& selected) {
  const auto& outfits = Services::Instance().config().widgetOutfits;
  const bool wasSyncing = std::exchange(syncing_, true);
  savedOutfitCombo_.Items().Clear();
  int index = outfits.empty() ? -1 : 0;
  for (size_t i = 0; i < outfits.size(); ++i) {
    savedOutfitCombo_.Items().Append(box_value(hstring(ToWide(outfits[i].name))));
    if (outfits[i].name == selected) index = static_cast<int>(i);
  }
  savedOutfitCombo_.IsEnabled(index >= 0);
  savedOutfitCombo_.SelectedIndex(index);
  syncing_ = wasSyncing;
  RefreshSavedOutfitPreview();
}

void SettingsWindow::RefreshSavedOutfitPreview() {
  const auto& outfits = Services::Instance().config().widgetOutfits;
  const int index = savedOutfitCombo_.SelectedIndex();
  const bool hasSelection = index >= 0 && static_cast<size_t>(index) < outfits.size();
  savedOutfitActions_.Visibility(hasSelection ? Visibility::Visible : Visibility::Collapsed);
  savedOutfitPreview_.Visibility(hasSelection ? Visibility::Visible : Visibility::Collapsed);
  if (!hasSelection) {
    savedOutfitPreview_.Child(nullptr);
    return;
  }
  StackPanel preview;
  preview.Spacing(4);
  preview.Orientation(Orientation::Horizontal);
  auto thumbnail = WidgetThumbnail(outfits[index].view, outfits[index].style);
  Add(preview, thumbnail);
  auto model = MakeText(WidgetViewName(outfits[index].view), 14);
  model.VerticalAlignment(VerticalAlignment::Center);
  Add(preview, model);
  savedOutfitPreview_.Child(preview);
}

void SettingsWindow::ShowAppearanceNotice(const std::wstring& message, bool error) {
  if (!appearanceNotice_) return;
  appearanceNotice_.Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Success);
  appearanceNotice_.Message(message);
  appearanceNotice_.IsOpen(true);
}

void SettingsWindow::EditSavedOutfitName(bool rename) {
  if (!rename && !CommitWidgetStyle()) return;
  const auto& config = Services::Instance().config();
  const int index = savedOutfitCombo_.SelectedIndex();
  if (rename && (index < 0 || static_cast<size_t>(index) >= config.widgetOutfits.size())) return;
  if (!rename && config.widgetOutfits.size() >= WidgetOutfit::kMaxSaved) {
    ShowAppearanceNotice(L"已保存 32 套外观。删除一个套装后可以继续保存。", true);
    return;
  }
  const std::string previous = rename ? config.widgetOutfits[index].name : std::string{};
  const auto currentView = config.presentation.view;
  const auto currentStyle = config.presentation.style;
  ContentDialog dialog;
  dialog.Title(box_value(rename ? L"重命名套装" : L"保存当前外观"));
  dialog.PrimaryButtonText(L"保存");
  dialog.CloseButtonText(L"取消");
  dialog.DefaultButton(ContentDialogButton::Primary);
  TextBox name;
  name.Header(box_value(L"套装名称"));
  name.MaxLength(static_cast<int>(WidgetOutfit::kMaxNameLength));
  name.Text(ToWide(previous));
  StackPanel content;
  content.Spacing(8);
  Add(content, name);
  auto error = MakeSubtitle(L"");
  Add(content, error);
  dialog.Content(content);
  dialog.PrimaryButtonClick([name, error, previous](auto const&, const ContentDialogButtonClickEventArgs& args) {
    const auto next = ToUtf8(TrimTemplateName(TextOf(name)));
    if (next.empty() || next.find_first_of("\r\n\t") != std::string::npos) {
      error.Text(L"请输入套装名称。");
      args.Cancel(true);
      return;
    }
    const auto& outfits = Services::Instance().config().widgetOutfits;
    const bool duplicate = next != previous && std::any_of(outfits.begin(), outfits.end(),
        [&next](const WidgetOutfit& outfit) { return outfit.name == next; });
    if (duplicate) {
      error.Text(L"已有同名套装，请换一个名称。");
      args.Cancel(true);
    }
  });
  dialog.XamlRoot(rootGrid_.XamlRoot());
  const auto operation = dialog.ShowAsync();
  operation.Completed([this, name, previous, currentView, currentStyle](
      const IAsyncOperation<ContentDialogResult>& async, AsyncStatus status) {
    if (status != AsyncStatus::Completed || async.GetResults() != ContentDialogResult::Primary) return;
    const auto next = ToUtf8(TrimTemplateName(TextOf(name)));
    auto& services = Services::Instance();
    auto outfits = services.config().widgetOutfits;
    if (previous.empty()) {
      outfits.push_back({next, currentView, currentStyle});
    } else {
      const auto found = std::find_if(outfits.begin(), outfits.end(), [&previous](const auto& outfit) {
        return outfit.name == previous;
      });
      if (found == outfits.end()) return;
      found->name = next;
    }
    if (!services.UpdateWidgetOutfits(std::move(outfits))) {
      ShowAppearanceNotice(L"套装保存失败。请检查本地配置目录后重试。", true);
      return;
    }
    RefreshSavedOutfits(next);
    ShowAppearanceNotice(previous.empty() ? L"套装已保存。" : L"套装名称已更新。");
  });
}

void SettingsWindow::RemoveSavedOutfit() {
  const auto& saved = Services::Instance().config().widgetOutfits;
  const int index = savedOutfitCombo_.SelectedIndex();
  if (index < 0 || static_cast<size_t>(index) >= saved.size()) return;
  const auto name = saved[index].name;
  ContentDialog dialog;
  dialog.Title(box_value(L"删除套装"));
  dialog.Content(box_value(hstring(L"删除「" + ToWide(name) + L"」？")));
  dialog.PrimaryButtonText(L"删除");
  dialog.CloseButtonText(L"取消");
  dialog.DefaultButton(ContentDialogButton::Close);
  dialog.XamlRoot(rootGrid_.XamlRoot());
  const auto operation = dialog.ShowAsync();
  operation.Completed([this, name](const IAsyncOperation<ContentDialogResult>& async, AsyncStatus status) {
    if (status != AsyncStatus::Completed || async.GetResults() != ContentDialogResult::Primary) return;
    auto& services = Services::Instance();
    auto outfits = services.config().widgetOutfits;
    outfits.erase(std::remove_if(outfits.begin(), outfits.end(),
        [&name](const WidgetOutfit& outfit) { return outfit.name == name; }), outfits.end());
    if (!services.UpdateWidgetOutfits(std::move(outfits))) {
      ShowAppearanceNotice(L"删除失败。请检查本地配置目录后重试。", true);
      return;
    }
    RefreshSavedOutfits();
    ShowAppearanceNotice(L"套装已删除。");
  });
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

WidgetStyle SettingsWindow::EditableWidgetStyle() const {
  return pendingWidgetStyle_.value_or(Services::Instance().config().presentation.style);
}

void SettingsWindow::EditWidgetStyle(WidgetStyle style, bool debounce) {
  pendingWidgetStyle_ = std::move(style);
  if (!debounce) {
    CommitWidgetStyle();
    return;
  }
  RefreshAppearanceControls();
  RefreshDisplayPreview(Services::Instance().config().presentation);
  if (!widgetStyleTimer_) {
    widgetStyleTimer_ = window_.DispatcherQueue().CreateTimer();
    widgetStyleTimer_.IsRepeating(false);
    widgetStyleTimer_.Interval(std::chrono::milliseconds(170));
    widgetStyleTimer_.Tick([this](auto const&, auto const&) { CommitWidgetStyle(); });
  }
  widgetStyleTimer_.Stop();
  widgetStyleTimer_.Start();
}

bool SettingsWindow::CommitWidgetStyle(std::optional<WidgetView> view) {
  if (widgetStyleTimer_) widgetStyleTimer_.Stop();
  auto draft = std::exchange(pendingWidgetStyle_, std::nullopt);
  if (!draft && !view) return true;
  auto& services = Services::Instance();
  auto presentation = services.config().presentation;
  if (draft) presentation.style = std::move(*draft);
  if (view) presentation.view = *view;
  const auto& saved = services.config().presentation;
  const bool changed = saved.view != presentation.view || !(saved.style == presentation.style);
  const bool persisted = !changed || services.UpdatePresentation(presentation);
  if (!persisted) {
    ShowAppearanceNotice(L"外观保存失败，已恢复上次保存的设置。", true);
  } else if (changed && appearanceNotice_ && appearanceNotice_.Severity() == InfoBarSeverity::Error) {
    appearanceNotice_.IsOpen(false);
  }
  if (appearanceScroll_ && activeSection_ == appearanceScroll_ && widgetViewChoices_) {
    RefreshAppearanceControls();
    RefreshDisplayPreview(services.config().presentation);
  }
  return persisted;
}

void SettingsWindow::ApplyOutfit(const WidgetOutfit& outfit) {
  pendingWidgetStyle_ = outfit.style;
  CommitWidgetStyle(outfit.view);
}

void SettingsWindow::CommitTemplateName() {
  if (templateNameTimer_) templateNameTimer_.Stop();
  const std::wstring name = std::exchange(pendingTemplateName_, std::wstring{});
  const std::string id = std::exchange(pendingTemplateId_, std::string{});
  if (id.empty() || name.empty()) return;
  Services::Instance().RenameTemplate(id, ToUtf8(name));
}

void SettingsWindow::StartPreviewRefresh() {
  if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_) || !ShowsDisplayPreview()) return;
  if (previewRefreshTimer_ && previewRefreshTimer_.IsRunning()) return;
  if (!previewRefreshTimer_) {
    previewRefreshTimer_ = window_.DispatcherQueue().CreateTimer();
    previewRefreshTimer_.Interval(std::chrono::seconds(1));
    previewRefreshTimer_.Tick([this](auto const&, auto const&) {
      if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_) || !ShowsDisplayPreview()) {
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

void SettingsWindow::ReleasePreviewResources() {
  StopPreviewRefresh();
  Motion::Stop(previewMotion_);
  if (previewDetail_) previewDetail_.Children().Clear();
  if (previewStageA_) previewStageA_.Children().Clear();
  if (previewStageB_) previewStageB_.Children().Clear();
  if (visualPreviewCanvas_) visualPreviewCanvas_.Children().Clear();
  previewDrawn_ = false;
  previewHovered_ = false;
  previewStateSignature_.clear();
}

void SettingsWindow::RefreshDisplayPreview(const Presentation& savedPresentation) {
  if (!previewChrome_) return;
  auto presentation = savedPresentation;
  if (pendingWidgetStyle_) presentation.style = *pendingWidgetStyle_;
  if (previewSample_) presentation.visible = true;
  const auto now = previewSample_ ? SamplePreviewTime() : NowClock();
  auto plan = Services::Instance().PlanFor(now);
  if (previewSample_) plan.segments = SamplePreviewPlan();
  const auto state = BuildWidgetState(plan.segments, now, presentation.scope);
  const auto palette = WidgetAppearance::ResolvePalette(presentation);
  const bool moving = state.snapshot.current.has_value() ||
      (presentation.view == WidgetView::ScreenEdge && state.edgeProgress > 0 && state.edgeProgress < 1);
  const int clockTick = !presentation.visible ? 0 :
      now.MinutesSinceMidnight() * 60 + (moving ? now.second : 0);
  const std::string stateSignature = now.DateKey() + ":" + std::to_string(clockTick) + ":" +
      std::to_string(plan.revision);
  const bool expanded = PreviewWidgetExpanded(presentation.autoHideAtEdge, previewHovered_,
                                               previewSample_);
  const Canvas stages[2] = {previewStageA_, previewStageB_};
  const auto& latest = previewAppearance_[previewStage_];
  const auto& last = latest.presentation;
  const bool sameAppearance = previewDrawn_ && last.view == presentation.view && last.scope == presentation.scope &&
      last.edge == presentation.edge && last.visible == presentation.visible &&
      last.offsetX == presentation.offsetX && last.offsetY == presentation.offsetY &&
      last.monitorId == presentation.monitorId &&
      last.autoHideAtEdge == presentation.autoHideAtEdge &&
      last.flushWithEdge == presentation.flushWithEdge &&
      last.coverTaskbar == presentation.coverTaskbar &&
      last.style == presentation.style && latest.expanded == expanded;
  const bool paletteChanged = !previewPalette_ || !(*previewPalette_ == palette);
  if (sameAppearance && !paletteChanged &&
      (previewStateSignature_ == stateSignature || previewMotion_)) return;
  if (!previewPalette_ || previewPalette_->highContrast != palette.highContrast ||
      (palette.highContrast && (previewPalette_->background != palette.background ||
                                previewPalette_->foreground != palette.foreground))) {
    DrawPreviewChrome(previewChrome_, palette);
  }
  previewStateSignature_ = stateSignature;
  previewPalette_ = palette;
  Motion::Stop(previewHighlightMotion_);
  Storyboard highlightPulse{nullptr};
  if (presentation.visible && presentation.view == WidgetView::ScreenEdge &&
      presentation.style.screenEdgeHighlight && !palette.highContrast &&
      !previewSample_ && Motion::Enabled()) {
    highlightPulse = Storyboard();
  }
  if (previewDetail_) {
    PreviewLayer detail(previewDetail_);
    if (!sameAppearance) previewDetail_.Children().Clear();
    DrawWidgetPreview(detail, previewDetailDial_, presentation, state, plan.segments,
                      now, palette, expanded, true, highlightPulse);
    detail.Trim();
  }

  // A new click starts from whichever layer is still most visible.
  int sourceIndex = previewStage_;
  if (previewMotion_ && stages[1 - sourceIndex].Opacity() > stages[sourceIndex].Opacity()) {
    sourceIndex = 1 - sourceIndex;
  }
  const int targetIndex = sameAppearance ? previewStage_ : (previewDrawn_ ? 1 - sourceIndex : 0);
  const Canvas source = previewDrawn_ && !sameAppearance ? stages[sourceIndex] : nullptr;
  const Canvas target = stages[targetIndex];
  const auto previous = previewAppearance_[sourceIndex];
  const auto from = PreviewAnchor(previous.presentation, previous.expanded);
  const auto to = PreviewAnchor(presentation, expanded);
  auto sourceTransform = source ? source.RenderTransform().try_as<CompositeTransform>() : nullptr;
  const double fromX = from.x + (sourceTransform ? sourceTransform.TranslateX() : 0);
  const double fromY = from.y + (sourceTransform ? sourceTransform.TranslateY() : 0);
  const double sourceOpacity = source ? source.Opacity() : 0;
  Motion::Stop(previewMotion_);
  if (!sameAppearance) target.Children().Clear();
  PreviewLayer preview(target);

  DrawWidgetPreview(preview, previewDials_[targetIndex], presentation, state, plan.segments,
                    now, palette, expanded, false, highlightPulse);
  preview.Trim();
  if (highlightPulse && highlightPulse.Children().Size() > 0) {
    previewHighlightMotion_ = highlightPulse;
    previewHighlightMotion_.Begin();
  }

  previewDrawn_ = true;
  previewAppearance_[targetIndex] = {presentation, expanded};
  previewStage_ = targetIndex;

  const auto& old = previous.presentation;
  const bool shapeChanged = old.view != presentation.view ||
      old.visible != presentation.visible || old.autoHideAtEdge != presentation.autoHideAtEdge ||
      previous.expanded != expanded;
  const bool docked = old.visible && presentation.visible &&
      old.view != WidgetView::ScreenEdge && presentation.view != WidgetView::ScreenEdge;
  const bool moved = docked && (old.edge != presentation.edge ||
      old.flushWithEdge != presentation.flushWithEdge ||
      old.coverTaskbar != presentation.coverTaskbar || fromX != to.x || fromY != to.y);
  const auto side = [](Presentation::Edge edge) {
    return edge == Presentation::Edge::Left || edge == Presentation::Edge::Right;
  };
  const bool rotates = side(old.edge) != side(presentation.edge);
  const bool crossfade = shapeChanged || (rotates && (presentation.view == WidgetView::Bar ||
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

  const int duration = moved ? 440 : 320;
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
  const auto& settings = Services::Instance().config().notifications;
  notificationsToggle_.IsOn(settings.enabled);
  muteToggle_.IsOn(settings.muted);
  focusToggle_.IsOn(settings.focusBoundaries);
  restToggle_.IsOn(settings.restBoundaries);
  lunchToggle_.IsOn(settings.lunchBoundaries);
  RefreshNotificationPermission();
}

void SettingsWindow::RefreshVisualAlertControls() {
  const auto& settings = Services::Instance().config().notifications;
  screenEdgeFlashToggle_.IsOn(settings.screenEdgeFlash);
  dropClockToggle_.IsOn(settings.dropClockAlert);
  dropClockOptionsCard_.Visibility(settings.dropClockAlert ? Visibility::Visible
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
  RefreshAlertColorEditor();
  RefreshVisualPreview();
}

void SettingsWindow::RefreshNotificationPermission() {
  const auto& settings = Services::Instance().config().notifications;
  const auto permission = QueryNotificationPermission();
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
  } else if (!permission.enabled) {
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
  permissionValueText_.Text(permission.label);
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
  if (!visualPreviewCanvas_ || activeSection_ != visualAlertsScroll_) return;
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

void SettingsWindow::ChangeLoginStartup(bool enable) {
  startupToggle_.IsEnabled(false);
  bool applied = false;
  bool registered = Services::Instance().config().launchAtLogin;
  try {
    if (LoginStartup::IsUnpackaged()) {
      applied = LoginStartup::SetRegistered(enable);
      registered = LoginStartup::Registered();
    } else {
      auto task = Windows::ApplicationModel::StartupTask::GetAsync(L"ClassTimeStartup").get();
      if (enable) task.RequestEnableAsync().get();
      else task.Disable();
      const auto state = task.State();
      using State = Windows::ApplicationModel::StartupTaskState;
      registered = state == State::Enabled || state == State::EnabledByPolicy;
      applied = registered == enable;
    }
  } catch (const winrt::hresult_error&) {
    applied = false;
  }
  const bool previous = std::exchange(syncing_, true);
  startupToggle_.IsOn(registered);
  syncing_ = previous;
  startupToggle_.IsEnabled(true);
  startupStatus_.Text(applied ? L"" : L"无法更改登录启动项，请检查 Windows 的启动应用设置。");
  startupStatus_.Visibility(applied ? Visibility::Collapsed : Visibility::Visible);
  Services::Instance().SetLaunchAtLogin(registered);
}

void SettingsWindow::RefreshGeneralControls() {
  startupToggle_.IsOn(LoginStartup::IsUnpackaged() ? LoginStartup::Registered()
                                                   : Services::Instance().config().launchAtLogin);
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
      RefreshCurrentPage();
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
  RefreshCurrentPage();
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
  RefreshCurrentPage();
}

// —— 与时间引擎同源的摘要（禁止 UI 另立计时器） ——

void SettingsWindow::RefreshTodayOverview() {
  const auto now = NowClock();
  const auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();

  if (activeSection_ == scheduleScroll_) {
    std::wstring summary = L"今天 " + std::to_wstring(now.month) + L"/" +
                           std::to_wstring(now.day) + L" · ";
    if (snapshot.current) {
      const auto& segment = *snapshot.current;
      summary += segment.kind == classtime::SegmentKind::Focus && segment.focusOrdinal > 0
                     ? L"第 " + std::to_wstring(segment.focusOrdinal) + L" 节"
                     : SegmentKindLabel(segment.kind);
      summary += L" " + FormatClock(segment.start) + L"–" + FormatClock(segment.end);
    } else if (snapshot.next) {
      summary += L"下一段 " + FormatClock(snapshot.next->start);
    } else {
      summary += snapshot.hasSchedule ? L"作息已结束" : L"没有安排";
    }
    scheduleSummary_.Text(hstring(summary));
    return;
  }

  // 编辑列表可切换到「今天的计划」，概览只显示当前和下一时段。
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

  if (snapshot.current && snapshot.next) {
    Add(todayOverview_, MakeSubtitle(L"下一时段 " + currentName(*snapshot.next) + L" " +
                                     FormatClock(snapshot.next->start)));
  }
}

void SettingsWindow::RefreshPreviewSummary() {
  if (previewSummaryText_ == nullptr) return;
  if (previewSample_) {
    previewSummaryText_.Text(L"示例课表 · 第 2 节 · 已过 56%");
    return;
  }
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

void SettingsWindow::ShowSection(SettingsPage page, const hstring& title) {
  CommitWidgetStyle();
  ScrollViewer selected{nullptr};
  switch (page) {
    case SettingsPage::Schedule:
      if (!scheduleScroll_) BuildSchedulePage();
      selected = scheduleScroll_;
      break;
    case SettingsPage::Timeline:
      if (!timelinePageScroll_) BuildTimelinePage();
      selected = timelinePageScroll_;
      break;
    case SettingsPage::Timetable:
      if (!timetablePageScroll_) BuildTimetablePage();
      selected = timetablePageScroll_;
      break;
    case SettingsPage::Templates:
      if (!templatesScroll_) BuildTemplatesPage();
      selected = templatesScroll_;
      break;
    case SettingsPage::Display:
      if (!displayScroll_) BuildDisplayPage();
      selected = displayScroll_;
      break;
    case SettingsPage::Appearance:
      if (!appearanceScroll_) BuildAppearancePage();
      selected = appearanceScroll_;
      break;
    case SettingsPage::Notifications:
      if (!notificationsScroll_) BuildNotificationPage();
      selected = notificationsScroll_;
      break;
    case SettingsPage::VisualAlerts:
      if (!visualAlertsScroll_) BuildVisualAlertsPage();
      selected = visualAlertsScroll_;
      break;
    case SettingsPage::General:
      if (!generalScroll_) BuildGeneralPage();
      selected = generalScroll_;
      break;
  }
  if (!selected || selected == activeSection_) return;
  if (timelineDragMode_ != TimelineDragMode::None) FinishTimelineDrag(false);
  if (activeSection_ == visualAlertsScroll_ && visualPreviewCanvas_) {
    visualPreviewCanvas_.Children().Clear();
  }

  pageTitle_.Text(title);
  RevealSection(selected);
  scheduleContext_.Visibility(ShowsScheduleContext() ? Visibility::Visible : Visibility::Collapsed);
  planScope_.Visibility(page == SettingsPage::Templates ? Visibility::Collapsed : Visibility::Visible);
  const bool showPreview = ShowsDisplayPreview();
  displayPreviewHost_.Visibility(showPreview ? Visibility::Visible : Visibility::Collapsed);
  appearanceTabsHost_.Visibility(page == SettingsPage::Appearance ? Visibility::Visible
                                                                  : Visibility::Collapsed);
  if (showPreview) {
    if (!displayPreview_) BuildDisplayPreview();
  } else {
    previewHovered_ = false;
    StopPreviewRefresh();
  }
  RefreshCurrentPage();
  if (showPreview) StartPreviewRefresh();
}

bool SettingsWindow::ShowsScheduleContext() const {
  return activeSection_ && (activeSection_ == scheduleScroll_ || activeSection_ == timelinePageScroll_ ||
                            activeSection_ == timetablePageScroll_ || activeSection_ == templatesScroll_);
}

bool SettingsWindow::ShowsDisplayPreview() const {
  return activeSection_ && (activeSection_ == displayScroll_ || activeSection_ == appearanceScroll_);
}

void SettingsWindow::RevealSection(const ScrollViewer& selected) {
  Motion::Stop(sectionMotion_);
  if (activeSection_) {
    activeSection_.Visibility(Visibility::Collapsed);
    activeSection_.Opacity(1);
    activeSection_.RenderTransform(nullptr);
  }
  activeSection_ = selected;
  activeSection_.Visibility(Visibility::Visible);
  if (!Motion::Enabled() || !IsWindowVisible(hwnd_)) return;
  auto section = activeSection_;
  sectionMotion_ = Motion::Slide(section, section, {0.72, 6}, {}, Motion::kPaneEnterMs);
  sectionMotion_.Completed([this, section](auto const& sender, auto const&) {
    if (sender != sectionMotion_) return;
    Motion::Stop(sectionMotion_);
    section.Opacity(1);
    section.RenderTransform(nullptr);
  });
  sectionMotion_.Begin();
}

void SettingsWindow::CommitWeekdays() {
  std::vector<int> weekdays;
  for (const auto& child : weekdayPanel_.Children()) {
    if (auto check = child.try_as<Primitives::ToggleButton>()) {
      const auto checked = check.IsChecked();
      if (checked != nullptr && checked.Value()) weekdays.push_back(unbox_value<int>(check.Tag()));
    }
  }
  SaveWeekdays(weekdays);
}

void SettingsWindow::SaveWeekdays(const std::vector<int>& weekdays) {
  std::string error;
  if (Services::Instance().SetActiveWeekdays(weekdays, &error)) return;
  ShowScheduleNotice(ToWide(error), true);
  RefreshCurrentPage();
}

std::vector<classtime::Segment> SettingsWindow::EditorBaseSegments(bool permanent) const {
  const auto& config = Services::Instance().config();
  const auto* schedule = classtime::SelectedTemplate(config);
  if (permanent) return schedule != nullptr ? schedule->segments : std::vector<classtime::Segment>{};
  return Services::Instance().effectiveSegments();
}

void SettingsWindow::OpenNewEditor() { OpenNewEditorAt(8 * 60); }

void SettingsWindow::OpenNewEditorAt(int minute) {
  if (!editorOverlay_) BuildEditor();
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
  if (!editorOverlay_) BuildEditor();
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
  if (!editorOverlay_) BuildEditor();
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
    editorMotion_ = Motion::Slide(editorOverlay_, editorWorkspace_, {0.60, 8}, {}, Motion::kPaneEnterMs);
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
  nav_.IsEnabled(true);
  planScope_.Focus(FocusState::Programmatic);
  if (!Motion::Enabled()) {
    FinishEditorClose();
    return;
  }
  const auto from = Motion::ReadPose(editorOverlay_, editorWorkspace_);
  Motion::Stop(editorMotion_);
  editorMotion_ = Motion::Slide(editorOverlay_, editorWorkspace_, from, {0, 8}, 220);
  editorMotion_.Completed([this](auto const& sender, auto const&) {
    if (sender == editorMotion_) FinishEditorClose();
  });
  editorMotion_.Begin();
}

void SettingsWindow::FinishEditorClose() {
  editorOverlay_.Visibility(Visibility::Collapsed);
  Motion::Stop(editorMotion_);
  editorError_.IsOpen(false);
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
  if (editorScope_.SelectedIndex() == 1) {
    const auto& services = Services::Instance();
    const auto* schedule = classtime::SelectedTemplate(services.config());
    const auto summary = schedule ? WeekdaysLabel(schedule->activeWeekdays) : std::wstring{};
    std::wstring hint = summary.empty() ? L"模板当前没有设置重复星期。"
                                        : L"模板每周 " + summary + L" 生效。";
    if (services.usingTodayOverride()) hint += L" 今天按临时计划执行。";
    editorScopeHint_.Text(hstring(hint));
  } else {
    editorScopeHint_.Text(L"改动仅用于今天。");
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
