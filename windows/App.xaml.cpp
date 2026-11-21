#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "app/boundary_monitor.h"
#include "app/notification_service.h"
#include "app/services.h"
#include "app/tray_icon.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClassTime::implementation {

App::App() {
  UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& args) {
    // 未处理异常时至少留下可诊断信息，不静默退出。
    const auto message = args.Message();
    OutputDebugStringW((L"ClassTime unhandled: " + std::wstring(message)).c_str());
  });
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
  dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

  // 单实例：第二次启动只把已有实例调到前台，不再启动第二套计时与通知。
  const auto instance = Microsoft::Windows::AppLifecycle::AppInstance::FindOrRegisterForKey(L"ClassTime");
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

  InitializeServices();
}

void App::InitializeServices() {
  auto& services = ClassTimeApp::Services::Instance();
  services.Initialize();

  notifications_ = std::make_unique<ClassTimeApp::NotificationService>();
  notifications_->Register([this] {
    auto dispatcher = dispatcher_;
    if (dispatcher != nullptr) {
      dispatcher.TryEnqueue([this] { ShowSettings(); });
    }
  });

  overlay_ = std::make_unique<ClassTimeApp::OverlayWindow>();
  settingsWindow_ = make<MainWindow>();

  tray_ = std::make_unique<ClassTimeApp::TrayIcon>();
  tray_->Create(GetModuleHandleW(nullptr), [this](UINT command) { HandleTrayCommand(command); });

  services.SetChangeCallback([this] {
    auto dispatcher = dispatcher_;
    if (dispatcher == nullptr) return;
    dispatcher.TryEnqueue([this] { ApplyState(); });
  });

  monitor_ = std::make_unique<ClassTimeApp::BoundaryMonitor>();
  monitor_->Start([this](const ClassTimeApp::BoundaryMonitor::FiredBatch& batch) {
    auto dispatcher = dispatcher_;
    if (dispatcher == nullptr) return;
    dispatcher.TryEnqueue([this, batch] {
      if (exiting_) return;
      if (notifications_) notifications_->ShowBoundary(batch.events, batch.plan);
      ApplyState();
    });
  });

  ApplyState();

  if (services.config().presentation.visible && overlay_) {
    overlay_->ShowOverlay();
  }
}

void App::ApplyState() {
  if (exiting_) return;
  auto& services = ClassTimeApp::Services::Instance();
  const auto& config = services.config();
  const auto& snapshot = services.snapshot();

  std::wstring tooltip = L"“上课”时间到！";
  if (snapshot.current.has_value() && snapshot.state != classtime::DayState::Idle) {
    const auto& current = *snapshot.current;
    tooltip += L" · ";
    tooltip += current.kind == classtime::SegmentKind::Focus
                   ? L"第 " + std::to_wstring(current.focusOrdinal) + L" 节 " +
                         ClassTimeApp::FormatPercent(snapshot.progress)
                   : ClassTimeApp::SegmentKindLabel(current.kind);
  } else if (snapshot.next.has_value()) {
    tooltip += L" · 下一节 " + ClassTimeApp::FormatClock(snapshot.next->start);
  } else if (snapshot.hasSchedule) {
    tooltip += L" · 今日安排已结束";
  } else {
    tooltip += L" · 今天没有安排";
  }
  if (config.notifications.muted) tooltip += L" · 提醒已暂停";

  if (tray_) {
    tray_->Update(tooltip, config.presentation.visible,
                  config.presentation.view == classtime::PresentationSettings::View::Ring,
                  config.presentation.scope == classtime::PresentationSettings::Scope::AllDay,
                  config.notifications.muted);
  }

  if (overlay_) {
    overlay_->ApplyState();
  }
  if (settingsWindow_ != nullptr) {
    settingsWindow_.as<winrt::ClassTime::MainWindow>().ApplyState();
  }
}

void App::ShowSettings() {
  if (settingsWindow_ == nullptr) return;
  auto window = settingsWindow_.as<winrt::ClassTime::MainWindow>();
  window.ShowSettings();
}

void App::HandleTrayCommand(UINT command) {
  auto& services = ClassTimeApp::Services::Instance();
  auto presentation = services.config().presentation;
  switch (command) {
    case ClassTimeApp::kTrayToggleOverlay:
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
    case ClassTimeApp::kTrayBarView:
      presentation.view = classtime::PresentationSettings::View::Bar;
      services.UpdatePresentation(presentation);
      break;
    case ClassTimeApp::kTrayRingView:
      presentation.view = classtime::PresentationSettings::View::Ring;
      services.UpdatePresentation(presentation);
      break;
    case ClassTimeApp::kTrayCurrentScope:
      presentation.scope = classtime::PresentationSettings::Scope::CurrentBlock;
      services.UpdatePresentation(presentation);
      break;
    case ClassTimeApp::kTrayAllDayScope:
      presentation.scope = classtime::PresentationSettings::Scope::AllDay;
      services.UpdatePresentation(presentation);
      break;
    case ClassTimeApp::kTrayToggleMute:
      services.SetMuted(!services.config().notifications.muted);
      break;
    case ClassTimeApp::kTrayOpenSettings:
      ShowSettings();
      break;
    case ClassTimeApp::kTrayExit:
      ExitApplication();
      break;
    default:
      break;
  }
}

void App::ExitApplication() {
  if (exiting_) return;
  exiting_ = true;
  ClassTimeApp::Services::Instance().SetChangeCallback(nullptr);
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
  ClassTimeApp::Services::Instance().Save();
  if (overlay_) {
    overlay_->Close();
    overlay_.reset();
  }
  if (settingsWindow_ != nullptr) {
    settingsWindow_.Close();
    settingsWindow_ = nullptr;
  }
  Application::Current().Exit();
}

}  // namespace winrt::ClassTime::implementation
