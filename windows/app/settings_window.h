// 设置窗口：纯 C++/WinUI 3 构建界面，不依赖 XAML 代码生成。
#pragma once

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
  void RefreshDisplayControls();
  void RefreshNotificationControls();
  void RefreshGeneralControls();
  void ShowSection(const std::wstring& tag);
  void CommitWeekdays();

  void OpenEditor(const std::string& segmentId);
  void OpenNewEditor();
  void CloseEditor();
  void UpdateEditorHints();
  std::vector<classtime::Segment> EditorBaseSegments(bool permanent) const;

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
  winrt::Microsoft::UI::Xaml::Controls::StackPanel WeekdayPanel() const { return weekdayPanel_; }
  winrt::Microsoft::UI::Xaml::Controls::StackPanel SegmentList() const { return segmentList_; }
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons ViewRadio() const { return viewRadio_; }
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons ScopeRadio() const { return scopeRadio_; }
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons EdgeRadio() const { return edgeRadio_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch VisibleToggle() const { return visibleToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch NotificationsToggle() const {
    return notificationsToggle_;
  }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch MuteToggle() const { return muteToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch FocusToggle() const { return focusToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch RestToggle() const { return restToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch LunchToggle() const { return lunchToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock PermissionText() const { return permissionText_; }
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch StartupToggle() const { return startupToggle_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock VersionText() const { return versionText_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ConfigPathText() const { return configPathText_; }
  winrt::Microsoft::UI::Xaml::Controls::Grid EditorOverlay() const { return editorOverlay_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBlock EditorTitle() const { return editorTitle_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorName() const { return editorName_; }
  winrt::Microsoft::UI::Xaml::Controls::ComboBox EditorKind() const { return editorKind_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorStart() const { return editorStart_; }
  winrt::Microsoft::UI::Xaml::Controls::TextBox EditorEnd() const { return editorEnd_; }
  winrt::Microsoft::UI::Xaml::Controls::NumberBox EditorMinutes() const { return editorMinutes_; }
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
  bool syncing_ = false;
  bool editorOpen_ = false;
  bool newSegment_ = false;
  bool allowClose_ = false;
  std::string editingId_;

  winrt::Microsoft::UI::Xaml::Controls::NavigationView nav_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scheduleScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer displayScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer notificationsScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer generalScroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ComboBox templateCombo_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock templateStatus_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel weekdayPanel_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel segmentList_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons viewRadio_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons scopeRadio_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::RadioButtons edgeRadio_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch visibleToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch notificationsToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch muteToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch focusToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch restToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch lunchToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock permissionText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch startupToggle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock versionText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock configPathText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid editorOverlay_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock editorTitle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorName_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ComboBox editorKind_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorStart_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox editorEnd_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::NumberBox editorMinutes_{nullptr};
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
