#pragma once

#include <windows.h>
#include <appmodel.h>
#include <string>

namespace ClassTimeApp::LoginStartup {

inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kValueName[] = L"ClassTime.Desktop";

inline bool IsUnpackaged() {
  UINT32 length = 0;
  return GetCurrentPackageFullName(&length, nullptr) == APPMODEL_ERROR_NO_PACKAGE;
}

inline std::wstring Command() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (!length || length + 2 > MAX_PATH) return {};
  return L"\"" + std::wstring(path, length) + L"\"";
}

inline bool Registered() {
  wchar_t value[MAX_PATH + 1]{};
  DWORD bytes = sizeof(value);
  const auto status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                                   nullptr, value, &bytes);
  return status == ERROR_SUCCESS && value[0] && _wcsicmp(value, Command().c_str()) == 0;
}

inline bool SetRegistered(bool enabled) {
  if (!enabled) {
    if (!Registered()) return true;
    const auto status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
  }
  const auto command = Command();
  if (command.empty()) return false;
  return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                         static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

}  // namespace ClassTimeApp::LoginStartup
