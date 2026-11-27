// 共享显示器查找与目标显示器全屏窗口检测工具（纯官方 Win32 / DWM API）。
#pragma once

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <cstring>
#include <string>

#include "services.h"

namespace ClassTimeApp::DisplayUtils {

inline HMONITOR FindMonitor(const std::string& id) {
  struct Search {
    std::wstring device;
    HMONITOR monitor = nullptr;
  } search{ToWide(id)};
  if (!search.device.empty()) {
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
      auto& search = *reinterpret_cast<Search*>(data);
      MONITORINFOEXW info{};
      info.cbSize = sizeof(info);
      if (GetMonitorInfoW(monitor, &info) && search.device == info.szDevice) {
        search.monitor = monitor;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
  }
  return search.monitor ? search.monitor : MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
}

inline bool IsShellOrDesktopWindow(HWND hwnd) {
  if (!hwnd || hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return true;
  wchar_t className[64]{};
  if (GetClassNameW(hwnd, className, 64) <= 0) return false;
  return wcscmp(className, L"Progman") == 0 ||
         wcscmp(className, L"WorkerW") == 0 ||
         wcscmp(className, L"Shell_TrayWnd") == 0 ||
         wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
         wcscmp(className, L"NotifyIconOverflowWindow") == 0 ||
         wcscmp(className, L"TaskListThumbnailWnd") == 0;
}

inline bool IsWindowCloaked(HWND hwnd) {
  BOOL cloaked = FALSE;
  return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
         cloaked != FALSE;
}

inline bool GetWindowVisualFrame(HWND hwnd, RECT* out) {
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, out, sizeof(*out)))) {
    return !IsRectEmpty(out);
  }
  return GetWindowRect(hwnd, out) != FALSE && !IsRectEmpty(out);
}

inline bool RectCoversMonitor(const RECT& rect, const RECT& monitor) {
  return rect.left <= monitor.left && rect.top <= monitor.top &&
         rect.right >= monitor.right && rect.bottom >= monitor.bottom;
}

inline bool IsMonitorInFullscreen(HMONITOR targetMonitor) {
  if (!targetMonitor) return false;
  MONITORINFO targetInfo{sizeof(targetInfo)};
  if (!GetMonitorInfoW(targetMonitor, &targetInfo)) return false;

  const HWND foreground = GetForegroundWindow();
  const DWORD currentPid = GetCurrentProcessId();
  if (foreground != nullptr) {
    DWORD fgPid = 0;
    GetWindowThreadProcessId(foreground, &fgPid);
    // 本应用窗口（例如设置窗口）处于前台时不触发全屏隐藏，方便用户调整。
    if (fgPid == currentPid) return false;

    if (IsWindowVisible(foreground) && !IsIconic(foreground) &&
        !IsShellOrDesktopWindow(foreground) && !IsWindowCloaked(foreground)) {
      RECT frame{};
      if (GetWindowVisualFrame(foreground, &frame) &&
          RectCoversMonitor(frame, targetInfo.rcMonitor)) {
        return true;
      }
      if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) == targetMonitor) {
        return false;
      }
    }
  }

  // 多屏场景下若焦点在另一块屏幕，扫描目标显示器 Z 序最顶层的可见普通窗口。
  struct ScanContext {
    RECT monitor{};
    DWORD currentPid = 0;
    bool foundTopWindow = false;
    bool fullscreen = false;
  } scan{targetInfo.rcMonitor, currentPid};
  EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
    auto& ctx = *reinterpret_cast<ScanContext*>(data);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsShellOrDesktopWindow(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx.currentPid) return TRUE;
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) != 0) return TRUE;
    if (IsWindowCloaked(hwnd)) return TRUE;
    RECT frame{};
    if (!GetWindowVisualFrame(hwnd, &frame)) return TRUE;
    RECT overlap{};
    if (!IntersectRect(&overlap, &frame, &ctx.monitor)) return TRUE;
    ctx.foundTopWindow = true;
    ctx.fullscreen = RectCoversMonitor(frame, ctx.monitor);
    return FALSE;
  }, reinterpret_cast<LPARAM>(&scan));
  if (scan.foundTopWindow && scan.fullscreen) return true;

  QUERY_USER_NOTIFICATION_STATE state{};
  if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
      (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE)) {
    if (!foreground) return true;
    if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST) == targetMonitor) return true;
  }
  return false;
}

}  // namespace ClassTimeApp::DisplayUtils
