#include "pch.h"
#include "tray_icon.h"

#include "resource.h"

#include <winrt/Microsoft.UI.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;

namespace ClassTimeApp {
namespace {
constexpr UINT kCallbackMessage = WM_APP + 41;
constexpr wchar_t kWindowClass[] = L"ClassTimeTrayWindow";
}  // namespace

bool TrayIcon::Create(HINSTANCE instance, CommandHandler handler) {
  instance_ = instance;
  handler_ = std::move(handler);
  menuLifetime_ = std::make_shared<int>(0);
  taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &TrayIcon::WindowProc;
  windowClass.hInstance = instance_;
  windowClass.lpszClassName = kWindowClass;
  RegisterClassExW(&windowClass);

  // TaskbarCreated, power and clock changes are broadcast to top-level windows.
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass, L"ClassTime",
                            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
  if (window_ == nullptr) return false;

  icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_CLASSTIME), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (icon_ == nullptr) icon_ = LoadIconW(nullptr, IDI_APPLICATION);

  data_.cbSize = sizeof(data_);
  data_.hWnd = window_;
  data_.uID = 1;
  data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  data_.uCallbackMessage = kCallbackMessage;
  data_.hIcon = icon_;
  wcsncpy_s(data_.szTip, tooltip_.c_str(), _TRUNCATE);
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
  menuPending_ = false;
  menuLifetime_.reset();
  if (menu_) {
    menu_.Hide();
    menu_ = nullptr;
  }
  if (menuWindow_) {
    menuWindow_.Close();
    menuWindow_ = nullptr;
    menuAnchor_ = nullptr;
    menuHwnd_ = nullptr;
  }
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

void TrayIcon::Update(const std::wstring& tooltip,
                      const classtime::PresentationSettings& presentation, bool muted) {
  tooltip_ = tooltip;
  presentation_ = presentation;
  muted_ = muted;
  if (!added_) return;
  wcsncpy_s(data_.szTip, tooltip_.c_str(), _TRUNCATE);
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
  if (message == WM_TIMECHANGE ||
      (message == WM_POWERBROADCAST && (wparam == PBT_APMRESUMEAUTOMATIC ||
                                        wparam == PBT_APMRESUMESUSPEND))) {
    if (handler_) handler_(kTrayClockChanged);
    return TRUE;
  }
  if (message == kCallbackMessage) {
    switch (LOWORD(lparam)) {
      case NIN_KEYSELECT:
      case NIN_SELECT:
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

POINT TrayIcon::MenuPosition() const {
  NOTIFYICONIDENTIFIER id{};
  id.cbSize = sizeof(id);
  id.hWnd = window_;
  id.uID = data_.uID;
  RECT bounds{};
  if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &bounds))) {
    return {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
  }
  POINT cursor{};
  GetCursorPos(&cursor);
  return cursor;
}

void TrayIcon::ShowMenu() {
  if (menuPending_) return;
  try {
    EnsureMenuWindow();
    menuPending_ = true;
    menuOpenRequested_ = false;

    MenuFlyout flyout;
    flyout.Placement(FlyoutPlacementMode::Top);
    // 托盘锚点只有几个像素，菜单必须允许延伸到该 XAML 根窗口以外。
    flyout.ShouldConstrainToRootBounds(false);
    const auto icon = [](const wchar_t* glyph) {
      FontIcon element;
      element.Glyph(glyph);
      return element;
    };
    // 将命令投递回托盘窗口，避免“退出”在 WinUI 点击回调中释放自身。
    const auto appendAction = [&](const wchar_t* text, const wchar_t* glyph, UINT command) {
      MenuFlyoutItem item;
      item.Text(text);
      item.Icon(icon(glyph));
      item.Click([target = window_, command](auto const&, auto const&) {
        PostMessageW(target, WM_COMMAND, MAKEWPARAM(command, 0), 0);
      });
      flyout.Items().Append(item);
    };
    const auto appendChoice = [&](MenuFlyoutSubItem const& parent, const wchar_t* text,
                                  const wchar_t* group, bool selected, UINT command) {
      RadioMenuFlyoutItem item;
      item.Text(text);
      item.GroupName(group);
      item.IsChecked(selected);
      item.Click([target = window_, command](auto const&, auto const&) {
        PostMessageW(target, WM_COMMAND, MAKEWPARAM(command, 0), 0);
      });
      parent.Items().Append(item);
    };

    appendAction(L"打开设置", L"\uE713", kTrayOpenSettings);
    appendAction(presentation_.visible ? L"隐藏桌面组件" : L"显示桌面组件", L"\uE7F4",
                 kTrayToggleOverlay);
    flyout.Items().Append(MenuFlyoutSeparator());

    using View = classtime::PresentationSettings::View;
    const wchar_t* viewLabel = presentation_.view == View::Ring ? L"圆环"
                               : presentation_.view == View::ScreenEdge ? L"屏幕边框"
                                                                        : L"分节长条";
    MenuFlyoutSubItem view;
    view.Text(hstring(std::wstring(L"显示形式  ·  ") + viewLabel));
    view.Icon(icon(L"\uE7F4"));
    appendChoice(view, L"分节长条", L"ClassTimeView", presentation_.view == View::Bar,
                 kTrayBarView);
    appendChoice(view, L"圆环", L"ClassTimeView", presentation_.view == View::Ring,
                 kTrayRingView);
    appendChoice(view, L"屏幕边框", L"ClassTimeView", presentation_.view == View::ScreenEdge,
                 kTrayScreenEdgeView);
    flyout.Items().Append(view);

    const bool allDay = presentation_.scope == classtime::PresentationSettings::Scope::AllDay;
    MenuFlyoutSubItem scope;
    scope.Text(allDay ? L"显示范围  ·  全天" : L"显示范围  ·  当前时段");
    scope.Icon(icon(L"\uE823"));
    appendChoice(scope, L"当前时段", L"ClassTimeScope", !allDay, kTrayCurrentScope);
    appendChoice(scope, L"全天", L"ClassTimeScope", allDay, kTrayAllDayScope);
    flyout.Items().Append(scope);
    flyout.Items().Append(MenuFlyoutSeparator());
    appendAction(muted_ ? L"恢复提醒" : L"暂停提醒", muted_ ? L"\uE768" : L"\uE7ED",
                 kTrayToggleMute);
    appendAction(L"退出", L"\uE8BB", kTrayExit);
    const std::weak_ptr<int> lifetime = menuLifetime_;
    flyout.Closed([this, lifetime](auto const&, auto const&) {
      if (lifetime.expired()) return;
      menuPending_ = false;
      if (menuHwnd_) ShowWindow(menuHwnd_, SW_HIDE);
    });
    menu_ = flyout;

    const POINT anchor = MenuPosition();
    SetWindowPos(menuHwnd_, HWND_TOPMOST, anchor.x, anchor.y, 2, 2,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    menuWindow_.Activate();
    // 等待内容岛完成布局后再定位菜单。
    const auto dispatcher = menuWindow_.DispatcherQueue();
    if (!dispatcher.TryEnqueue([this, lifetime] {
          if (lifetime.expired()) return;
          OpenMenuFlyout();
        })) {
      menuPending_ = false;
      ShowWindow(menuHwnd_, SW_HIDE);
      ShowFallbackMenu();
    }
  } catch (...) {
    menuPending_ = false;
    if (menuHwnd_) ShowWindow(menuHwnd_, SW_HIDE);
    ShowFallbackMenu();
  }
}

void TrayIcon::OpenMenuFlyout() {
  if (!menuPending_ || menuOpenRequested_ || !menu_ || !menuAnchor_ || !menuAnchor_.IsLoaded()) {
    return;
  }
  menuOpenRequested_ = true;
  try {
    menu_.ShowAt(menuAnchor_);
  } catch (...) {
    menuPending_ = false;
    if (menuHwnd_) ShowWindow(menuHwnd_, SW_HIDE);
    ShowFallbackMenu();
  }
}

void TrayIcon::EnsureMenuWindow() {
  if (menuWindow_) return;
  Window window;
  window.Title(L"ClassTime Tray Menu");
  auto appWindow = window.AppWindow();
  if (auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.SetBorderAndTitleBar(false, false);
    presenter.IsResizable(false);
    presenter.IsMinimizable(false);
    presenter.IsMaximizable(false);
  }
  Grid anchor;
  const std::weak_ptr<int> lifetime = menuLifetime_;
  anchor.Loaded([this, lifetime](auto const&, auto const&) {
    if (!lifetime.expired()) OpenMenuFlyout();
  });
  window.Content(anchor);
  HWND hwnd = reinterpret_cast<HWND>(Microsoft::UI::GetWindowFromWindowId(appWindow.Id()));
  if (!hwnd) throw hresult_error(E_FAIL);
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                    (exStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)) | WS_EX_TOOLWINDOW);
  SetWindowPos(hwnd, nullptr, 0, 0, 2, 2,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  menuWindow_ = window;
  menuAnchor_ = anchor;
  menuHwnd_ = hwnd;
}

void TrayIcon::ShowFallbackMenu() {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;
  AppendMenuW(menu, MF_STRING, kTrayOpenSettings, L"打开设置");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (presentation_.visible ? MF_CHECKED : MF_UNCHECKED), kTrayToggleOverlay,
              L"显示悬浮窗");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  using View = classtime::PresentationSettings::View;
  const auto appendView = [&](View view, UINT command, const wchar_t* label) {
    AppendMenuW(menu, MF_STRING | (presentation_.view == view ? MF_CHECKED : MF_UNCHECKED),
                command, label);
  };
  appendView(View::Bar, kTrayBarView, L"分节长条");
  appendView(View::Ring, kTrayRingView, L"圆环");
  appendView(View::ScreenEdge, kTrayScreenEdgeView, L"屏幕边框");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  const bool allDay = presentation_.scope == classtime::PresentationSettings::Scope::AllDay;
  AppendMenuW(menu, MF_STRING | (!allDay ? MF_CHECKED : MF_UNCHECKED), kTrayCurrentScope,
              L"当前时段");
  AppendMenuW(menu, MF_STRING | (allDay ? MF_CHECKED : MF_UNCHECKED), kTrayAllDayScope,
              L"全天");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (muted_ ? MF_CHECKED : MF_UNCHECKED), kTrayToggleMute,
              L"暂停提醒");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayExit, L"退出");

  const POINT anchor = MenuPosition();
  SetForegroundWindow(window_);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                      anchor.x, anchor.y, 0, window_, nullptr);
  DestroyMenu(menu);
  PostMessageW(window_, WM_NULL, 0, 0);
  if (command != 0 && handler_) handler_(command);
}

}  // namespace ClassTimeApp
