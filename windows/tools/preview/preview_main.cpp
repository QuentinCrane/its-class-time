// 技术预览入口：在没有 MSBuild / XAML 编译器的环境下，直接验证悬浮层、托盘与作息引擎。
// 正式设置窗口仍由 ClassTime.vcxproj 构建；此文件不参与 MSIX 打包。
#include "pch.h"

#include <MddBootstrap.h>

#include <fstream>

#include "app/boundary_monitor.h"
#include "app/overlay_window.h"
#include "app/services.h"
#include "app/tray_icon.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

void Log(const char* text) {
  std::ofstream stream("E:\\Github Program\\It'sClassTime\\windows\\build\\preview\\preview.log",
                       std::ios::app);
  stream << text << "\n";
  stream.flush();
}

std::wstring BuildTooltip() {
  auto& services = ClassTimeApp::Services::Instance();
  const auto& snapshot = services.snapshot();
  std::wstring tooltip = L"“上课”时间到！";
  if (snapshot.current.has_value()) {
    if (snapshot.current->kind == classtime::SegmentKind::Focus) {
      tooltip += L" · 第 " + std::to_wstring(snapshot.current->focusOrdinal) + L" 节 " +
                 ClassTimeApp::FormatPercent(snapshot.progress);
    } else {
      tooltip += L" · " + ClassTimeApp::SegmentKindLabel(snapshot.current->kind);
    }
  } else if (snapshot.next.has_value()) {
    tooltip += L" · 下一节 " + ClassTimeApp::FormatClock(snapshot.next->start);
  } else if (snapshot.hasSchedule) {
    tooltip += L" · 今日已结束";
  } else {
    tooltip += L" · 今天没有安排";
  }
  if (services.config().notifications.muted) tooltip += L" · 提醒已暂停";
  return tooltip;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  init_apartment(apartment_type::single_threaded);

  const HRESULT boot = MddBootstrapInitialize(0x00010006, L"", PACKAGE_VERSION{});
  if (FAILED(boot)) {
    wchar_t message[160];
    swprintf_s(message, L"Windows App Runtime 1.6 初始化失败：0x%08X", static_cast<unsigned>(boot));
    MessageBoxW(nullptr, message, L"ClassTime 预览", MB_ICONERROR);
    return static_cast<int>(boot);
  }

  Application::Start([instance](auto&&) {
    Log("enter Start");
    try {
      // 预览只使用 Grid/Border/TextBlock 等基础元素，不依赖 XamlControlsResources。
      Log("skip resources");

      auto& services = ClassTimeApp::Services::Instance();
      if (!services.Initialize()) {
        const std::wstring message =
            L"无法安全读取、备份或写入配置，预览已停止启动。\n"
            L"原有配置或已创建的备份已保留。\n"
            L"请检查该文件及其文件夹的访问权限，然后重试。\n\n" +
            services.configPath().wstring();
        MessageBoxW(nullptr, message.c_str(), L"ClassTime 预览", MB_OK | MB_ICONERROR);
        Application::Current().Exit();
        return;
      }
      Log("services ok");

      wchar_t demo[8]{};
      if (GetEnvironmentVariableW(L"CLASSTIME_PREVIEW_DEMO", demo, 8) > 0) {
        const auto now = ClassTimeApp::NowClock();
        const int base = std::max(0, now.MinutesSinceMidnight() - 10);
        auto make = [](const char* id, const wchar_t* label, classtime::SegmentKind kind, int start,
                       int end) {
          classtime::Segment segment;
          segment.id = id;
          segment.label = ClassTimeApp::ToUtf8(label);
          segment.kind = kind;
          segment.start = *classtime::TimeOfDay::FromTotalMinutes(start);
          segment.end = *classtime::TimeOfDay::FromTotalMinutes(end);
          return segment;
        };
        std::vector<classtime::Segment> demoPlan;
        demoPlan.push_back(make("demo1", L"演示 · 第 1 节", classtime::SegmentKind::Focus, base,
                                base + 45));
        demoPlan.push_back(make("demob1", L"课间", classtime::SegmentKind::Break, base + 45,
                                base + 55));
        demoPlan.push_back(make("demo2", L"演示 · 第 2 节", classtime::SegmentKind::Focus,
                                base + 55, base + 100));
        demoPlan.push_back(make("demol", L"午休", classtime::SegmentKind::Lunch, base + 100,
                                base + 130));
        demoPlan.push_back(make("demo3", L"演示 · 第 3 节", classtime::SegmentKind::Focus,
                                base + 130, base + 175));
        std::string error;
        if (!services.ApplyScheduleEdit(demoPlan, ClassTimeApp::Services::SaveScope::TodayOnly,
                                        &error)) {
          Log(("demo failed: " + error).c_str());
        } else {
          Log("demo plan applied");
        }

        // 预览时放到屏幕顶部，便于观察与截图（正式配置不受影响）。
        auto presentation = services.config().presentation;
        presentation.edge = classtime::PresentationSettings::Edge::Top;
        presentation.offsetY = 80;
        services.UpdatePresentation(presentation);
      }

      auto overlay = std::make_unique<ClassTimeApp::OverlayWindow>();
      Log("overlay ok");
      ClassTimeApp::TrayIcon tray;

      auto refresh = [&] {
        Log("refresh");
        overlay->ApplyPresentation();
        const auto& config = services.config();
        tray.Update(BuildTooltip(), config.presentation.visible,
                    config.presentation.view == classtime::PresentationSettings::View::Ring,
                    config.presentation.scope == classtime::PresentationSettings::Scope::AllDay,
                    config.notifications.muted);
      };

      tray.Create(instance, [&](UINT command) {
        Log("tray command");
        auto presentation = services.config().presentation;
        switch (command) {
          case ClassTimeApp::kTrayToggleOverlay:
            presentation.visible = !presentation.visible;
            services.UpdatePresentation(presentation);
            if (presentation.visible) {
              overlay->ShowOverlay();
            } else {
              overlay->HideOverlay();
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
            MessageBoxW(nullptr,
                        L"本次为无 MSBuild 的技术预览：悬浮层与托盘可直接验证。\n"
                        L"完整设置窗口需要 XAML 编译（或后续开放 MSBuild）。",
                        L"ClassTime 预览", MB_ICONINFORMATION);
            break;
          case ClassTimeApp::kTrayExit:
            Application::Current().Exit();
            break;
          default:
            break;
        }
      });
      Log("tray ok");

      services.SetChangeCallback([refresh](ClassTimeApp::Services::Change) { refresh(); });

      auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
      auto monitor = std::make_unique<ClassTimeApp::BoundaryMonitor>();
      monitor->Start([&refresh, dispatcher](const ClassTimeApp::BoundaryMonitor::FiredBatch&) {
        dispatcher.TryEnqueue([&refresh] { refresh(); });
      });
      Log("monitor ok");

      refresh();
      Log("refresh ok");
      if (services.config().presentation.visible) overlay->ShowOverlay();
      Log("shown");
    } catch (const std::exception& error) {
      Log((std::string("std exception: ") + error.what()).c_str());
    } catch (const winrt::hresult_error& error) {
      Log(("hresult: " + winrt::to_string(error.message())).c_str());
    } catch (...) {
      Log("unknown exception");
    }
  });

  MddBootstrapShutdown();
  return 0;
}
