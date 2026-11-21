#include "pch.h"
#include "tray_icon.h"

#include "resource.h"

namespace ClassTimeApp {
namespace {
constexpr UINT kCallbackMessage = WM_APP + 41;
constexpr wchar_t kWindowClass[] = L"ClassTimeTrayWindow";
}  // namespace

bool TrayIcon::Create(HINSTANCE instance, CommandHandler handler) {
  instance_ = instance;
  handler_ = std::move(handler);
  taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &TrayIcon::WindowProc;
  windowClass.hInstance = instance_;
  windowClass.lpszClassName = kWindowClass;
  RegisterClassExW(&windowClass);

  window_ = CreateWindowExW(0, kWindowClass, L"ClassTime", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            instance_, this);
  if (window_ == nullptr) return false;

  icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_CLASSTIME), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (icon_ == nullptr) icon_ = LoadIconW(nullptr, IDI_APPLICATION);

  data_.cbSize = sizeof(data_);
  data_.hWnd = window_;
  data_.uID = 1;
  data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data_.uCallbackMessage = kCallbackMessage;
  data_.hIcon = icon_;
  wcscpy_s(data_.szTip, tooltip_.c_str());
  AddIcon();
  return true;
}

void TrayIcon::AddIcon() {
  added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
  if (added_) {
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
  }
}

void TrayIcon::Destroy() {
  if (added_) {
    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
  }
  if (window_ != nullptr) {
    DestroyWindow(window_);
    window_ = nullptr;
  }
  if (icon_ != nullptr) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
}

void TrayIcon::Update(const std::wstring& tooltip, bool overlayVisible, bool ringView,
                      bool allDayScope, bool muted) {
  tooltip_ = tooltip;
  overlayVisible_ = overlayVisible;
  ringView_ = ringView;
  allDayScope_ = allDayScope;
  muted_ = muted;
  if (!added_) return;
  wcscpy_s(data_.szTip, tooltip_.c_str());
  Shell_NotifyIconW(NIM_MODIFY, &data_);
}

LRESULT CALLBACK TrayIcon::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* self = static_cast<TrayIcon*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->window_ = hwnd;
  }
  auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != nullptr) return self->HandleMessage(message, wparam, lparam);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
    // 资源管理器重启后重建图标。
    AddIcon();
    return 0;
  }
  if (message == kCallbackMessage) {
    switch (LOWORD(lparam)) {
      case WM_LBUTTONUP:
        if (handler_) handler_(kTrayOpenSettings);
        return 0;
      case WM_RBUTTONUP:
      case WM_CONTEXTMENU:
        ShowMenu();
        return 0;
      default:
        return 0;
    }
  }
  if (message == WM_COMMAND) {
    const UINT command = LOWORD(wparam);
    if (handler_) handler_(command);
    return 0;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

void TrayIcon::ShowMenu() {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;
  AppendMenuW(menu, MF_STRING, kTrayOpenSettings, L"打开设置");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (overlayVisible_ ? MF_CHECKED : MF_UNCHECKED), kTrayToggleOverlay,
              L"显示悬浮窗");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (!ringView_ ? MF_CHECKED : MF_UNCHECKED), kTrayBarView, L"分节长条");
  AppendMenuW(menu, MF_STRING | (ringView_ ? MF_CHECKED : MF_UNCHECKED), kTrayRingView, L"圆环");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (!allDayScope_ ? MF_CHECKED : MF_UNCHECKED), kTrayCurrentScope,
              L"当前时段");
  AppendMenuW(menu, MF_STRING | (allDayScope_ ? MF_CHECKED : MF_UNCHECKED), kTrayAllDayScope,
              L"全天");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (muted_ ? MF_CHECKED : MF_UNCHECKED), kTrayToggleMute,
              L"暂停提醒");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayExit, L"退出");

  POINT cursor{};
  GetCursorPos(&cursor);
  SetForegroundWindow(window_);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                      cursor.x, cursor.y, 0, window_, nullptr);
  DestroyMenu(menu);
  PostMessageW(window_, WM_NULL, 0, 0);
  if (command != 0 && handler_) handler_(command);
}

}  // namespace ClassTimeApp
