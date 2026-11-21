#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"
#include "notification_service.h"
#include "services.h"

#include <winrt/Microsoft.UI.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;

namespace winrt::ClassTime::implementation {

using ClassTimeApp::NowClock;
using ClassTimeApp::ToUtf8;
using ClassTimeApp::ToWide;

namespace {

constexpr wchar_t kVersion[] = L"0.1.0";

std::wstring TextOf(TextBox const& box) { return std::wstring(box.Text().c_str()); }

void SetText(TextBox const& box, const std::wstring& text) {
  if (TextOf(box) != text) box.Text(hstring(text));
}

std::optional<classtime::TimeOfDay> ParseBox(TextBox const& box) {
  return classtime::TimeOfDay::Parse(ToUtf8(TextOf(box)));
}

Brush ResourceBrush(const wchar_t* key, Windows::UI::Color fallback) {
  try {
    const auto resource = Application::Current().Resources().Lookup(box_value(key));
    if (auto brush = resource.try_as<Brush>()) return brush;
  } catch (...) {
  }
  return SolidColorBrush(fallback);
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

std::wstring SegmentName(const std::vector<classtime::Segment>& segments,
                         const std::string& id) {
  for (const auto& segment : segments) {
    if (segment.id == id) return ToWide(segment.label);
  }
  return ToWide(id);
}

}  // namespace

MainWindow::MainWindow() {
  const auto appWindow = AppWindow();
  hwnd_ = reinterpret_cast<HWND>(winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));

  double scale = 1.0;
  if (hwnd_ != nullptr) {
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi > 0) scale = static_cast<double>(dpi) / 96.0;
  }
  const int width = static_cast<int>(1020 * scale);
  const int height = static_cast<int>(780 * scale);
  appWindow.Resize({width, height});
  try {
    const auto area =
        Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
            appWindow.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary);
    const auto work = area.WorkArea();
    appWindow.MoveAndResize({work.X + (work.Width - width) / 2, work.Y + (work.Height - height) / 3,
                             width, height});
  } catch (...) {
  }

  appWindow.Closing([this](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
    if (allowClose_) return;
    // 关闭设置窗口只隐藏，后台继续判断边界。
    args.Cancel(true);
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
  });

  // 星期勾选
  const wchar_t* names[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
  for (int day = 1; day <= 7; ++day) {
    CheckBox box;
    box.Content(box_value(hstring(names[day - 1])));
    box.MinWidth(0);
    box.Margin(Thickness{0, 0, 0, 0});
    box.Tag(box_value(day));
    box.Checked([this](auto const&, auto const&) {
      if (syncing_) return;
      std::vector<int> weekdays;
      for (const auto& child : WeekdayPanel().Children()) {
        if (auto check = child.try_as<CheckBox>()) {
          const auto checked = check.IsChecked();
          if (checked != nullptr && checked.Value()) {
            weekdays.push_back(unbox_value<int>(check.Tag()));
          }
        }
      }
      ClassTimeApp::Services::Instance().SetActiveWeekdays(weekdays);
    });
    box.Unchecked([this](auto const&, auto const&) {
      if (syncing_) return;
      std::vector<int> weekdays;
      for (const auto& child : WeekdayPanel().Children()) {
        if (auto check = child.try_as<CheckBox>()) {
          const auto checked = check.IsChecked();
          if (checked != nullptr && checked.Value()) {
            weekdays.push_back(unbox_value<int>(check.Tag()));
          }
        }
      }
      ClassTimeApp::Services::Instance().SetActiveWeekdays(weekdays);
    });
    WeekdayPanel().Children().Append(box);
  }

  Nav().SelectedItem(Nav().MenuItems().GetAt(0));
  RefreshAll();
}

void MainWindow::ShowSettings() {
  if (hwnd_ == nullptr) return;
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  RefreshAll();
}

void MainWindow::ApplyState() {
  if (hwnd_ == nullptr) return;
  if (!editorOpen_) RefreshAll();
  RefreshNotificationControls();
}

void MainWindow::RefreshAll() {
  syncing_ = true;
  RefreshTemplates();
  RefreshWeekdays();
  RefreshSegments();
  RefreshDisplayControls();
  RefreshNotificationControls();
  RefreshGeneralControls();
  syncing_ = false;
}

