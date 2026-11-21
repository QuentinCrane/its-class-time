// 应用入口与常驻生命周期：纯 C++/WinUI 3，不依赖 XAML 代码生成。
// App 必须实现 IXamlMetadataProvider，否则 XamlControlsResources 找不到
// WinUI 控件主题资源，需要默认样式的控件（NavigationView 等）在渲染期
// 以 stowed exception 崩溃。参见 microsoft-ui-xaml issue #7606。
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

#include "app/boundary_monitor.h"
#include "app/notification_service.h"
#include "app/overlay_window.h"
#include "app/settings_window.h"
#include "app/tray_icon.h"

namespace ClassTimeApp {

struct DesktopApp
    : winrt::Microsoft::UI::Xaml::ApplicationT<
          DesktopApp, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
  DesktopApp();

  void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

  // 由设置页调用：完整退出。
  void ExitApplication();

  // —— IXamlMetadataProvider：转发到 WinUI 控件元数据 ——
  winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(
      winrt::Windows::UI::Xaml::Interop::TypeName const& type) {
    return metadataProvider_.GetXamlType(type);
  }
  winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& fullName) {
    return metadataProvider_.GetXamlType(fullName);
  }
  winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions() {
    return metadataProvider_.GetXmlnsDefinitions();
  }

 private:
  void Initialize();
  void ApplyState();
  void ShowSettings();
  void HandleTrayCommand(UINT command);

  winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadataProvider_;
  std::unique_ptr<OverlayWindow> overlay_;
  std::unique_ptr<SettingsWindow> settings_;
  std::unique_ptr<TrayIcon> tray_;
  std::unique_ptr<BoundaryMonitor> monitor_;
  std::unique_ptr<NotificationService> notifications_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
  bool exiting_ = false;
};

}  // namespace ClassTimeApp
