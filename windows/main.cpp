// ClassTime 入口：打包运行时由 MSIX 激活；解包运行时通过 Windows App SDK Bootstrapper 初始化。
#include "pch.h"

#include <appmodel.h>
#include <MddBootstrap.h>

#include "app/desktop_app.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

bool HasPackageIdentity() {
  UINT32 length = 0;
  return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // Inno Setup checks this marker before an upgrade or uninstall. The OS
  // releases it when the process exits, including after tray shutdown.
  CreateMutexW(nullptr, FALSE, L"ClassTime.Desktop.Running");
  init_apartment(apartment_type::single_threaded);

  bool bootstrapped = false;
  if (!HasPackageIdentity()) {
    const HRESULT hr = MddBootstrapInitialize(0x00010006, L"", PACKAGE_VERSION{});
    if (FAILED(hr)) {
      wchar_t message[192];
      swprintf_s(message,
                 L"无法初始化 Windows App Runtime 1.6（0x%08X）。\n请先安装 Windows App SDK 运行时。",
                 static_cast<unsigned>(hr));
      MessageBoxW(nullptr, message, L"“上课”时间到！", MB_ICONERROR);
      return static_cast<int>(hr);
    }
    bootstrapped = true;
  }

  try {
    Application::Start([](auto&&) { make<ClassTimeApp::DesktopApp>(); });
  } catch (const hresult_error& error) {
    MessageBoxW(nullptr, error.message().c_str(), L"“上课”时间到！", MB_ICONERROR);
  }

  if (bootstrapped) MddBootstrapShutdown();
  return 0;
}
