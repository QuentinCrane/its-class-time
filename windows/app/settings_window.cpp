#include "pch.h"
#include "settings_window.h"

#include "notification_service.h"
#include "resource.h"
#include "services.h"
#include "ui_motion.h"

#include <initializer_list>
#include <limits>

#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
using namespace Windows::Foundation;

namespace ClassTimeApp {
namespace {

constexpr wchar_t kVersion[] = L"0.1.0";
using Presentation = classtime::PresentationSettings;
constexpr double kPreviewWidth = 420;
constexpr double kPreviewHeight = 172;

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

Windows::UI::Color SegmentColor(classtime::SegmentKind kind) {
  const auto brush = ResourceBrush(
      kind == classtime::SegmentKind::Focus
          ? L"ClassTimeFocusBrush"
          : kind == classtime::SegmentKind::Lunch ? L"ClassTimeLunchBrush" : L"ClassTimeRestBrush",
      Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237));
  if (const auto solid = brush.try_as<SolidColorBrush>()) return solid.Color();
  return Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237);
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
  auto block = MakeText(text, 28);
  if (auto style = ResourceStyle(L"TitleTextBlockStyle")) block.Style(style);
  return block;
}

TextBlock MakeSubtitle(const std::wstring& text) {
  auto block = MakeText(text, 12, ResourceBrush(L"TextFillColorSecondaryBrush", SecondaryColor()));
  return block;
}

Border MakeCard() {
  Border card;
  card.CornerRadius(CornerRadius{8, 8, 8, 8});
  card.Padding(Thickness{16, 16, 16, 16});
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
  grid.ColumnSpacing(3);
  int index = 0;
  for (const auto* label : labels) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(column);
    Primitives::ToggleButton button;
    button.Content(box_value(hstring(label)));
    button.HorizontalAlignment(HorizontalAlignment::Stretch);
    button.HorizontalContentAlignment(HorizontalAlignment::Center);
    button.MinHeight(34);
    button.Padding(Thickness{8, 4, 8, 4});
    button.CornerRadius(CornerRadius{7, 7, 7, 7});
    button.Click([select, index](auto const&, auto const&) { select(index); });
    Grid::SetColumn(button, index++);
    grid.Children().Append(button);
  }
  auto track = MakeCard();
  track.Padding(Thickness{3, 3, 3, 3});
  track.Child(grid);
  return track;
}

void SelectSegment(const Grid& grid, int selected) {
  int index = 0;
  for (const auto& child : grid.Children()) {
    child.as<Primitives::ToggleButton>().IsChecked(index++ == selected);
  }
}

// —— 显示预览：模拟屏幕是静态背景，只绘制一次 ——
constexpr double kPreviewScreenLeft = 42;
constexpr double kPreviewScreenTop = 5;
constexpr double kPreviewScreenWidth = kPreviewWidth - 2 * kPreviewScreenLeft;
constexpr double kPreviewScreenHeight = 143;
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

struct PreviewPoint {
  double x = 0;
  double y = 0;
};

// 控件贴近哪条边，缩放入场时就从那条边的中点出发。
PreviewPoint PreviewAnchor(const Presentation& presentation, bool expanded) {
  using Edge = Presentation::Edge;
  const auto edge = presentation.edge;
  const bool side = edge == Edge::Left || edge == Edge::Right;
  const bool grip = presentation.autoHideAtEdge && !expanded;
  const bool flush = presentation.flushWithEdge;
  if (!presentation.visible || presentation.view == Presentation::View::ScreenEdge) {
    return {kPreviewScreenCenterX, kPreviewScreenTop + kPreviewScreenHeight / 2};
  }
  const bool ring = presentation.view == Presentation::View::Ring && !grip;
  if (side) {
    const double inset = grip ? 7.5 : ring ? (flush ? 46 : 55) : (flush ? 12.5 : 18.5);
    const double x = edge == Edge::Left ? kPreviewScreenLeft + inset
                                       : kPreviewScreenLeft + kPreviewScreenWidth - inset;
    return {x, kPreviewScreenTop + kPreviewScreenHeight / 2};
  }
  const double inset = grip ? 4.5 : ring ? (flush ? 46 : 51) : (flush ? 12.5 : 19.5);
  return {kPreviewScreenCenterX, edge == Edge::Top ? kPreviewScreenTop + inset
                           : kPreviewScreenTop + kPreviewScreenHeight - inset};
}

}  // namespace

