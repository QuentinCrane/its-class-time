// tools/screenshot.cpp — 截屏并保存为 24 位 BMP，用于本机 UI 验证。
// 用法: screenshot.exe <输出.bmp> [窗口标题]
//       给出标题时用 FindWindow 定位窗口（前台激活 + PrintWindow 截取窗口内容）。
#include <windows.h>
#include <cstdio>
#include <vector>

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    wprintf(L"usage: screenshot.exe <output.bmp> [window-title]\n");
    return 1;
  }
  HWND target = nullptr;
  int width = 0, height = 0;
  HDC screen = nullptr, mem = nullptr;
  HBITMAP bmp = nullptr;

  if (argc >= 3) {
    target = FindWindowW(nullptr, argv[2]);
    if (!target) {
      wprintf(L"window not found: %s\n", argv[2]);
      return 4;
    }
    SetForegroundWindow(target);
    ShowWindow(target, SW_RESTORE);
    Sleep(400);
    RECT rc{};
    if (!GetClientRect(target, &rc)) return 5;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
      wprintf(L"window has zero client size\n");
      return 6;
    }
    HDC wdc = GetDC(target);
    mem = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, width, height);
    SelectObject(mem, bmp);
    PrintWindow(target, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    ReleaseDC(target, wdc);
    screen = nullptr;
  } else {
    screen = GetDC(nullptr);
    if (!screen) return 2;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    mem = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, width, height);
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, width, height, screen, 0, 0, SRCCOPY);
  }

  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = width;
  bi.biHeight = -height;  // top-down
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  const DWORD row = ((width * 3 + 3) / 4) * 4;
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + row * static_cast<DWORD>(height);

  FILE* f = nullptr;
  _wfopen_s(&f, argv[1], L"wb");
  if (!f) return 3;
  std::vector<BYTE> pixels(row * static_cast<size_t>(height));
  GetDIBits(mem, bmp, 0, height, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
  fwrite(&fh, sizeof(fh), 1, f);
  fwrite(&bi, sizeof(bi), 1, f);
  fwrite(pixels.data(), 1, pixels.size(), f);
  fclose(f);

  SelectObject(mem, bmp);
  DeleteObject(bmp);
  DeleteDC(mem);
  if (screen) ReleaseDC(nullptr, screen);
  wprintf(L"saved %s (%dx%d)\n", argv[1], width, height);
  return 0;
}
