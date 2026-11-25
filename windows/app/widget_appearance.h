#pragma once

namespace ClassTimeApp::WidgetAppearance {

using Edge = classtime::PresentationSettings::Edge;
using winrt::Microsoft::UI::Xaml::CornerRadius;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::UI::Color;

inline CornerRadius DockCorners(double radius, Edge edge, bool flush, double edgeRadius = 4) {
  if (!flush) return {radius, radius, radius, radius};
  switch (edge) {
    case Edge::Top: return {edgeRadius, edgeRadius, radius, radius};
    case Edge::Left: return {edgeRadius, radius, radius, edgeRadius};
    case Edge::Right: return {radius, edgeRadius, edgeRadius, radius};
    default: return {radius, radius, edgeRadius, edgeRadius};
  }
}

inline Brush Surface(Color background) { return SolidColorBrush(background); }

}  // namespace ClassTimeApp::WidgetAppearance
