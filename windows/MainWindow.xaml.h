#pragma once

#include "MainWindow.g.h"

namespace winrt::ClassTime::implementation {

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

  void ApplyState();
  void ShowSettings();

  void Nav_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
  void TemplateCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void AddSegment_Click(winrt::Windows::Foundation::IInspectable const&,
                        Microsoft::UI::Xaml::RoutedEventArgs const&);
  void RestorePreset_Click(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);
  void ClearOverride_Click(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);
  void ViewRadio_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void ScopeRadio_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void EdgeRadio_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void VisibleToggle_Toggled(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);
  void Reanchor_Click(winrt::Windows::Foundation::IInspectable const&,
                      Microsoft::UI::Xaml::RoutedEventArgs const&);
  void NotificationsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
  void MuteToggle_Toggled(winrt::Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&);
  void CategoryToggle_Toggled(winrt::Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
  void TestNotification_Click(winrt::Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OpenNotificationSettings_Click(winrt::Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
  void StartupToggle_Toggled(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OpenConfigFolder_Click(winrt::Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
  void Exit_Click(winrt::Windows::Foundation::IInspectable const&,
                  Microsoft::UI::Xaml::RoutedEventArgs const&);
  void EditorStart_TextChanged(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
  void EditorEnd_TextChanged(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
  void EditorMinutes_ValueChanged(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
  void EditorSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
  void EditorSave_Click(winrt::Windows::Foundation::IInspectable const&,
                        Microsoft::UI::Xaml::RoutedEventArgs const&);
  void EditorCancel_Click(winrt::Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&);
  void DeleteSegment_Click(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);

 private:
  void RefreshAll();
  void RefreshTemplates();
  void RefreshWeekdays();
  void RefreshSegments();
  void RefreshDisplayControls();
  void RefreshNotificationControls();
  void RefreshGeneralControls();
  void ShowSection(const std::wstring& tag);

  void OpenEditor(const std::string& segmentId);
  void OpenNewEditor();
  void CloseEditor();
  void UpdateEditorSync(bool fromStart, bool fromEnd, bool fromMinutes);
  void UpdateEditorHints();

  std::vector<classtime::Segment> EditorBaseSegments(bool permanent) const;

  HWND hwnd_ = nullptr;
  bool syncing_ = false;
  bool editorOpen_ = false;
  bool newSegment_ = false;
  std::string editingId_;
  bool allowClose_ = false;
};

}  // namespace winrt::ClassTime::implementation

namespace winrt::ClassTime::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {
};
}  // namespace winrt::ClassTime::factory_implementation
