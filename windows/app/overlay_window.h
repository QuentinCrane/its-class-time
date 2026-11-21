// 悬浮层：WinUI 3 内容 + Win32 窗口样式（置顶、不抢焦点、贴边吸附）。
// 用代码构建界面，避免每秒重建 XAML 树；进度更新只改画刷与几何。
#pragma once

namespace ClassTimeApp {

class OverlayWindow {
 public:
  OverlayWindow();
  ~OverlayWindow();

  void ApplyState();
  void ShowOverlay();
  void HideOverlay();
  void Close();

  bool visible() const { return visible_; }

 private:
  void CreateNativeWindow();
  void BuildBar();
  void BuildRing();
  void ApplyPresentation();
  void RefreshBar();
  void RefreshRing();
  void LayoutWindow();
  void UpdateTooltip(size_t index);
  void StartTimer();
  void StopTimer();

  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid barRoot_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid ringRoot_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel segmentPanel_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock stateText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock progressText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock nextText_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock pauseText_{nullptr};
  winrt::Microsoft::UI::Xaml::Shapes::Path ringArc_{nullptr};
  winrt::Microsoft::UI::Xaml::Shapes::Path ringFull_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringPercent_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringLabel_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringNext_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel ringDots_{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};

  struct SegmentVisual {
    std::string id;
    classtime::SegmentKind kind = classtime::SegmentKind::Focus;
    winrt::Microsoft::UI::Xaml::Controls::Grid host{nullptr};
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle track{nullptr};
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle fill{nullptr};
    double width = 0;
  };
  std::vector<SegmentVisual> visuals_;
  std::string visualSignature_;

  HWND hwnd_ = nullptr;
  bool visible_ = false;
  bool created_ = false;
  bool ringMode_ = false;
  bool allDay_ = true;
  int offsetX_ = 0;
  int offsetY_ = 12;
  classtime::PresentationSettings::Edge edge_ = classtime::PresentationSettings::Edge::Bottom;
  double dpiScale_ = 1.0;
  int windowWidth_ = 0;
  int windowHeight_ = 0;
};

}  // namespace ClassTimeApp
