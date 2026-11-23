// 悬浮层：WinUI 3 内容 + Win32 窗口样式（置顶、不抢焦点、贴边吸附）。
// 用代码构建界面，避免每秒重建 XAML 树；进度更新只改画刷与几何。
#pragma once

#include <array>
#include <chrono>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace ClassTimeApp {

class OverlayWindow {
 public:
  OverlayWindow();
  ~OverlayWindow();

  void ApplyState();
  // 重新读取显示设置（视图/范围/贴边），必要时重建分节并重新定位。
  void ApplyPresentation();
  void ShowOverlay();
  void HideOverlay();
  void Close();

  bool visible() const { return visible_; }

 private:
  void CreateNativeWindow();
  void BuildBar();
  void BuildRing();
  void BuildDetailCard();
  void ShowDetail(const winrt::Microsoft::UI::Xaml::FrameworkElement& anchor,
                  const std::string& segmentId);
  void RefreshDetail();
  void HideDetail(bool animate = false);
  void AnimateDetail(bool show);
  void BindDrag();
  void CenterOverlay();
  void FinishDrag(bool savePosition);
  void SnapTo(POINT target, RECT workArea);
  void StopSnap();
  // 位置/尺寸立即按当前设置落位；补间滑行由 AnimateWindowRect 负责，
  // 避免贴边位置与显示形式切换时直接闪现。
  void LayoutWindow();
  void ApplyLayoutStyle();
  RECT ComputeLayoutRect();
  void ApplyWindowRect(const RECT& rect);
  enum class Layer { None, Bar, Ring, Handle };
  void AnimateWindowRect(const RECT& target, Layer outgoing);
  void AnimateScreenEdge(bool entering);
  Layer CurrentLayer() const;
  void StopWindowMotion();
  void FinishMorph();
  void SettleMotion();
  void QueueEdgeCollapse();
  void SetEdgeCollapsed(bool collapsed, bool animate = false);
  void ApplyEdgeCollapsed(bool collapsed, bool fade);
  void ResetEdgeMotion();
  winrt::Microsoft::UI::Xaml::FrameworkElement EdgeContent() const;
  void UpdateVisibleLayer();
  void RefreshEdgeHandle();
  void RefreshBar();
  void RefreshRing();
  void RefreshScreenEdge();
  void HideScreenEdge();
  // 屏幕边框条的透明度过渡：出现/消失不再硬切。
  void RenderStrips(int alpha);
  void UpdateWindowShape();
  void StartTimer();
  void StopTimer();

  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border barRoot_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid ringRoot_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border edgeHandle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border edgeTrack_{nullptr};
  winrt::Microsoft::UI::Xaml::Shapes::Rectangle edgeFill_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush edgeFillBrush_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel segmentPanel_{nullptr};
  winrt::Microsoft::UI::Xaml::Shapes::Path ringArc_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::ArcSegment ringSweep_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush ringAccent_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringTime_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringLabel_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock ringSummary_{nullptr};
  std::string ringDetailId_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
  winrt::event_token snapFrame_{};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer edgeTimer_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard edgeMotion_{nullptr};
  // 屏幕边框条整体透明度（0-255），切换形式时做淡入淡出。
  int stripAlpha_ = 255;
  winrt::event_token windowMotionFrame_{};
  Layer morphOutgoing_ = Layer::None;

  winrt::Microsoft::UI::Xaml::Controls::Primitives::Popup detailPopup_{nullptr};
  winrt::Microsoft::UI::Xaml::FrameworkElement detailAnchor_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border detailCard_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border detailMarker_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Border detailFill_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailTitle_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailRange_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailPercent_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailTiming_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailNext_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock detailMuted_{nullptr};
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard detailMotion_{nullptr};
  std::string detailSegmentId_;

  struct SegmentVisual {
    winrt::Microsoft::UI::Xaml::Controls::Grid host{nullptr};
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle track{nullptr};
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle fill{nullptr};
    double length = 0;
    double thickness = 0;
  };
  std::vector<SegmentVisual> visuals_;
  std::string visualSignature_;

  HWND hwnd_ = nullptr;
  bool visible_ = false;
  classtime::PresentationSettings::View view_ = classtime::PresentationSettings::View::Bar;
  bool allDay_ = true;
  bool autoHideAtEdge_ = false;
  bool flushWithEdge_ = true;
  bool edgeCollapsed_ = false;
  bool edgeClosing_ = false;
  std::string monitorId_;
  int offsetX_ = 0;
  int offsetY_ = 12;
  classtime::PresentationSettings::Edge edge_ = classtime::PresentationSettings::Edge::Bottom;
  double dpiScale_ = 1.0;
  int windowWidth_ = 0;
  int windowHeight_ = 0;
  RECT windowRegionBounds_{};
  std::array<int, 4> windowRegionRadii_{};
  Layer windowRegionLayer_ = Layer::None;
  std::optional<uint32_t> dragPointer_;
  POINT dragStart_{};
  POINT dragOrigin_{};
  POINT lastSnapPosition_{};
  bool dragging_ = false;
  ULONGLONG lastPressTick_ = 0;
  POINT lastPressPoint_{};
  int pressCount_ = 0;

  struct EdgeStrip {
    HWND hwnd = nullptr;
    RECT bounds{};
    int filledPixels = -1;
    uint32_t fill = 0;
    uint32_t track = 0;
    int alpha = -1;
  };
  std::array<EdgeStrip, 4> screenEdgeStrips_;
};

}  // namespace ClassTimeApp
