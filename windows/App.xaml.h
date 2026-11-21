#pragma once

#include "App.xaml.g.h"
#include "app/boundary_monitor.h"
#include "app/notification_service.h"
#include "app/overlay_window.h"
#include "app/tray_icon.h"

namespace winrt::ClassTime::implementation {

struct App : AppT<App> {
  App();

  void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

  // 由设置页调用：完整退出（停止后台判断、移除托盘、注销通知）。
  void ExitApplication();

 private:
  void InitializeServices();
  void ApplyState();
  void ShowSettings();
  void HandleTrayCommand(UINT command);

  winrt::Microsoft::UI::Xaml::Window settingsWindow_{nullptr};
  std::unique_ptr<ClassTimeApp::OverlayWindow> overlay_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
  std::unique_ptr<ClassTimeApp::NotificationService> notifications_;
  std::unique_ptr<ClassTimeApp::BoundaryMonitor> monitor_;
  std::unique_ptr<ClassTimeApp::TrayIcon> tray_;
  bool exiting_ = false;
};

}  // namespace winrt::ClassTime::implementation