SettingsWindow::SettingsWindow() { Build(); }

SettingsWindow::~SettingsWindow() {
  CommitScreenEdgeThickness();
  Motion::Stop(editorMotion_);
  Motion::Stop(sectionMotion_);
  Motion::Stop(previewMotion_);
  Motion::Stop(displayPanelMotion_);
  if (window_ != nullptr) {
    try {
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
    CommitScreenEdgeThickness();
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
  nav_.OpenPaneLength(216);
  nav_.CompactPaneLength(52);
  nav_.CompactModeThresholdWidth(0);
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
    SymbolIcon icon;
    icon.Symbol(section.symbol);
    item.Icon(icon);
    nav_.MenuItems().Append(item);
  }

  auto content = Grid();
  content.Margin(Thickness{24, 16, 24, 24});
  nav_.Content(content);

  auto makePage = [&](ScrollViewer& scroll) {
    ScrollViewer viewer;
    viewer.Visibility(Visibility::Collapsed);
    viewer.HorizontalScrollMode(ScrollMode::Disabled);
    viewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    viewer.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    auto stack = StackPanel();
    stack.MaxWidth(980);
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
    // 页首：标题与说明在左，当前节次与今天日期徽标在右（同一时间引擎取值）。
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
    Add(titles, MakeSubtitle(L"高中与大学作息模板；可编辑课间和午休，时间按真实时钟前进。"));
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
    Grid::SetColumn(chips, 1);
    header.Children().Append(chips);
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
    templateRow.ColumnDefinitions().Append(labelCol);
    templateRow.ColumnDefinitions().Append(valueCol);
    templateRow.ColumnSpacing(12);
    auto label = MakeText(L"模板", 14);
    label.VerticalAlignment(VerticalAlignment::Center);
    templateRow.Children().Append(label);
    templateCombo_ = ComboBox();
    templateCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
    Grid::SetColumn(templateCombo_, 1);
    templateRow.Children().Append(templateCombo_);
    Add(stack, templateRow);

    templateStatus_ = MakeSubtitle(L"");
    Add(stack, templateStatus_);

    Add(stack, MakeText(L"生效星期", 14));
    weekdayPanel_ = VariableSizedWrapGrid();
    weekdayPanel_.Orientation(Orientation::Horizontal);
    weekdayPanel_.ItemWidth(52);
    weekdayPanel_.ItemHeight(34);
    Add(stack, weekdayPanel_);
    Add(stack, MakeSubtitle(L"所勾选的星期使用同一份作息模板。"));

    planScope_ = RadioButtons();
    planScope_.MaxColumns(2);
    planScope_.Items().Append(box_value(L"常规模板"));
    planScope_.Items().Append(box_value(L"今天的计划"));
    planScope_.SelectedIndex(0);
    Automation::AutomationProperties::SetName(planScope_, L"查看的作息计划");
    Add(stack, planScope_);

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
    auto add = Button();
    add.Content(box_value(hstring(L"新增时段")));
    add.Click([this](auto const&, auto const&) { OpenNewEditor(); });
    auto restore = Button();
    restore.Content(box_value(hstring(L"恢复默认...")));
    restore.Click([this](auto const&, auto const&) { ConfirmRestorePreset(); });
    clearTodayButton_ = Button();
    clearTodayButton_.Content(box_value(hstring(L"撤销今日调整")));
    clearTodayButton_.Click([this](auto const&, auto const&) {
      Services::Instance().ClearTodayOverride();
      ShowScheduleNotice(L"已撤销今天的临时调整，恢复按常规模板执行。");
      RefreshAll();
    });
    Add(buttons, add);
    Add(buttons, restore);
    Add(buttons, clearTodayButton_);
    BindActionRow(buttons, 350);
    Add(stack, buttons);
    Add(stack, timetableScroll_);
    Add(stack, MakeCardTitle(L"时段列表"));
    Add(stack, segmentList_);

    card.Child(stack);

    auto overviewCard = MakeCard();
    overviewCard.Padding(Thickness{14, 14, 14, 14});
    auto overviewStack = StackPanel();
    overviewStack.Spacing(10);
    Add(overviewStack, MakeCardTitle(L"今日概览"));
    todayOverview_ = StackPanel();
    todayOverview_.Spacing(6);
    Add(overviewStack, todayOverview_);
    overviewCard.Child(overviewStack);
    Add(scheduleStack, MakeAdaptiveColumns(card, overviewCard, 300, 902));
  }

  // —— 显示与位置 ——
  auto displayStack = makePage(displayScroll_);
  Add(displayStack, MakeSectionTitle(L"显示与位置"));
  Add(displayStack, MakeSubtitle(L"选择桌面进度表现形式与屏幕贴边位置。"));
  {
    // 左栏配置，右栏实时预览；窄窗口时预览收起、配置占满整行。
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(12);

    Add(stack, MakeText(L"显示形式", 14));
    Add(stack, MakeSegments(viewSegments_, {L"分节长条", L"圆环", L"屏幕边框"}, [this](int index) {
      SelectSegment(viewSegments_, index);
      constexpr Presentation::View views[] = {
          Presentation::View::Bar, Presentation::View::Ring, Presentation::View::ScreenEdge};
      auto presentation = Services::Instance().config().presentation;
      if (presentation.view == views[index]) return;
      presentation.view = views[index];
      Services::Instance().UpdatePresentation(presentation);
    }));
    Add(stack, MakeSubtitle(L"屏幕边框为待确认/实验项目；优先使用分节长条与圆环。"));

    Add(stack, MakeText(L"显示范围", 14));
    Add(stack, MakeSegments(scopeSegments_, {L"全天", L"当前时段"}, [this](int index) {
      SelectSegment(scopeSegments_, index);
      const auto scope = index == 0 ? Presentation::Scope::AllDay : Presentation::Scope::CurrentBlock;
      auto presentation = Services::Instance().config().presentation;
      if (presentation.scope == scope) return;
      presentation.scope = scope;
      Services::Instance().UpdatePresentation(presentation);
    }));

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
    Add(screenEdgeControls_, MakeSubtitle(L"鼠标可穿过边框。全天范围按首段到末段推进，颜色随当前时段变化。"));

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
    auto dockRow = StackPanel();
    BindActionRow(dockRow, 430);
    dockRow.Spacing(12);
    edgeAutoHideToggle_ = ToggleSwitch();
    edgeAutoHideToggle_.Header(box_value(L"贴边自动隐藏"));
    Add(dockRow, edgeAutoHideToggle_);
    flushToggle_ = ToggleSwitch();
    flushToggle_.Header(box_value(L"紧贴边缘"));
    Add(dockRow, flushToggle_);
    reanchorButton_ = Button();
    reanchorButton_.Content(box_value(hstring(L"重新贴边")));
    reanchorButton_.VerticalAlignment(VerticalAlignment::Bottom);
    reanchorButton_.Click([](auto const&, auto const&) {
      auto presentation = Services::Instance().config().presentation;
      presentation.visible = true;
      presentation.offsetX = 0;
      presentation.offsetY = 0;
      Services::Instance().UpdatePresentation(presentation);
    });
    Add(dockRow, reanchorButton_);
    Add(dockControls_, dockRow);
    Add(dockControls_, MakeSubtitle(L"紧贴边缘时与任务栏或屏幕边缘无缝衔接；关闭则保留悬浮间距。"));

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
    Add(stack, displayOptionsHost_);
    visibleToggle_ = ToggleSwitch();
    visibleToggle_.Header(box_value(hstring(L"显示悬浮窗")));
    Add(stack, visibleToggle_);

    card.Child(stack);

    // 右栏：与悬浮组件共用视觉逻辑的实时预览，附一行引擎摘要。
    auto previewCard = MakeCard();
    previewCard.Padding(Thickness{12, 12, 12, 12});
    previewCard.VerticalAlignment(VerticalAlignment::Top);
    auto previewContent = StackPanel();
    previewContent.Spacing(3);
    Add(previewContent, MakeCardTitle(L"实时效果预览"));
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
                     kPreviewScreenHeight});
    motionSurface.Clip(screenClip);
    motionSurface.Children().Append(previewStageA_);
    motionSurface.Children().Append(previewStageB_);
    displayPreview_.Children().Append(motionSurface);
    Viewbox previewScale;
    previewScale.MaxHeight(kPreviewHeight);
    previewScale.Child(displayPreview_);
    Add(previewContent, previewScale);
    previewSummaryText_ = MakeSubtitle(L"");
    previewSummaryText_.Margin(Thickness{0, 6, 0, 0});
    Add(previewContent, previewSummaryText_);
    Add(previewContent, MakeSubtitle(L"悬停悬浮组件时显示完整时段信息。"));
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
    Add(displayStack, MakeAdaptiveColumns(card, previewCard, 320, 760));
  }

  // —— 通知 ——
  auto notificationsStack = makePage(notificationsScroll_);
  Add(notificationsStack, MakeSectionTitle(L"通知"));
  Add(notificationsStack,
      MakeSubtitle(L"由常驻应用在时段切换时主动请求系统通知，不预排整天通知。"));
  {
    // 左栏提醒状态，右栏系统权限与测试；窄窗口时纵向堆叠。
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(12);

    notificationStateText_ = MakeText(L"", 14);
    Add(stack, notificationStateText_);

    notificationsToggle_ = ToggleSwitch();
    notificationsToggle_.Header(box_value(hstring(L"启用边界提醒")));
    Add(stack, notificationsToggle_);
    muteToggle_ = ToggleSwitch();
    muteToggle_.Header(box_value(hstring(L"临时暂停提醒")));
    Add(stack, muteToggle_);
    Add(stack, MakeSubtitle(L"暂停时进度照常运行，不更改课表；也可在托盘菜单快速切换。"));

    Add(stack, MakeCardTitle(L"在这些时段切换时提醒"));
    focusToggle_ = ToggleSwitch();
    focusToggle_.Header(box_value(hstring(L"上课开始 / 下课结束")));
    Add(stack, focusToggle_);
    restToggle_ = ToggleSwitch();
    restToggle_.Header(box_value(hstring(L"课间开始 / 结束")));
    Add(stack, restToggle_);
    lunchToggle_ = ToggleSwitch();
    lunchToggle_.Header(box_value(hstring(L"午休开始 / 结束")));
    Add(stack, lunchToggle_);

    Add(stack, MakeSubtitle(L"系统静音与勿扰模式由操作系统决定，应用不替代系统设置。"));

    card.Child(stack);

    auto checkCard = MakeCard();
    checkCard.Padding(Thickness{14, 14, 14, 14});
    checkCard.VerticalAlignment(VerticalAlignment::Top);
    auto checkStack = StackPanel();
    checkStack.Spacing(10);
    Add(checkStack, MakeCardTitle(L"立即检查"));
    Add(checkStack, MakeText(L"Windows 系统权限", 14));
    permissionValueText_ = MakeSubtitle(L"");
    Add(checkStack, permissionValueText_);

    auto buttons = StackPanel();
    buttons.Spacing(8);
    auto test = Button();
    test.Content(box_value(hstring(L"发送测试通知")));
    test.Click([this](auto const&, auto const&) {
      const bool ok = ShowTestSystemNotification();
      testResultText_.Text(hstring(ok ? L"已请求系统投递；勿扰或静音时不保证弹出。"
                                      : L"发送失败：请检查系统通知权限。"));
    });
    auto open = Button();
    open.Content(box_value(hstring(L"打开系统通知设置")));
    open.Click([](auto const&, auto const&) {
      try {
        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri(L"ms-settings:notifications"));
      } catch (...) {
      }
    });
    Add(buttons, test);
    Add(buttons, open);
    Add(checkStack, buttons);

    Add(checkStack, MakeText(L"最近一次发送结果", 14));
    testResultText_ = MakeSubtitle(L"尚未发送测试通知。");
    Add(checkStack, testResultText_);

    checkCard.Child(checkStack);
    Add(notificationsStack, MakeAdaptiveColumns(card, checkCard, 280, 700));
  }

  // —— 常规 ——
  auto generalStack = makePage(generalScroll_);
  Add(generalStack, MakeSectionTitle(L"常规"));
  Add(generalStack, MakeSubtitle(L"管理启动、退出以及本地配置；不改变作息时间本身。"));
  {
    // 左列启动与本地数据，右列关于；底部单独放退出按钮。
    auto leftStack = StackPanel();
    leftStack.Spacing(12);

    auto windowCard = MakeCard();
    auto windowStack = StackPanel();
    windowStack.Spacing(10);
    Add(windowStack, MakeCardTitle(L"启动与窗口"));
    startupToggle_ = ToggleSwitch();
    startupToggle_.Header(box_value(hstring(L"登录时自动启动")));
    Add(windowStack, startupToggle_);
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
    Add(aboutStack, MakeCardTitle(L"关于 ClassTime"));
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
    exit.Click([](auto const&, auto const&) { Application::Current().Exit(); });
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
  deleteButton_.HorizontalAlignment(HorizontalAlignment::Left);
  auto cancel = Button();
  cancel.Content(box_value(hstring(L"取消")));
  saveButton_ = Button();
  saveButton_.Content(box_value(hstring(L"保存调整")));
  if (auto style = ResourceStyle(L"AccentButtonStyle")) saveButton_.Style(style);
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
    std::string error;
    if (!Services::Instance().SelectTemplate(templateId, &error)) return;
    RefreshAll();
  });
  planScope_.SelectionChanged([this](auto const&, auto const&) {
    if (!syncing_) RefreshSegments();
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
  const wchar_t* names[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
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
  templateCombo_.Items().Clear();
  int selected = 0;
  int index = 0;
  for (const auto& schedule : config.templates) {
    ComboBoxItem item;
    item.Content(box_value(hstring(ToWide(schedule.name))));
    item.Tag(box_value(hstring(ToWide(schedule.id))));
    templateCombo_.Items().Append(item);
    if (schedule.id == config.selectedTemplateId) selected = index;
    ++index;
  }
  templateCombo_.SelectedIndex(selected);

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
  segmentList_.Children().Clear();
  auto sorted = classtime::SortedByStart(EditorBaseSegments(permanent));
  RefreshTimetable(sorted, permanent);
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
    addRow.Click([this](auto const&, auto const&) { OpenNewEditor(); });
    Add(emptyRow, addRow);
    Add(segmentList_, emptyRow);
  }
  int ordinal = 0;
  for (const auto& segment : sorted) {
    if (segment.kind == classtime::SegmentKind::Focus) ++ordinal;

    auto row = Border();
    row.CornerRadius(CornerRadius{6, 6, 6, 6});
    row.Padding(Thickness{10, 6, 10, 6});
    row.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));

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
    auto detail = MakeSubtitle(FormatRange(segment) + L" · " + FormatMinutes(segment.DurationMinutes()));
    text.Children().Append(name);
    text.Children().Append(detail);
    Grid::SetColumn(text, 1);
    grid.Children().Append(text);

    auto edit = Button();
    edit.Content(box_value(hstring(L"编辑")));
    edit.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(edit, hstring(L"编辑 " + ToWide(segment.label)));
    edit.Click([this, segment](auto const&, auto const&) { OpenEditor(segment); });
    Grid::SetColumn(edit, 2);
    grid.Children().Append(edit);

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
  ApplyDisplayPanelVisibility(screenEdge, true);
  screenEdgeSlider_.Value(pendingThickness_.value_or(presentation.screenEdgeThickness));
  visibleToggle_.IsOn(presentation.visible);
  edgeAutoHideToggle_.IsOn(presentation.autoHideAtEdge);
  flushToggle_.IsOn(presentation.flushWithEdge);
  RefreshDisplayPreview(presentation);
  RefreshPreviewSummary();
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

void SettingsWindow::RefreshDisplayPreview(const Presentation& presentation) {
  using View = Presentation::View;
  const int thickness = pendingThickness_.value_or(presentation.screenEdgeThickness);
  screenEdgeValue_.Text(std::to_wstring(thickness));
  if (previewChrome_ == nullptr || previewStageA_ == nullptr || previewStageB_ == nullptr) return;

  const bool expanded = previewHovered_ && presentation.autoHideAtEdge;
  const Canvas stages[2] = {previewStageA_, previewStageB_};
  const auto& latest = previewAppearance_[previewStage_];
  const auto& last = latest.presentation;
  if (previewDrawn_ && last.view == presentation.view && last.scope == presentation.scope &&
      last.edge == presentation.edge && last.visible == presentation.visible &&
      last.autoHideAtEdge == presentation.autoHideAtEdge &&
      last.flushWithEdge == presentation.flushWithEdge && latest.expanded == expanded &&
      latest.thickness == thickness) return;

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
  const auto accent = color(118, 183, 250);
  const auto track = color(82, 101, 123);
  const auto face = color(242, 247, 253);
  const auto ink = color(30, 50, 72);
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
    shape.RadiusX(radius);
    shape.RadiusY(radius);
    shape.Fill(brush);
    place(shape, x, y);
  };
  const auto text = [&](const wchar_t* label, double size, const Brush& brush, double y) {
    auto block = MakeText(label, size, brush);
    block.Width(kPreviewWidth);
    block.TextAlignment(TextAlignment::Center);
    place(block, 0, y);
  };
  const auto textAt = [&](const wchar_t* label, double size, const Brush& brush, double x,
                          double y, double width) {
    auto block = MakeText(label, size, brush);
    block.Width(width);
    block.TextAlignment(TextAlignment::Center);
    place(block, x, y);
  };
  constexpr double left = kPreviewScreenLeft;
  constexpr double top = kPreviewScreenTop;
  constexpr double width = kPreviewScreenWidth;
  constexpr double height = kPreviewScreenHeight;
  constexpr double center = kPreviewScreenCenterX;

  const auto draw = [&] {
  if (!presentation.visible) {
    text(L"悬浮窗已隐藏", 14, face, 66);
    return;
  }
  const bool allDay = presentation.scope == Presentation::Scope::AllDay;
  const bool dockTop = presentation.edge == Presentation::Edge::Top;
  const bool dockLeft = presentation.edge == Presentation::Edge::Left;
  const bool side = dockLeft || presentation.edge == Presentation::Edge::Right;
  const bool flush = presentation.flushWithEdge;
  if (presentation.view == View::ScreenEdge) {
    const double line = thickness * 0.58;
    rect(left, top, width, line, track);
    rect(left + width - line, top + line, line, height - line, track);
    rect(left, top + height - line, width - line, line, track);
    rect(left, top + line, line, height - 2 * line, track);
    const double perimeter = 2 * (width + height - 2 * line);
    double filled = perimeter * (allDay ? 0.62 : 0.29);
    rect(left, top, std::min(filled, width), line, accent);
    filled -= width;
    if (filled > 0) {
      rect(left + width - line, top + line, line, std::min(filled, height - line), accent);
      filled -= height - line;
    }
    if (filled > 0) {
      rect(left + width - line - filled, top + height - line, filled, line, accent);
    }
    return;
  }

  if (presentation.autoHideAtEdge && !previewHovered_) {
    if (side) {
      const double x = dockLeft ? left + 3 : left + width - 12;
      const double y = top + height / 2 - 26;
      rect(x, y, 9, 52, face, 4);
      rect(x + 3, y + 16, 3, 20, accent, 1.5);
    } else {
      const double y = dockTop ? top : top + height - 9;
      rect(center - 26, y, 52, 9, face, 4);
      rect(center - 10, y + 3, 20, 3, accent, 1.5);
    }
    text(L"悬停展开", 12, face, 67);
    return;
  }
  if (presentation.view == View::Bar) {
    if (side) {
      const double inset = flush ? 2 : 8;
      const double barX = dockLeft ? left + inset : left + width - inset - 21;
      const double barY = top + height / 2 - 55;
      rect(barX, barY, 21, 110, face, 7);
      const int segments = allDay ? 6 : 3;
      const double innerTop = barY + 8;
      const double innerHeight = 94;
      constexpr double gap = 4;
      const double segmentHeight = (innerHeight - gap * (segments - 1)) / segments;
      const int current = allDay ? 3 : 1;
      for (int i = 0; i < segments; ++i) {
        const double y = innerTop + i * (segmentHeight + gap);
        rect(barX + 6, y, 9, segmentHeight, color(194, 207, 223), 4.5);
        if (i <= current) {
          const double fill = i == current ? .46 : 1;
          rect(barX + 6, y, 9, segmentHeight * fill, accent, 4.5);
        }
      }
      return;
    }
    const double y = dockTop ? top + (flush ? 2 : 9) : top + height - (flush ? 2 : 9) - 21;
    const int segments = allDay ? 6 : 3;
    constexpr double segmentWidth = 34;
    constexpr double gap = 5;
    const double start = center - (segments * (segmentWidth + gap) - gap) / 2;
    rect(start - 8, y, 2 * (center - start) + 16, 21, face, 7);
    for (int i = 0; i < segments; ++i) {
      const double x = start + i * (segmentWidth + gap);
      rect(x, y + 8, segmentWidth, 5, color(194, 207, 223), 2.5);
      const int current = allDay ? 3 : 1;
      if (i <= current) {
        rect(x, y + 8, segmentWidth * (i == current ? .46 : 1), 5, accent, 2.5);
      }
    }
    return;
  }

  constexpr float radius = 36;
  const float y = static_cast<float>(side ? top + height / 2
                                          : (dockTop ? top + (flush ? 46 : 51)
                                                     : top + height - (flush ? 46 : 51)));
  const double cx = side ? (dockLeft ? left + (flush ? 46 : 55) : left + width - (flush ? 46 : 55))
                         : center;
  Shapes::Ellipse dial;
  dial.Width(88);
  dial.Height(88);
  dial.Fill(face);
  place(dial, cx - 44, y - 44);
  Shapes::Ellipse outline;
  outline.Width(2 * radius + 4);
  outline.Height(2 * radius + 4);
  outline.Stroke(color(199, 212, 227));
  outline.StrokeThickness(4);
  place(outline, cx - radius - 2, y - radius - 2);
  PathGeometry geometry;
  PathFigure figure;
  figure.StartPoint({static_cast<float>(cx), y - radius});
  ArcSegment arc;
  arc.Size({radius, radius});
  arc.SweepDirection(SweepDirection::Clockwise);
  arc.IsLargeArc(true);
  arc.Point({static_cast<float>(cx) - 22, y + 28.5f});
  figure.Segments().Append(arc);
  geometry.Figures().Append(figure);
  Shapes::Path progress;
  progress.Data(geometry);
  progress.Stroke(color(59, 132, 210));
  progress.StrokeThickness(4);
  progress.StrokeStartLineCap(PenLineCap::Round);
  progress.StrokeEndLineCap(PenLineCap::Round);
  children.Append(progress);
  textAt(L"13:24", 16, ink, cx - 44, y - 11, 88);
  if (allDay) textAt(L"今日 2 / 6 节", 8, ink, cx - 44, y + 10, 88);
  };
  draw();

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
      old.flushWithEdge != presentation.flushWithEdge || fromX != to.x || fromY != to.y);
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
  const auto& snapshot = Services::Instance().snapshot();
  if (!snapshot.current && !snapshot.next) {
    previewSummaryText_.Text(L"今天没有安排的时段。");
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

void SettingsWindow::OpenNewEditor() {
  newSegment_ = true;
  editingId_ = "seg-" + std::to_string(GetTickCount64());
  syncing_ = true;
  editorTitle_.Text(L"新增时段");
  editorName_.Text(L"");
  editorKind_.SelectedIndex(0);
  editorStart_.Text(L"08:00");
  editorEnd_.Text(L"08:45");
  editorSlider_.Maximum(180);
  SyncDurationControls(45);
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
