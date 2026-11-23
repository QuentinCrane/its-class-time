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
                    const EasingFunctionBase& easing, bool changesLayout = false,
                    int delayMilliseconds = 0) {
  DoubleAnimation animation;
  animation.From(from);
  animation.To(to);
  animation.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(milliseconds)));
  animation.EasingFunction(easing);
  animation.EnableDependentAnimation(changesLayout);
  if (delayMilliseconds > 0) {
    animation.BeginTime(winrt::box_value(winrt::Windows::Foundation::TimeSpan(std::chrono::milliseconds(delayMilliseconds)))
                            .as<winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::TimeSpan>>());
  }
  Storyboard::SetTarget(animation, target);
  Storyboard::SetTargetProperty(animation, property);
  storyboard.Children().Append(animation);
}

inline double Mix(double from, double to, double progress) {
  return from + (to - from) * progress;
}

inline double SmoothStep(double progress) {
  const double t = std::clamp(progress, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

inline double Glide(double t, double overshoot) {
  const double recoil = std::sin(3.141592653589793 * SmoothStep((t - 0.74) / 0.26));
  return SmoothStep(t / 0.85) + overshoot * recoil;
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
