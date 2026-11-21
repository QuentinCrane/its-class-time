#include "pch.h"
#include "settings_window.h"

#include "notification_service.h"
#include "services.h"

#include <winrt/Microsoft.UI.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;

namespace ClassTimeApp {
namespace {

constexpr wchar_t kVersion[] = L"0.1.0";

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

std::wstring TextOf(TextBox const& box) { return std::wstring(box.Text().c_str()); }

void SetText(TextBox const& box, const std::wstring& text) {
  if (TextOf(box) != text) box.Text(hstring(text));
}

std::optional<classtime::TimeOfDay> ParseBox(TextBox const& box) {
  return classtime::TimeOfDay::Parse(ToUtf8(TextOf(box)));
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

}  // namespace

SettingsWindow::SettingsWindow() { Build(); }

SettingsWindow::~SettingsWindow() {
  if (window_ != nullptr) {
    try {
      window_.Close();
    } catch (...) {
    }
  }
}

void SettingsWindow::Build() {
  window_ = Window();
  window_.Title(L"“上课”时间到！");
  window_.ExtendsContentIntoTitleBar(true);

  const auto appWindow = window_.AppWindow();
  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  double scale = 1.0;
  if (hwnd_ != nullptr) {
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi > 0) scale = static_cast<double>(dpi) / 96.0;
  }
  const int width = static_cast<int>(1040 * scale);
  const int height = static_cast<int>(800 * scale);
  try {
    const auto area = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
        appWindow.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary);
    const auto work = area.WorkArea();
    appWindow.MoveAndResize({work.X + (work.Width - width) / 2, work.Y + (work.Height - height) / 3,
                             width, height});
  } catch (...) {
    appWindow.Resize({width, height});
  }

  appWindow.Closing([this](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
    if (allowClose_) return;
    args.Cancel(true);
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
  });

  auto root = Grid();
  window_.Content(root);