void MainWindow::RefreshTemplates() {
  const auto& config = ClassTimeApp::Services::Instance().config();
  TemplateCombo().Items().Clear();
  int selected = 0;
  int index = 0;
  for (const auto& schedule : config.templates) {
    ComboBoxItem item;
    item.Content(box_value(hstring(ToWide(schedule.name))));
    item.Tag(box_value(hstring(ToWide(schedule.id))));
    TemplateCombo().Items().Append(item);
    if (schedule.id == config.selectedTemplateId) selected = index;
    ++index;
  }
  TemplateCombo().SelectedIndex(selected);

  const auto& services = ClassTimeApp::Services::Instance();
  const auto now = NowClock();
  if (services.usingTodayOverride()) {
    TemplateStatusText().Text(L"今天使用临时调整，常规模板未被改写。");
  } else if (const auto* schedule = classtime::SelectedTemplate(config)) {
    if (!schedule->IsActiveOn(now.Weekday())) {
      TemplateStatusText().Text(L"今天不是该模板的生效日，悬浮层会显示“今天没有安排”。");
    } else {
      TemplateStatusText().Text(L"今天按常规模板执行。修改后立即按新时段重新定位。");
    }
  } else {
    TemplateStatusText().Text(L"没有可用的模板。");
  }
}

void MainWindow::RefreshWeekdays() {
  const auto* schedule = classtime::SelectedTemplate(ClassTimeApp::Services::Instance().config());
  for (const auto& child : WeekdayPanel().Children()) {
    if (auto check = child.try_as<CheckBox>()) {
      const int day = unbox_value<int>(check.Tag());
      check.IsChecked(schedule != nullptr && schedule->IsActiveOn(day));
    }
  }
}

void MainWindow::RefreshSegments() {
  const auto& config = ClassTimeApp::Services::Instance().config();
  const auto* schedule = classtime::SelectedTemplate(config);
  SegmentList().Children().Clear();
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
    const auto kindBrush = ResourceBrush(segment.kind == classtime::SegmentKind::Focus
                                             ? L"ClassTimeFocusBrush"
                                             : (segment.kind == classtime::SegmentKind::Lunch
                                                    ? L"ClassTimeLunchBrush"
                                                    : L"ClassTimeRestBrush"),
                                         Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237));
    Windows::UI::Color kindColor = Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237);
    if (auto solid = kindBrush.try_as<SolidColorBrush>()) kindColor = solid.Color();
    auto softColor = kindColor;
    softColor.A = 38;
    badge.Background(SolidColorBrush(softColor));
    auto badgeText = TextBlock();
    badgeText.Text(segment.kind == classtime::SegmentKind::Focus
                       ? hstring(L"第 " + std::to_wstring(ordinal) + L" 节")
                       : hstring(ClassTimeApp::SegmentKindLabel(segment.kind)));
    badgeText.FontSize(11);
    badgeText.Foreground(SolidColorBrush(kindColor));
    badgeText.HorizontalAlignment(HorizontalAlignment::Center);
    badge.Child(badgeText);
    grid.Children().Append(badge);

    auto text = StackPanel();
    text.VerticalAlignment(VerticalAlignment::Center);
    text.Spacing(1);
    auto name = TextBlock();
    name.Text(hstring(ToWide(segment.label)));
    name.FontSize(14);
    auto detail = TextBlock();
    detail.Text(hstring(ClassTimeApp::FormatRange(segment) + L" · " +
                        ClassTimeApp::FormatMinutes(segment.DurationMinutes())));
    detail.FontSize(12);
    detail.Foreground(ResourceBrush(L"TextFillColorSecondaryBrush",
                                    Windows::UI::ColorHelper::FromArgb(255, 110, 112, 120)));
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
    SegmentList().Children().Append(row);
  }
}

void MainWindow::RefreshDisplayControls() {
  const auto& presentation = ClassTimeApp::Services::Instance().config().presentation;
  ViewRadio().SelectedIndex(
      presentation.view == classtime::PresentationSettings::View::Ring ? 1 : 0);
  ScopeRadio().SelectedIndex(
      presentation.scope == classtime::PresentationSettings::Scope::CurrentBlock ? 1 : 0);
  EdgeRadio().SelectedIndex(presentation.edge == classtime::PresentationSettings::Edge::Top ? 0 : 1);
  VisibleToggle().IsOn(presentation.visible);
}

