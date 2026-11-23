#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace ClassTimeApp::Motion {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Animation;

inline bool Enabled() {
  BOOL enabled = FALSE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && enabled;
}

inline void Stop(Storyboard& storyboard) {
  if (auto running = std::exchange(storyboard, nullptr)) running.Stop();
}

inline void Animate(const Storyboard& storyboard, const DependencyObject& target,
                    const wchar_t* property, double from, double to, int milliseconds,
                    const EasingFunctionBase& easing) {
  DoubleAnimation animation;
  animation.From(from);
  animation.To(to);
  animation.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(milliseconds)));
  animation.EasingFunction(easing);
  Storyboard::SetTarget(animation, target);
  Storyboard::SetTargetProperty(animation, property);
  storyboard.Children().Append(animation);
}

struct Pose {
  double opacity = 1;
  double y = 0;
  double x = 0;
};

inline Pose ReadPose(const FrameworkElement& surface, const FrameworkElement& content) {
  const auto offset = content.RenderTransform().try_as<TranslateTransform>();
  return {surface.Opacity(), offset ? offset.Y() : 0, offset ? offset.X() : 0};
}

// 只做透明度过渡：用于内容换层时的淡入淡出。
inline Storyboard Fade(const DependencyObject& target, double from, double to, int milliseconds,
                       bool leaving = false) {
  Storyboard storyboard;
  SineEase easing;
  easing.EasingMode(leaving ? EasingMode::EaseIn : EasingMode::EaseOut);
  Animate(storyboard, target, L"Opacity", from, to, milliseconds, easing);
  return storyboard;
}

// 以指定中心轻微缩放并淡入：形状改变后用来交代“从旧位置落位”。
inline Storyboard Pop(const FrameworkElement& content, double centerX, double centerY,
                      double fromScale, double fromOpacity, int milliseconds) {
  ScaleTransform scale;
  scale.CenterX(centerX);
  scale.CenterY(centerY);
  scale.ScaleX(fromScale);
  scale.ScaleY(fromScale);
  content.RenderTransform(scale);
  content.Opacity(fromOpacity);
  Storyboard storyboard;
  CubicEase settle;
  settle.EasingMode(EasingMode::EaseOut);
  Animate(storyboard, content, L"Opacity", fromOpacity, 1.0, milliseconds, settle);
  BackEase grow;
  grow.Amplitude(0.05);
  grow.EasingMode(EasingMode::EaseOut);
  Animate(storyboard, scale, L"ScaleX", fromScale, 1.0, milliseconds + 40, grow);
  Animate(storyboard, scale, L"ScaleY", fromScale, 1.0, milliseconds + 40, grow);
  return storyboard;
}

inline Storyboard Slide(const FrameworkElement& surface, const FrameworkElement& content,
                        Pose from, Pose to, int milliseconds, bool spring = false) {
  TranslateTransform offset;
  offset.X(from.x);
  offset.Y(from.y);
  content.RenderTransform(offset);
  surface.Opacity(from.opacity);
  Storyboard storyboard;
  SineEase fade;
  fade.EasingMode(EasingMode::EaseInOut);
  Animate(storyboard, surface, L"Opacity", from.opacity, to.opacity, milliseconds, fade);
  if (spring) {
    BackEase movement;
    movement.Amplitude(0.25);
    movement.EasingMode(EasingMode::EaseOut);
    Animate(storyboard, offset, L"X", from.x, to.x, milliseconds, movement);
    Animate(storyboard, offset, L"Y", from.y, to.y, milliseconds, movement);
  } else {
    CubicEase movement;
    movement.EasingMode(to.opacity == 0 ? EasingMode::EaseIn : EasingMode::EaseOut);
    Animate(storyboard, offset, L"X", from.x, to.x, milliseconds, movement);
    Animate(storyboard, offset, L"Y", from.y, to.y, milliseconds, movement);
  }
  return storyboard;
}

}  // namespace ClassTimeApp::Motion