  nav_ = NavigationView();
  nav_.IsSettingsVisible(false);
  nav_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
  nav_.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
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
    auto stack = StackPanel();
    stack.MaxWidth(820);
    stack.Spacing(16);
    stack.HorizontalAlignment(HorizontalAlignment::Left);
    viewer.Content(stack);
    content.Children().Append(viewer);
    scroll = viewer;
    return stack;
  };

  // —— 作息计划 ——
  auto scheduleStack = makePage(scheduleScroll_);
  Add(scheduleStack, MakeSectionTitle(L"作息计划"));
  Add(scheduleStack, MakeSubtitle(L"以高中/大学示例为基础，按你的实际课表调整。修改只写入本地配置。"));
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
    templateCombo_.MinWidth(240);
    Grid::SetColumn(templateCombo_, 1);
    templateRow.Children().Append(templateCombo_);
    Add(stack, templateRow);

    templateStatus_ = MakeSubtitle(L"");
    Add(stack, templateStatus_);

    Add(stack, MakeSubtitle(L"生效星期"));
    weekdayPanel_ = StackPanel();
    weekdayPanel_.Orientation(Orientation::Horizontal);
    weekdayPanel_.Spacing(4);
    Add(stack, weekdayPanel_);

    segmentList_ = StackPanel();
    segmentList_.Spacing(4);
    Add(stack, segmentList_);

    auto buttons = StackPanel();
    buttons.Orientation(Orientation::Horizontal);
    buttons.Spacing(8);
    auto add = Button();
    add.Content(box_value(hstring(L"新增时段")));
    add.Click([this](auto const&, auto const&) { OpenNewEditor(); });
    auto restore = Button();
    restore.Content(box_value(hstring(L"恢复随包预设")));
    restore.Click([this](auto const&, auto const&) {
      std::string error;
      if (!Services::Instance().RestorePreset(&error)) {
        TemplateStatusText().Text(hstring(L"恢复失败：" + ToWide(error)));
        return;
      }
      RefreshAll();
    });
    auto clear = Button();
    clear.Content(box_value(hstring(L"撤销今日临时调整")));
    clear.Click([this](auto const&, auto const&) {
      Services::Instance().ClearTodayOverride();
      RefreshAll();
    });
    Add(buttons, add);
    Add(buttons, restore);
    Add(buttons, clear);
    Add(stack, buttons);

    card.Child(stack);
    Add(scheduleStack, card);
  }

  // —— 显示与位置 ——
  auto displayStack = makePage(displayScroll_);
  Add(displayStack, MakeSectionTitle(L"显示与位置"));
  Add(displayStack, MakeSubtitle(L"悬浮层按真实时钟推进；切换显示形式不会改变提醒状态。"));
  {
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(14);

    Add(stack, MakeText(L"显示形式", 14));
    viewRadio_ = RadioButtons();
    viewRadio_.Items().Append(box_value(hstring(L"分节长条")));
    viewRadio_.Items().Append(box_value(hstring(L"圆环")));
    Add(stack, viewRadio_);

    Add(stack, MakeText(L"显示范围", 14));
    scopeRadio_ = RadioButtons();
    scopeRadio_.Items().Append(box_value(hstring(L"全天")));
    scopeRadio_.Items().Append(box_value(hstring(L"当前时段")));
    Add(stack, scopeRadio_);

    Add(stack, MakeText(L"贴边位置", 14));
    edgeRadio_ = RadioButtons();
    edgeRadio_.Items().Append(box_value(hstring(L"贴在屏幕顶部")));
    edgeRadio_.Items().Append(box_value(hstring(L"贴在屏幕底部")));
    Add(stack, edgeRadio_);

    auto row = StackPanel();
    row.Orientation(Orientation::Horizontal);
    row.Spacing(12);
    visibleToggle_ = ToggleSwitch();
    visibleToggle_.Header(box_value(hstring(L"显示悬浮窗")));
    Add(row, visibleToggle_);
    auto reanchor = Button();
    reanchor.Content(box_value(hstring(L"重新贴边")));
    reanchor.VerticalAlignment(VerticalAlignment::Bottom);
    reanchor.Click([this](auto const&, auto const&) {
      auto presentation = Services::Instance().config().presentation;
      presentation.visible = true;
      presentation.offsetX = 0;
      Services::Instance().UpdatePresentation(presentation);
    });
    Add(row, reanchor);
    Add(stack, row);

    card.Child(stack);
    Add(displayStack, card);
  }

  // —— 通知 ——
  auto notificationsStack = makePage(notificationsScroll_);
  Add(notificationsStack, MakeSectionTitle(L"通知"));
  Add(notificationsStack,
      MakeSubtitle(L"到边界时由正在运行的时钟判断并调用系统通知，不预排整天通知。"));
  {
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(12);

    notificationsToggle_ = ToggleSwitch();
    notificationsToggle_.Header(box_value(hstring(L"启用边界提醒")));
    Add(stack, notificationsToggle_);
    muteToggle_ = ToggleSwitch();
    muteToggle_.Header(box_value(hstring(L"暂停提醒（时间进度继续）")));
    Add(stack, muteToggle_);

    focusToggle_ = ToggleSwitch();
    focusToggle_.Header(box_value(hstring(L"上课 / 下课")));
    Add(stack, focusToggle_);
    restToggle_ = ToggleSwitch();
    restToggle_.Header(box_value(hstring(L"课间 / 长休息")));
    Add(stack, restToggle_);
    lunchToggle_ = ToggleSwitch();
    lunchToggle_.Header(box_value(hstring(L"午休开始 / 结束")));
    Add(stack, lunchToggle_);

    permissionText_ = MakeSubtitle(L"");
    Add(stack, permissionText_);

    auto buttons = StackPanel();
    buttons.Orientation(Orientation::Horizontal);
    buttons.Spacing(8);
    auto test = Button();
    test.Content(box_value(hstring(L"发送测试通知")));
    test.Click([](auto const&, auto const&) { ShowTestSystemNotification(); });
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
    Add(stack, buttons);

    card.Child(stack);
    Add(notificationsStack, card);
  }

  // —— 常规 ——
  auto generalStack = makePage(generalScroll_);
  Add(generalStack, MakeSectionTitle(L"常规"));
  Add(generalStack, MakeSubtitle(L"关闭设置窗口不会退出；提醒由后台进程继续判断。"));
  {
    auto card = MakeCard();
    auto stack = StackPanel();
    stack.Spacing(12);

    startupToggle_ = ToggleSwitch();
    startupToggle_.Header(box_value(hstring(L"登录时启动（可选）")));
    Add(stack, startupToggle_);

    versionText_ = MakeText(L"", 13);
    Add(stack, versionText_);
    configPathText_ = MakeSubtitle(L"");
    configPathText_.IsTextSelectionEnabled(true);
    Add(stack, configPathText_);

    auto buttons = StackPanel();
    buttons.Orientation(Orientation::Horizontal);
    buttons.Spacing(8);
    auto openFolder = Button();
    openFolder.Content(box_value(hstring(L"打开配置文件夹")));
    openFolder.Click([](auto const&, auto const&) {
      const auto path = Services::Instance().dataDirectory().wstring();
      ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    auto exit = Button();
    exit.Content(box_value(hstring(L"退出应用")));
    exit.Click([](auto const&, auto const&) { Application::Current().Exit(); });
    Add(buttons, openFolder);
    Add(buttons, exit);
    Add(stack, buttons);

    card.Child(stack);
    Add(generalStack, card);
  }

  // —— 编辑浮层 ——
  editorOverlay_ = Grid();
  editorOverlay_.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(102, 0, 0, 0)));
  editorOverlay_.Visibility(Visibility::Collapsed);
  content.Children().Append(editorOverlay_);

  auto editorCard = Border();
  editorCard.Width(560);
  editorCard.MaxHeight(720);
  editorCard.HorizontalAlignment(HorizontalAlignment::Center);
  editorCard.VerticalAlignment(VerticalAlignment::Center);
  editorCard.CornerRadius(CornerRadius{10, 10, 10, 10});
  editorCard.Padding(Thickness{24, 24, 24, 24});
  editorCard.Background(ResourceBrush(L"SolidBackgroundFillColorBaseBrush",
                                     Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)));
  editorCard.BorderBrush(ResourceBrush(L"CardStrokeColorDefaultBrush",
                                      Windows::UI::ColorHelper::FromArgb(40, 0, 0, 0)));
  editorCard.BorderThickness(Thickness{1, 1, 1, 1});
  editorOverlay_.Children().Append(editorCard);

  auto editorScroll = ScrollViewer();
  editorCard.Child(editorScroll);
  auto editorStack = StackPanel();
  editorStack.Spacing(12);
  editorScroll.Content(editorStack);

  editorTitle_ = MakeText(L"编辑时段", 20);
  Add(editorStack, editorTitle_);

  editorName_ = TextBox();
  editorName_.Header(box_value(hstring(L"名称")));
  editorName_.PlaceholderText(L"例如：第 3 节 / 午休");
  Add(editorStack, editorName_);

  editorKind_ = ComboBox();
  editorKind_.Header(box_value(hstring(L"类型")));
  for (const wchar_t* kind : {L"上课", L"课间", L"长休息", L"午休"}) {
    editorKind_.Items().Append(box_value(hstring(kind)));
  }
  Add(editorStack, editorKind_);

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

  editorMinutes_ = NumberBox();
  editorMinutes_.Header(box_value(hstring(L"持续（分钟）")));
  editorMinutes_.Minimum(1);
  editorMinutes_.Maximum(1439);
  Add(editorStack, editorMinutes_);

  editorSlider_ = Slider();
  editorSlider_.Minimum(5);
  editorSlider_.Maximum(180);
  editorSlider_.StepFrequency(5);
  Add(editorStack, editorSlider_);
  editorSliderHint_ = MakeSubtitle(L"滑块步进 5 分钟；需要精确到 1 分钟时请用数字框。");
  Add(editorStack, editorSliderHint_);

  Add(editorStack, MakeText(L"作用范围", 14));
  editorScope_ = RadioButtons();
  editorScope_.Items().Append(box_value(hstring(L"仅今天")));
  editorScope_.Items().Append(box_value(hstring(L"永久修改模板")));
  Add(editorStack, editorScope_);
  editorScopeHint_ = MakeSubtitle(L"");
  Add(editorStack, editorScopeHint_);

  Add(editorStack, MakeText(L"后续时段", 14));
  editorStrategy_ = RadioButtons();
  editorStrategy_.Items().Append(box_value(hstring(L"顺延后续时段")));
  editorStrategy_.Items().Append(box_value(hstring(L"后续保持不变")));
  Add(editorStack, editorStrategy_);

  editorError_ = InfoBar();
  editorError_.Severity(InfoBarSeverity::Error);
  editorError_.IsOpen(false);
  editorError_.IsClosable(false);
  Add(editorStack, editorError_);

  auto editorButtons = StackPanel();
  editorButtons.Orientation(Orientation::Horizontal);
  editorButtons.Spacing(8);
  editorButtons.HorizontalAlignment(HorizontalAlignment::Right);
  deleteButton_ = Button();
  deleteButton_.Content(box_value(hstring(L"删除该时段")));
  auto cancel = Button();
  cancel.Content(box_value(hstring(L"取消")));
  saveButton_ = Button();
  saveButton_.Content(box_value(hstring(L"保存调整")));
  if (auto style = ResourceStyle(L"AccentButtonStyle")) saveButton_.Style(style);
  Add(editorButtons, deleteButton_);
  Add(editorButtons, cancel);
  Add(editorButtons, saveButton_);
  Add(editorStack, editorButtons);

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
  viewRadio_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.view = viewRadio_.SelectedIndex() == 1
                            ? classtime::PresentationSettings::View::Ring
                            : classtime::PresentationSettings::View::Bar;
    Services::Instance().UpdatePresentation(presentation);
  });
  scopeRadio_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.scope = scopeRadio_.SelectedIndex() == 1
                             ? classtime::PresentationSettings::Scope::CurrentBlock
                             : classtime::PresentationSettings::Scope::AllDay;
    Services::Instance().UpdatePresentation(presentation);
  });
  edgeRadio_.SelectionChanged([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.edge = edgeRadio_.SelectedIndex() == 0
                            ? classtime::PresentationSettings::Edge::Top
                            : classtime::PresentationSettings::Edge::Bottom;
    Services::Instance().UpdatePresentation(presentation);
  });
  visibleToggle_.Toggled([this](auto const&, auto const&) {
    if (syncing_) return;
    auto presentation = Services::Instance().config().presentation;
    presentation.visible = visibleToggle_.IsOn();
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

  editorStart_.TextChanged([this](auto const&, auto const&) {
    if (!editorOpen_ || syncing_) return;
    const auto start = ParseBox(editorStart_);
    if (!start.has_value()) return;
    const double minutes = editorMinutes_.Value();
    if (std::isnan(minutes) || minutes < 1) return;
    const auto end =
        classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + static_cast<int>(minutes));
    if (!end.has_value()) {
      editorError_.Message(hstring(L"结束时间会超过 23:59，请缩短时长。"));
      editorError_.IsOpen(true);
      return;
    }
    syncing_ = true;
    SetText(editorEnd_, FormatClock(*end));
    syncing_ = false;
    editorError_.IsOpen(false);
    UpdateEditorHints();
  });
  editorEnd_.TextChanged([this](auto const&, auto const&) {
    if (!editorOpen_ || syncing_) return;
    const auto start = ParseBox(editorStart_);
    const auto end = ParseBox(editorEnd_);
    if (!start.has_value() || !end.has_value()) return;
    const int minutes = end->TotalMinutes() - start->TotalMinutes();
    if (minutes <= 0) {
      editorError_.Message(hstring(L"结束时间必须晚于开始时间。"));
      editorError_.IsOpen(true);
      return;
    }
    syncing_ = true;
    editorMinutes_.Value(minutes);
    editorSlider_.Value(std::min<double>(editorSlider_.Maximum(), minutes));
    syncing_ = false;
    editorError_.IsOpen(false);
    UpdateEditorHints();
  });
  editorMinutes_.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const&) {
    if (!editorOpen_ || syncing_) return;
    const double value = editorMinutes_.Value();
    if (std::isnan(value)) return;
    const auto start = ParseBox(editorStart_);
    if (!start.has_value()) return;
    const auto end =
        classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + static_cast<int>(value));
    if (!end.has_value()) {
      editorError_.Message(hstring(L"结束时间会超过 23:59，请缩短时长。"));
      editorError_.IsOpen(true);
      return;
    }
    syncing_ = true;
    SetText(editorEnd_, FormatClock(*end));
    editorSlider_.Value(std::min<double>(editorSlider_.Maximum(), std::max<double>(5, value)));
    syncing_ = false;
    editorError_.IsOpen(false);
    UpdateEditorHints();
  });
  editorSlider_.ValueChanged([this](auto const&, Primitives::RangeBaseValueChangedEventArgs const&) {
    if (!editorOpen_ || syncing_) return;
    const int value = static_cast<int>(editorSlider_.Value());
    syncing_ = true;
    editorMinutes_.Value(value);
    syncing_ = false;
    const auto start = ParseBox(editorStart_);
    if (!start.has_value()) return;
    const auto end = classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + value);
    if (!end.has_value()) return;
    syncing_ = true;
    SetText(editorEnd_, FormatClock(*end));
    syncing_ = false;
    UpdateEditorHints();
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
    const bool permanent = editorScope_.SelectedIndex() == 1;
    const auto strategy = editorStrategy_.SelectedIndex() == 1 ? classtime::ShiftStrategy::Keep
                                                               : classtime::ShiftStrategy::Shift;
    auto base = EditorBaseSegments(permanent);
    const auto kind = IndexToKind(editorKind_.SelectedIndex());
    classtime::EditOutcome outcome;
    if (newSegment_) {
      classtime::Segment segment;
      segment.id = editingId_;
      segment.label = ToUtf8(name);
      segment.kind = kind;
      segment.start = *start;
      segment.end = *end;
      outcome = classtime::InsertSegment(base, segment);
    } else {
      classtime::EditRequest request;
      request.segmentId = editingId_;
      request.start = *start;
      request.end = *end;
      request.label = ToUtf8(name);
      request.kind = kind;
      request.strategy = strategy;
      outcome = classtime::ApplyEdit(base, request);
    }
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
    CloseEditor();
    RefreshAll();
  });
  deleteButton_.Click([this](auto const&, auto const&) {
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
    CloseEditor();
    RefreshAll();
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

void SettingsWindow::ApplyState() {
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
  const auto& config = Services::Instance().config();
  const auto* schedule = classtime::SelectedTemplate(config);
  segmentList_.Children().Clear();
  if (schedule == nullptr) return;

  auto sorted = classtime::SortedByStart(schedule->segments);
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
    const auto kindBrush = ResourceBrush(
        segment.kind == classtime::SegmentKind::Focus
            ? L"ClassTimeFocusBrush"
            : (segment.kind == classtime::SegmentKind::Lunch ? L"ClassTimeLunchBrush"
                                                            : L"ClassTimeRestBrush"),
        Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237));
    Windows::UI::Color kindColor = Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237);
    if (auto solid = kindBrush.try_as<SolidColorBrush>()) kindColor = solid.Color();
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
    const std::string id = segment.id;
    edit.Click([this, id](auto const&, auto const&) { OpenEditor(id); });
    Grid::SetColumn(edit, 2);
    grid.Children().Append(edit);

    row.Child(grid);
    segmentList_.Children().Append(row);
  }
}