void MainWindow::RefreshNotificationControls() {
  const bool previous = syncing_;
  syncing_ = true;
  const auto& settings = ClassTimeApp::Services::Instance().config().notifications;
  NotificationsToggle().IsOn(settings.enabled);
  MuteToggle().IsOn(settings.muted);
  FocusToggle().IsOn(settings.focusBoundaries);
  RestToggle().IsOn(settings.restBoundaries);
  LunchToggle().IsOn(settings.lunchBoundaries);
  const std::wstring label = ClassTimeApp::QueryNotificationPermissionLabel();
  PermissionText().Text(hstring(label +
                                (ClassTimeApp::QuerySystemNotificationsEnabled()
                                     ? L""
                                     : L"。若没有看到提醒，请到系统设置中允许通知。")));
  syncing_ = previous;
}

void MainWindow::RefreshGeneralControls() {
  StartupToggle().IsOn(ClassTimeApp::Services::Instance().config().launchAtLogin);
  VersionText().Text(hstring(L"版本 " + std::wstring(kVersion) + L" · Windows 预览"));
  const std::wstring path = ClassTimeApp::Services::Instance().dataDirectory().wstring();
  ConfigPathText().Text(hstring(L"配置文件：" + path));
}

void MainWindow::Nav_SelectionChanged(IInspectable const&, NavigationViewSelectionChangedEventArgs const& args) {
  if (args.SelectedItem() == nullptr) return;
  if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
    ShowSection(std::wstring(unbox_value<hstring>(item.Tag()).c_str()));
  }
}

void MainWindow::ShowSection(const std::wstring& tag) {
  ScheduleScroll().Visibility(tag == L"schedule" ? Visibility::Visible : Visibility::Collapsed);
  DisplayScroll().Visibility(tag == L"display" ? Visibility::Visible : Visibility::Collapsed);
  NotificationsScroll().Visibility(tag == L"notifications" ? Visibility::Visible
                                                           : Visibility::Collapsed);
  GeneralScroll().Visibility(tag == L"general" ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::TemplateCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
  if (syncing_) return;
  const int index = TemplateCombo().SelectedIndex();
  const auto& config = ClassTimeApp::Services::Instance().config();
  if (index < 0 || index >= static_cast<int>(config.templates.size())) return;
  const std::string templateId = config.templates[index].id;
  std::string error;
  if (!ClassTimeApp::Services::Instance().SelectTemplate(templateId, &error)) {
    return;
  }
  RefreshAll();
}

void MainWindow::AddSegment_Click(IInspectable const&, RoutedEventArgs const&) { OpenNewEditor(); }

void MainWindow::RestorePreset_Click(IInspectable const&, RoutedEventArgs const&) {
  std::string error;
  if (!ClassTimeApp::Services::Instance().RestorePreset(&error)) {
    EditorError().Message(hstring(ToWide(error)));
    EditorError().IsOpen(true);
  }
}

void MainWindow::ClearOverride_Click(IInspectable const&, RoutedEventArgs const&) {
  ClassTimeApp::Services::Instance().ClearTodayOverride();
  RefreshAll();
}

void MainWindow::ViewRadio_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
  if (syncing_) return;
  auto presentation = ClassTimeApp::Services::Instance().config().presentation;
  presentation.view = ViewRadio().SelectedIndex() == 1 ? classtime::PresentationSettings::View::Ring
                                                       : classtime::PresentationSettings::View::Bar;
  ClassTimeApp::Services::Instance().UpdatePresentation(presentation);
}

void MainWindow::ScopeRadio_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
  if (syncing_) return;
  auto presentation = ClassTimeApp::Services::Instance().config().presentation;
  presentation.scope = ScopeRadio().SelectedIndex() == 1
                           ? classtime::PresentationSettings::Scope::CurrentBlock
                           : classtime::PresentationSettings::Scope::AllDay;
  ClassTimeApp::Services::Instance().UpdatePresentation(presentation);
}

void MainWindow::EdgeRadio_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
  if (syncing_) return;
  auto presentation = ClassTimeApp::Services::Instance().config().presentation;
  presentation.edge = EdgeRadio().SelectedIndex() == 0 ? classtime::PresentationSettings::Edge::Top
                                                       : classtime::PresentationSettings::Edge::Bottom;
  ClassTimeApp::Services::Instance().UpdatePresentation(presentation);
}

