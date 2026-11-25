#include "pch.h"
#include "desktop_app.h"

#include "services.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace ClassTimeApp {
namespace {

std::wstring BuildTooltip() {
  auto& services = Services::Instance();
  const auto& snapshot = services.snapshot();
  std::wstring tooltip = L"“上课”时间到！";
  if (snapshot.current.has_value()) {
    if (snapshot.current->kind == classtime::SegmentKind::Focus) {
      tooltip += L" · 第 " + std::to_wstring(snapshot.current->focusOrdinal) + L" 节 " +
                 FormatPercent(snapshot.progress);
    } else {
      tooltip += L" · " + SegmentKindLabel(snapshot.current->kind);
    }
  } else if (snapshot.next.has_value()) {
    tooltip += L" · 下一节 " + FormatClock(snapshot.next->start);
  } else if (snapshot.hasSchedule) {
    tooltip += L" · 今日已结束";
  } else {
    tooltip += L" · 今天没有安排";
  }
  if (services.config().notifications.muted) tooltip += L" · 提醒已暂停";
  return tooltip;
}

}  // namespace

DesktopApp::DesktopApp() = default;

void DesktopApp::OnLaunched(LaunchActivatedEventArgs const&) {
  dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

  // 单实例：第二次启动只激活已有实例。
  const auto instance =
      Microsoft::Windows::AppLifecycle::AppInstance::FindOrRegisterForKey(L"ClassTime");
  if (!instance.IsCurrent()) {
    try {
      instance
          .RedirectActivationToAsync(
              Microsoft::Windows::AppLifecycle::AppInstance::GetCurrent().GetActivatedEventArgs())
          .get();
    } catch (...) {
    }
    Application::Current().Exit();
    return;
  }

  Initialize();
}

void DesktopApp::Initialize() {
  // 统一挂载 WinUI 默认样式与强调色。
  try {
    auto resources = Resources();
    resources.MergedDictionaries().Append(XamlControlsResources());
    resources.Insert(box_value(L"ClassTimeFocusBrush"),
                     SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 79, 107, 237)));
    resources.Insert(box_value(L"ClassTimeRestBrush"),
                     SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 63, 167, 150)));
    resources.Insert(box_value(L"ClassTimeLunchBrush"),
                     SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 224, 164, 88)));
  } catch (...) {
  }

  auto& services = Services::Instance();
  if (!services.Initialize()) {
    const std::wstring message =
        L"无法安全读取、备份或写入配置，应用已停止启动。\n"
        L"原有配置或已创建的备份已保留。\n"
        L"请检查该文件及其文件夹的访问权限，然后重试。\n\n" +
        services.configPath().wstring();
    MessageBoxW(nullptr, message.c_str(), L"“上课”时间到！", MB_OK | MB_ICONERROR);
    Application::Current().Exit();
    return;
  }

  notifications_ = std::make_unique<NotificationService>();
  notifications_->Register([this] {
    auto dispatcher = dispatcher_;
    if (dispatcher != nullptr) dispatcher.TryEnqueue([this] { ShowSettings(); });
  });

  overlay_ = std::make_unique<OverlayWindow>();
  // 设置窗口懒加载：首次打开设置时才创建，避免启动期多个未激活窗口互相干扰。

  tray_ = std::make_unique<TrayIcon>();
  tray_->Create(GetModuleHandleW(nullptr), [this](UINT command) { HandleTrayCommand(command); });

  services.SetChangeCallback([this](Services::Change change) {
    if (monitor_) monitor_->RequestRefresh();
    auto dispatcher = dispatcher_;
    if (dispatcher == nullptr) return;
    dispatcher.TryEnqueue([this, change] { ApplyState(change == Services::Change::Presentation); });
  });

  monitor_ = std::make_unique<BoundaryMonitor>();
  monitor_->Start([this](const BoundaryMonitor::FiredBatch& batch) {
    auto dispatcher = dispatcher_;
    if (dispatcher == nullptr) return;
    dispatcher.TryEnqueue([this, batch] {
      if (exiting_) return;
      if (notifications_) notifications_->ShowBoundary(batch.events, batch.plan);
      ApplyState();
    });
  });

  ApplyState();

  wchar_t showSettings[8]{};
  if (GetEnvironmentVariableW(L"CLASSTIME_SHOW_SETTINGS", showSettings, 8) > 0) {
    ShowSettings();
  }
}

void DesktopApp::ApplyState(bool presentationOnly) {
  if (exiting_) return;
  auto& services = Services::Instance();
  const auto& config = services.config();

  if (tray_) {
    tray_->Update(BuildTooltip(), config.presentation, config.notifications.muted);
  }

  if (overlay_) {
    if (config.presentation.visible) {
      if (!overlay_->visible()) {
        overlay_->ShowOverlay();
      } else {
        overlay_->ApplyPresentation();
      }
    } else if (overlay_->visible()) {
      overlay_->HideOverlay();
    }
  }
  if (settings_) settings_->ApplyState(presentationOnly);
}

void DesktopApp::ShowSettings() {
  if (!settings_) settings_ = std::make_unique<SettingsWindow>([this] { ExitApplication(); });
  settings_->Show();
}

void DesktopApp::HandleTrayCommand(UINT command) {
  auto& services = Services::Instance();
  auto presentation = services.config().presentation;
  switch (command) {
    case kTrayToggleOverlay:
      presentation.visible = !presentation.visible;
      services.UpdatePresentation(presentation);
      if (overlay_) {
        if (presentation.visible) {
          overlay_->ShowOverlay();
        } else {
          overlay_->HideOverlay();
        }
      }
      break;
    case kTrayBarView:
      presentation.view = classtime::PresentationSettings::View::Bar;
      services.UpdatePresentation(presentation);
      break;
    case kTrayRingView:
      presentation.view = classtime::PresentationSettings::View::Ring;
      services.UpdatePresentation(presentation);
      break;
    case kTrayScreenEdgeView:
      presentation.view = classtime::PresentationSettings::View::ScreenEdge;
      services.UpdatePresentation(presentation);
      break;
    case kTrayCurrentScope:
      presentation.scope = classtime::PresentationSettings::Scope::CurrentBlock;
      services.UpdatePresentation(presentation);
      break;
    case kTrayAllDayScope:
      presentation.scope = classtime::PresentationSettings::Scope::AllDay;
      services.UpdatePresentation(presentation);
      break;
    case kTrayToggleMute:
      services.SetMuted(!services.config().notifications.muted);
      break;
    case kTrayOpenSettings:
      ShowSettings();
      break;
    case kTrayExit:
      ExitApplication();
      break;
    default:
      break;
  }
}

void DesktopApp::ExitApplication() {
  if (exiting_) return;
  exiting_ = true;
  Services::Instance().SetChangeCallback(nullptr);
  if (monitor_) {
    monitor_->Stop();
    monitor_.reset();
  }
  if (tray_) {
    tray_->Destroy();
    tray_.reset();
  }
  if (notifications_) {
    notifications_->Unregister();
    notifications_.reset();
  }
  Services::Instance().Save();
  settings_.reset();
  overlay_.reset();
  Application::Current().Exit();
}

}  // namespace ClassTimeApp