void SettingsWindow::RefreshDisplayControls() {
  const auto& presentation = Services::Instance().config().presentation;
  viewRadio_.SelectedIndex(presentation.view == classtime::PresentationSettings::View::Ring ? 1 : 0);
  scopeRadio_.SelectedIndex(
      presentation.scope == classtime::PresentationSettings::Scope::CurrentBlock ? 1 : 0);
  edgeRadio_.SelectedIndex(presentation.edge == classtime::PresentationSettings::Edge::Top ? 0 : 1);
  visibleToggle_.IsOn(presentation.visible);
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
  const std::wstring label = QueryNotificationPermissionLabel();
  permissionText_.Text(hstring(label + (QuerySystemNotificationsEnabled()
                                            ? L""
                                            : L"。若没有看到提醒，请到系统设置中允许通知。")));
  syncing_ = previous;
}

void SettingsWindow::RefreshGeneralControls() {
  startupToggle_.IsOn(Services::Instance().config().launchAtLogin);
  versionText_.Text(hstring(L"版本 " + std::wstring(kVersion) + L" · Windows"));
  const std::wstring path = Services::Instance().dataDirectory().wstring();
  configPathText_.Text(hstring(L"配置文件：" + path));
}

void SettingsWindow::ShowSection(const std::wstring& tag) {
  scheduleScroll_.Visibility(tag == L"schedule" ? Visibility::Visible : Visibility::Collapsed);
  displayScroll_.Visibility(tag == L"display" ? Visibility::Visible : Visibility::Collapsed);
  notificationsScroll_.Visibility(tag == L"notifications" ? Visibility::Visible
                                                           : Visibility::Collapsed);
  generalScroll_.Visibility(tag == L"general" ? Visibility::Visible : Visibility::Collapsed);
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
  auto effective = Services::Instance().effectiveSegments();
  if (effective.empty() && schedule != nullptr) return schedule->segments;
  return effective;
}