void MainWindow::VisibleToggle_Toggled(IInspectable const&, RoutedEventArgs const&) {
  if (syncing_) return;
  auto presentation = ClassTimeApp::Services::Instance().config().presentation;
  presentation.visible = VisibleToggle().IsOn();
  ClassTimeApp::Services::Instance().UpdatePresentation(presentation);
}

void MainWindow::Reanchor_Click(IInspectable const&, RoutedEventArgs const&) {
  auto presentation = ClassTimeApp::Services::Instance().config().presentation;
  presentation.visible = true;
  presentation.offsetX = 0;
  ClassTimeApp::Services::Instance().UpdatePresentation(presentation);
}

void MainWindow::NotificationsToggle_Toggled(IInspectable const&, RoutedEventArgs const&) {
  if (syncing_) return;
  auto settings = ClassTimeApp::Services::Instance().config().notifications;
  settings.enabled = NotificationsToggle().IsOn();
  ClassTimeApp::Services::Instance().UpdateNotifications(settings);
}

void MainWindow::MuteToggle_Toggled(IInspectable const&, RoutedEventArgs const&) {
  if (syncing_) return;
  ClassTimeApp::Services::Instance().SetMuted(MuteToggle().IsOn());
}

void MainWindow::CategoryToggle_Toggled(IInspectable const&, RoutedEventArgs const&) {
  if (syncing_) return;
  auto settings = ClassTimeApp::Services::Instance().config().notifications;
  settings.focusBoundaries = FocusToggle().IsOn();
  settings.restBoundaries = RestToggle().IsOn();
  settings.lunchBoundaries = LunchToggle().IsOn();
  ClassTimeApp::Services::Instance().UpdateNotifications(settings);
}

void MainWindow::TestNotification_Click(IInspectable const&, RoutedEventArgs const&) {
  ClassTimeApp::ShowTestSystemNotification();
}

void MainWindow::OpenNotificationSettings_Click(IInspectable const&, RoutedEventArgs const&) {
  try {
    Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri(L"ms-settings:notifications"));
  } catch (...) {
  }
}

void MainWindow::StartupToggle_Toggled(IInspectable const&, RoutedEventArgs const&) {
  if (syncing_) return;
  const bool want = StartupToggle().IsOn();
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
  StartupToggle().IsOn(want && applied);
  syncing_ = false;
  ClassTimeApp::Services::Instance().SetLaunchAtLogin(want && applied);
}

