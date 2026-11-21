// 托盘图标：Shell_NotifyIcon + 原生菜单，关闭设置窗口后仍可恢复悬浮窗与退出。
#pragma once

namespace ClassTimeApp {

enum TrayCommand : UINT {
  kTrayToggleOverlay = 100,
  kTrayBarView = 101,
  kTrayRingView = 102,
  kTrayCurrentScope = 103,
  kTrayAllDayScope = 104,
  kTrayToggleMute = 105,
  kTrayOpenSettings = 106,
  kTrayExit = 107,
};

class TrayIcon {
 public:
  using CommandHandler = std::function<void(UINT command)>;

  bool Create(HINSTANCE instance, CommandHandler handler);
  void Destroy();

  // 更新菜单勾选状态与提示文字。
  void Update(const std::wstring& tooltip, bool overlayVisible, bool ringView, bool allDayScope,
              bool muted);

  HWND window() const { return window_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void ShowMenu();
  void AddIcon();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HICON icon_ = nullptr;
  CommandHandler handler_;
  NOTIFYICONDATAW data_{};
  UINT taskbarCreatedMessage_ = 0;
  bool added_ = false;
  std::wstring tooltip_ = L"“上课”时间到！";
  bool overlayVisible_ = true;
  bool ringView_ = false;
  bool allDayScope_ = true;
  bool muted_ = false;
};

}  // namespace ClassTimeApp