void SettingsWindow::OpenNewEditor() {
  newSegment_ = true;
  editingId_ = "seg-" + std::to_string(GetTickCount64());
  editorOpen_ = true;
  syncing_ = true;
  editorTitle_.Text(hstring(L"新增时段"));
  editorName_.Text(hstring(L""));
  editorKind_.SelectedIndex(0);
  editorStart_.Text(hstring(L"08:00"));
  editorEnd_.Text(hstring(L"08:45"));
  editorMinutes_.Value(45);
  editorSlider_.Minimum(5);
  editorSlider_.Maximum(180);
  editorSlider_.Value(45);
  editorScope_.SelectedIndex(0);
  editorStrategy_.SelectedIndex(0);
  editorError_.IsOpen(false);
  deleteButton_.IsEnabled(false);
  editorOverlay_.Visibility(Visibility::Visible);
  syncing_ = false;
  UpdateEditorHints();
}

void SettingsWindow::OpenEditor(const std::string& segmentId) {
  auto base = EditorBaseSegments(false);
  const auto* segment = classtime::FindSegmentById(base, segmentId);
  if (segment == nullptr) {
    auto permanentBase = EditorBaseSegments(true);
    segment = classtime::FindSegmentById(permanentBase, segmentId);
    base = permanentBase;
  }
  if (segment == nullptr) return;

  newSegment_ = false;
  editingId_ = segmentId;
  editorOpen_ = true;
  syncing_ = true;
  editorTitle_.Text(hstring(ToWide(segment->label)));
  editorName_.Text(hstring(ToWide(segment->label)));
  editorKind_.SelectedIndex(KindToIndex(segment->kind));
  editorStart_.Text(hstring(FormatClock(segment->start)));
  editorEnd_.Text(hstring(FormatClock(segment->end)));
  editorMinutes_.Value(segment->DurationMinutes());
  const double maxMinutes = segment->kind == classtime::SegmentKind::Focus ? 180.0 : 240.0;
  editorSlider_.Minimum(5);
  editorSlider_.Maximum(maxMinutes);
  editorSlider_.Value(std::min<double>(maxMinutes, segment->DurationMinutes()));
  editorScope_.SelectedIndex(0);
  editorStrategy_.SelectedIndex(0);
  editorError_.IsOpen(false);
  deleteButton_.IsEnabled(true);
  editorOverlay_.Visibility(Visibility::Visible);
  syncing_ = false;
  UpdateEditorHints();
}