void MainWindow::OpenConfigFolder_Click(IInspectable const&, RoutedEventArgs const&) {
  const auto path = ClassTimeApp::Services::Instance().dataDirectory().wstring();
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MainWindow::Exit_Click(IInspectable const&, RoutedEventArgs const&) {
  allowClose_ = true;
  const auto application = Application::Current();
  if (auto self = winrt::get_self<App>(application)) {
    self->ExitApplication();
  }
}

// —— 编辑浮层 ——

std::vector<classtime::Segment> MainWindow::EditorBaseSegments(bool permanent) const {
  const auto& services = ClassTimeApp::Services::Instance();
  const auto& config = services.config();
  const auto* schedule = classtime::SelectedTemplate(config);
  if (permanent) return schedule != nullptr ? schedule->segments : std::vector<classtime::Segment>{};
  auto effective = services.effectiveSegments();
  if (effective.empty() && schedule != nullptr) return schedule->segments;
  return effective;
}

void MainWindow::OpenNewEditor() {
  newSegment_ = true;
  editingId_ = "seg-" + std::to_string(GetTickCount64());
  editorOpen_ = true;

  syncing_ = true;
  EditorTitle().Text(hstring(L"新增时段"));
  EditorName().Text(hstring(L""));
  EditorKind().SelectedIndex(0);
  EditorStart().Text(hstring(L"08:00"));
  EditorEnd().Text(hstring(L"08:45"));
  EditorMinutes().Value(45);
  EditorSlider().Minimum(5);
  EditorSlider().Maximum(180);
  EditorSlider().Value(45);
  EditorScope().SelectedIndex(0);
  EditorStrategy().SelectedIndex(0);
  EditorError().IsOpen(false);
  DeleteButton().IsEnabled(false);
  EditorOverlay().Visibility(Visibility::Visible);
  syncing_ = false;
  UpdateEditorHints();
}

void MainWindow::OpenEditor(const std::string& segmentId) {
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
  EditorTitle().Text(hstring(ToWide(segment->label)));
  EditorName().Text(hstring(ToWide(segment->label)));
  EditorKind().SelectedIndex(KindToIndex(segment->kind));
  EditorStart().Text(hstring(ClassTimeApp::FormatClock(segment->start)));
  EditorEnd().Text(hstring(ClassTimeApp::FormatClock(segment->end)));
  EditorMinutes().Value(segment->DurationMinutes());
  const double maxMinutes = segment->kind == classtime::SegmentKind::Focus ? 180.0 : 240.0;
  EditorSlider().Minimum(5);
  EditorSlider().Maximum(maxMinutes);
  EditorSlider().Value(std::min<double>(maxMinutes, segment->DurationMinutes()));
  EditorScope().SelectedIndex(0);
  EditorStrategy().SelectedIndex(0);
  EditorError().IsOpen(false);
  DeleteButton().IsEnabled(true);
  EditorOverlay().Visibility(Visibility::Visible);
  syncing_ = false;
  UpdateEditorHints();
}

void MainWindow::CloseEditor() {
  editorOpen_ = false;
  EditorOverlay().Visibility(Visibility::Collapsed);
  EditorError().IsOpen(false);
}

void MainWindow::UpdateEditorHints() {
  const double minutes = EditorMinutes().Value();
  const double maximum = EditorSlider().Maximum();
  if (!std::isnan(minutes) && minutes > maximum) {
    EditorSliderHint().Text(hstring(L"当前 " + std::to_wstring(static_cast<int>(minutes)) +
                                    L" 分钟超出滑块范围 5–" +
                                    std::to_wstring(static_cast<int>(maximum)) +
                                    L" 分钟；滑块显示边界值，保存以数字框为准。"));
  } else {
    EditorSliderHint().Text(hstring(L"滑块步进 5 分钟；需要精确到 1 分钟时请用数字框。"));
  }

  const auto& services = ClassTimeApp::Services::Instance();
  if (EditorScope().SelectedIndex() == 1 && services.usingTodayOverride()) {
    EditorScopeHint().Text(hstring(L"今天仍按临时方案执行；永久修改只影响之后的常规模板。"));
  } else if (EditorScope().SelectedIndex() == 1) {
    EditorScopeHint().Text(hstring(L"修改会写入当前模板，并按生效星期重复。"));
  } else {
    EditorScopeHint().Text(hstring(L"只影响今天，不改动常规模板。"));
  }
}

void MainWindow::EditorStart_TextChanged(IInspectable const&, TextChangedEventArgs const&) {
  if (!editorOpen_ || syncing_) return;
  const auto start = ParseBox(EditorStart());
  if (!start.has_value()) return;
  const double minutes = EditorMinutes().Value();
  if (std::isnan(minutes) || minutes < 1) return;
  const auto end = classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() +
                                                          static_cast<int>(minutes));
  if (!end.has_value()) {
    EditorError().Message(hstring(L"结束时间会超过 23:59，请缩短时长。"));
    EditorError().IsOpen(true);
    return;
  }
  syncing_ = true;
  SetText(EditorEnd(), ClassTimeApp::FormatClock(*end));
  syncing_ = false;
  EditorError().IsOpen(false);
  UpdateEditorHints();
}

void MainWindow::EditorEnd_TextChanged(IInspectable const&, TextChangedEventArgs const&) {
  if (!editorOpen_ || syncing_) return;
  const auto start = ParseBox(EditorStart());
  const auto end = ParseBox(EditorEnd());
  if (!start.has_value() || !end.has_value()) return;
  const int minutes = end->TotalMinutes() - start->TotalMinutes();
  if (minutes <= 0) {
    EditorError().Message(hstring(L"结束时间必须晚于开始时间。"));
    EditorError().IsOpen(true);
    return;
  }
  syncing_ = true;
  EditorMinutes().Value(minutes);
  EditorSlider().Value(std::min<double>(EditorSlider().Maximum(), minutes));
  syncing_ = false;
  EditorError().IsOpen(false);
  UpdateEditorHints();
}

