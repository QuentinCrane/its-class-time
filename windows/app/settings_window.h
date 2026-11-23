// 设置窗口：纯 C++/WinUI 3 构建界面，不依赖 XAML 代码生成。
#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace ClassTimeApp {

class SettingsWindow {
 public:
  SettingsWindow();
  ~SettingsWindow();

  void Show();
  void ApplyState();

 private:
  void Build();
  void RefreshAll();
  void RefreshTemplates();
  void RefreshWeekdays();
  void RefreshSegments();
  void RefreshTimetable(const std::vector<classtime::Segment>& segments, bool permanent);
  void RefreshDisplayControls();
  void RefreshDisplayPreview(const classtime::PresentationSettings& presentation);
  void ApplyDisplayPanelVisibility(bool screenEdge, bool animate);
  void CommitScreenEdgeThickness();
  void RefreshNotificationControls();
  void RefreshGeneralControls();
  void ShowSection(const std::wstring& tag);
  void RevealSection();
  void CommitWeekdays();

  void OpenEditor(const classtime::Segment& segment);
  void OpenNewEditor();
  void ShowEditor();
  void CloseEditor();
  void FinishEditorClose();
  void UpdateEditorEnd();
  void SyncDurationControls(int minutes);
  void UpdateEditorHints();
  void RefreshEditorPreview();
  classtime::EditOutcome EvaluateDraft(const std::vector<classtime::Segment>& base,
                                      const classtime::Segment& draft) const;
  std::vector<classtime::Segment> EditorBaseSegments(bool permanent) const;

  // 非阻断状态提示与需要确认的操作。
  void ShowScheduleNotice(const std::wstring& text, bool error = false);
  void ConfirmRestorePreset();
  void PerformRestorePreset();
  void ConfirmEditorDelete();
  void PerformEditorDelete();

  // 与时间引擎同源的摘要数据（不另起计时器）。
  void RefreshTodayOverview();
  void RefreshPreviewSummary();

  // 与旧 XAML 访问器同名，便于沿用同一套处理逻辑。
  winrt::Microsoft::UI::Xaml::Controls::NavigationView Nav() const { return nav_; }
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer ScheduleScroll() const { return scheduleScroll_; }
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer DisplayScroll() const { return displayScroll_; }
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer NotificationsScroll() const {
    return notificationsScroll_;
  }
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer GeneralScroll() const { return generalScroll_; }
  winrt::Microsoft::UI::Xaml::Controls::ComboBox TemplateCombo() const { return templateCombo_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock TemplateStatusText() const {
    return templateStatus_;
  }
  winrt::Microsoft::UI::Xaml::Controls::StackPanel SegmentList() const { return segmentList_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch VisibleToggle() const { return visibleToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch NotificationsToggle() const {
    return notificationsToggle_;
  }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch MuteToggle() const { return muteToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch FocusToggle() const { return focusToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch RestToggle() const { return restToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch LunchToggle() const { return lunchToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch StartupToggle() const { return startupToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock VersionText() const { return versionText_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ConfigPathText() const { return configPathText_; }
  winrt::Microsoft::UI::Xaml::Controls::Grid EditorOverlay() const { return editorOverlay_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock EditorTitle() const { return editorTitle_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorName() const { return editorName_; }
  winrt::Microsoft::UI::Xaml::Controls::ComboBox EditorKind() const { return editorKind_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorStart() const { return editorStart_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorEnd() const { return editorEnd_; }
  winrt::Microsoft::UI::Xaml::Controls::Slider EditorSlider() const { return editorSlider_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock EditorSliderHint() const {
    return editorSliderHint_;
  }
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons EditorScope() const { return editorScope_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock EditorScopeHint() const { return editorScopeHint_; }
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons EditorStrategy() const { return editorStrategy_; }
  winrt::Microsoft::UI::Xaml::Controls::InfoBar EditorError() const { return editorError_; }
  winrt::Microsoft::UI::Xaml::Controls::Button DeleteButton() const { return deleteButton_; }
  winrt::Microsoft::UI::Xaml::Controls::Button SaveButton() const { return saveButton_; }

  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
  HWND hwnd_ = nullptr;
  HICON smallIcon_ = nullptr;
  HICON largeIcon_ = nullptr;
  bool syncing_ = false;
  bool editorOpen_ = false;
  bool newSegment_ = false;
  bool allowClose_ = false;
  std::string editingId_;

  winrt::Microsoft::UI::Xaml::Controls::NavigationView nav_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid rootGrid_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scheduleScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer displayScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer notificationsScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer generalScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ComboBox templateCombo_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock templateStatus_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons planScope_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel weekdayPanel_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer timetableScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel segmentList_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid viewSegments_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid scopeSegments_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid edgeSegments_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Canvas displayPreview_{nullptr};
  // 预览分三层：静态模拟屏幕 + 两层交替承载控件外观，切换形式时交叉淡入淡出。
  winrt::Microsoft::UI::Xaml::Controls::Canvas previewChrome_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Canvas previewStageA_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Canvas previewStageB_{nullptr};
  int previewStage_ = 0;
  bool previewDrawn_ = false;
  classtime::PresentationSettings::View previewView_ = classtime::PresentationSettings::View::Bar;
  classtime::PresentationSettings::Edge previewEdge_ = classtime::PresentationSettings::Edge::Bottom;
  bool previewHoveredShown_ = false;
  bool previewVisibleShown_ = true;
  uint64_t previewGeneration_ = 0;
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard previewMotion_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid displayOptionsHost_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::RectangleGeometry displayOptionsClip_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard displayPanelMotion_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard displayPanelFadeIn_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel dockControls_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel screenEdgeControls_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Slider screenEdgeSlider_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock screenEdgeValue_{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer thicknessTimer_{nullptr};
  std::optional<int> pendingThickness_;
  bool previewHovered_ = false;
  winrt::Microsoft::UI::Xaml::Controls::Button reanchorButton_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch visibleToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch edgeAutoHideToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch flushToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch notificationsToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch muteToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch focusToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch restToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch lunchToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch startupToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock versionText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock configPathText_{nullptr};
  // 作息计划：顶栏状态徽标、非阻断提示、今日概览与响应式两栏宿主。
  winrt::Microsoft::UI::Xaml::Controls::InfoBar scheduleNotice_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid scheduleBody_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock scheduleNowText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock scheduleDateText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel todayOverview_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button clearTodayButton_{nullptr};
  // 显示与位置：预览摘要文案与响应式两栏宿主。
  winrt::Microsoft::UI::Xaml::Controls::Grid displayBody_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock previewSummaryText_{nullptr};
  // 通知：有效提醒状态、系统权限与最近一次测试结果。
  winrt::Microsoft::UI::Xaml::Controls::Grid notificationBody_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock notificationStateText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock permissionValueText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock testResultText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid generalBody_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid editorOverlay_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid editorWorkspace_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border editorCard_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel editorPreview_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard editorMotion_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard sectionMotion_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer activeSection_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer requestedSection_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock editorTitle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorName_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ComboBox editorKind_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorStart_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorEnd_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorMinutes_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Slider editorSlider_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock editorSliderHint_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons editorScope_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock editorScopeHint_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons editorStrategy_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::InfoBar editorError_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button deleteButton_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button saveButton_{nullptr};
};

}  // namespace ClassTimeApp