void SettingsWindow::CloseEditor() {
  editorOpen_ = false;
  editorOverlay_.Visibility(Visibility::Collapsed);
  editorError_.IsOpen(false);
}

void SettingsWindow::UpdateEditorHints() {
  const double minutes = editorMinutes_.Value();
  const double maximum = editorSlider_.Maximum();
  if (!std::isnan(minutes) && minutes > maximum) {
    editorSliderHint_.Text(hstring(L"当前 " + std::to_wstring(static_cast<int>(minutes)) +
                                   L" 分钟超出滑块范围 5–" +
                                   std::to_wstring(static_cast<int>(maximum)) +
                                   L" 分钟；保存以数字框为准。"));
  } else {
    editorSliderHint_.Text(hstring(L"滑块步进 5 分钟；需要精确到 1 分钟时请用数字框。"));
  }
  if (editorScope_.SelectedIndex() == 1 && Services::Instance().usingTodayOverride()) {
    editorScopeHint_.Text(hstring(L"今天仍按临时方案执行；永久修改只影响之后的常规模板。"));
  } else if (editorScope_.SelectedIndex() == 1) {
    editorScopeHint_.Text(hstring(L"修改会写入当前模板，并按生效星期重复。"));
  } else {
    editorScopeHint_.Text(hstring(L"只影响今天，不改动常规模板。"));
  }
}

}  // namespace ClassTimeApp