void MainWindow::EditorMinutes_ValueChanged(IInspectable const&, NumberBoxValueChangedEventArgs const&) {
  if (!editorOpen_ || syncing_) return;
  const double value = EditorMinutes().Value();
  if (std::isnan(value)) return;
  const int minutes = static_cast<int>(value);
  const auto start = ParseBox(EditorStart());
  if (!start.has_value()) return;
  const auto end = classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + minutes);
  if (!end.has_value()) {
    EditorError().Message(hstring(L"结束时间会超过 23:59，请缩短时长。"));
    EditorError().IsOpen(true);
    return;
  }
  syncing_ = true;
  SetText(EditorEnd(), ClassTimeApp::FormatClock(*end));
  EditorSlider().Value(std::min<double>(EditorSlider().Maximum(), std::max<double>(5, minutes)));
  syncing_ = false;
  EditorError().IsOpen(false);
  UpdateEditorHints();
}

void MainWindow::EditorSlider_ValueChanged(IInspectable const&, Primitives::RangeBaseValueChangedEventArgs const&) {
  if (!editorOpen_ || syncing_) return;
  const int value = static_cast<int>(EditorSlider().Value());
  syncing_ = true;
  EditorMinutes().Value(value);
  syncing_ = false;
  const auto start = ParseBox(EditorStart());
  if (!start.has_value()) return;
  const auto end = classtime::TimeOfDay::FromTotalMinutes(start->TotalMinutes() + value);
  if (!end.has_value()) return;
  syncing_ = true;
  SetText(EditorEnd(), ClassTimeApp::FormatClock(*end));
  syncing_ = false;
  UpdateEditorHints();
}

void MainWindow::EditorSave_Click(IInspectable const&, RoutedEventArgs const&) {
  if (!editorOpen_) return;
  const auto start = ParseBox(EditorStart());
  const auto end = ParseBox(EditorEnd());
  const std::wstring name = TextOf(EditorName());
  if (!start.has_value() || !end.has_value()) {
    EditorError().Message(hstring(L"时间格式应为 HH:mm，例如 10:05。"));
    EditorError().IsOpen(true);
    return;
  }
  if (name.empty()) {
    EditorError().Message(hstring(L"名称不能为空。"));
    EditorError().IsOpen(true);
    return;
  }
  if (end->TotalMinutes() <= start->TotalMinutes()) {
    EditorError().Message(hstring(L"结束时间必须晚于开始时间。"));
    EditorError().IsOpen(true);
    return;
  }

  const bool permanent = EditorScope().SelectedIndex() == 1;
  const auto strategy = EditorStrategy().SelectedIndex() == 1 ? classtime::ShiftStrategy::Keep
                                                             : classtime::ShiftStrategy::Shift;
  auto base = EditorBaseSegments(permanent);
  const auto kind = IndexToKind(EditorKind().SelectedIndex());

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
    EditorError().Message(hstring(message));
    EditorError().IsOpen(true);
    return;
  }

  std::string error;
  const auto scope = permanent ? ClassTimeApp::Services::SaveScope::Permanent
                               : ClassTimeApp::Services::SaveScope::TodayOnly;
  if (!ClassTimeApp::Services::Instance().ApplyScheduleEdit(outcome.segments, scope, &error)) {
    EditorError().Message(hstring(ToWide(error)));
    EditorError().IsOpen(true);
    return;
  }

  // 类型改变时提示用户：编辑的是完整列表，类型随本次保存生效。
  CloseEditor();
  RefreshAll();
}

void MainWindow::EditorCancel_Click(IInspectable const&, RoutedEventArgs const&) { CloseEditor(); }

void MainWindow::DeleteSegment_Click(IInspectable const&, RoutedEventArgs const&) {
  if (!editorOpen_ || newSegment_) return;
  const bool permanent = EditorScope().SelectedIndex() == 1;
  auto base = EditorBaseSegments(permanent);
  const auto outcome = classtime::RemoveSegment(base, editingId_);
  if (!outcome.ok()) {
    EditorError().Message(hstring(ToWide(outcome.message)));
    EditorError().IsOpen(true);
    return;
  }
  std::string error;
  const auto scope = permanent ? ClassTimeApp::Services::SaveScope::Permanent
                               : ClassTimeApp::Services::SaveScope::TodayOnly;
  if (!ClassTimeApp::Services::Instance().ApplyScheduleEdit(outcome.segments, scope, &error)) {
    EditorError().Message(hstring(ToWide(error)));
    EditorError().IsOpen(true);
    return;
  }
  CloseEditor();
  RefreshAll();
}

}  // namespace winrt::ClassTime::implementation
