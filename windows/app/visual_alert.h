// 系统通知之外的可选视觉到点动效：全屏边缘柔光呼吸 + 掉落放大 3D 电子/机械时钟。
// 不替代或绕过系统通知；空闲时零常驻窗口与零定时器，绝不抢占前台焦点。
#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <array>
#include <chrono>
#include <vector>

#include "notification_service.h"
#include "widget_appearance.h"

namespace ClassTimeApp {

class VisualAlert {
 public:
  explicit VisualAlert(winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);
  ~VisualAlert();

  // 真实边界到达时调用（遵守 screenEdgeFlash / dropClockAlert 及 hideInFullscreen）。
  void Trigger(const BoundaryAlert& alert);
  // 设置页「测试视觉提醒」调用（跳过全屏抑制；若两项开关均关闭则返回 false）。
  bool TriggerPreview();
  void StopAll();

  static LRESULT CALLBACK ClockWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

 private:
  struct FlashStrip {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    RECT bounds{};
  };

  void StartFlash(const BoundaryAlert& alert, const RECT& area, double dpiScale);
  void TickFlash();
  void StopFlash();

  void StartClock(const BoundaryAlert& alert, const RECT& area, double dpiScale);
  void TickClock();
  void BeginClockRetract();
  void StopClock();
  bool EnsureClockResources(int pxWidth, int pxHeight, double dpiScale);
  void ReleaseClockResources();
  void RenderClockFrame(double rotXDeg, double rotYDeg, double scale, double dropDip,
                        double flipProgress, double sweepProgress, double opacity);

  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};

  // —— 四边柔光呼吸闪光 ——
  std::array<FlashStrip, 4> flashStrips_{};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer flashTimer_{nullptr};
  std::chrono::steady_clock::time_point flashStart_{};
  bool flashAnimated_ = true;

  // —— 掉落 3D 时钟 ——
  HWND clockHwnd_ = nullptr;
  HDC clockCanvasDc_ = nullptr;
  HBITMAP clockCanvasBmp_ = nullptr;
  HGDIOBJ clockCanvasPrev_ = nullptr;
  void* clockCanvasPixels_ = nullptr;
  HDC clockOutDc_ = nullptr;
  HBITMAP clockOutBmp_ = nullptr;
  HGDIOBJ clockOutPrev_ = nullptr;
  void* clockOutPixels_ = nullptr;
  int clockCanvasPxW_ = 0;
  int clockCanvasPxH_ = 0;
  int clockOutPxW_ = 0;
  int clockOutPxH_ = 0;
  double clockDpiScale_ = 1.0;
  double clockCardWidthDip_ = 300.0;
  double clockCardHeightDip_ = 176.0;
  RECT clockAnchorRect_{};
  BoundaryAlert clockAlert_{};
  classtime::NotificationSettings::AlertClockStyle clockStyle_ =
      classtime::NotificationSettings::AlertClockStyle::Digital3D;
  classtime::NotificationSettings::AlertClockPosition clockPosition_ =
      classtime::NotificationSettings::AlertClockPosition::TopCenter;
  WidgetAppearance::ClockAppearance clockAppearance_{};
  bool clockFaceDirty_ = true;
  double lastRenderedFlip_ = -1.0;
  double lastRenderedSweep_ = -1.0;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer clockTimer_{nullptr};
  std::chrono::steady_clock::time_point clockPhaseStart_{};
  enum class ClockPhase { Idle, Intro, Hold, Retract };
  ClockPhase clockPhase_ = ClockPhase::Idle;
  std::chrono::seconds clockHoldDuration_{4};
  bool clockAnimated_ = true;
  bool clockHovered_ = false;
  bool clockTrackingMouse_ = false;
  std::chrono::steady_clock::duration clockHoldElapsed_{};
  double retractStartRotX_ = -4.0;
  double retractStartRotY_ = 0.0;
  double retractStartScale_ = 1.0;
  double retractStartDropDip_ = 0.0;

  winrt::com_ptr<ID2D1Factory> d2dFactory_;
  winrt::com_ptr<ID2D1DCRenderTarget> d2dTarget_;
  winrt::com_ptr<IDWriteFactory> dwriteFactory_;
  winrt::com_ptr<IDWriteTextFormat> fmtDigitalDigits_;
  winrt::com_ptr<IDWriteTextFormat> fmtDigitalBadge_;
  winrt::com_ptr<IDWriteTextFormat> fmtDigitalTitle_;
  winrt::com_ptr<IDWriteTextFormat> fmtDigitalFooter_;
  winrt::com_ptr<IDWriteTextFormat> fmtMechNumeral_;
  winrt::com_ptr<IDWriteTextFormat> fmtMechSmall_;
  winrt::com_ptr<IDWriteTextFormat> fmtMechTitle_;
  winrt::com_ptr<IDWriteTextFormat> fmtMechFooter_;
};

}  // namespace ClassTimeApp
