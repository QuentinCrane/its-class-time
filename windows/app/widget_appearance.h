#pragma once

#include <winrt/Windows.UI.ViewManagement.h>
#include "widget_state.h"

namespace ClassTimeApp::WidgetAppearance {

using Edge = classtime::PresentationSettings::Edge;
using winrt::Microsoft::UI::Xaml::CornerRadius;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::UI::Color;

struct Palette {
  Color background;
  Color foreground;
  Color accent;
  Color rest;
  Color lunch;
};

inline Color WithAlpha(Color color, uint8_t alpha) {
  color.A = alpha;
  return color;
}

inline Palette CurrentPalette() {
  using namespace winrt::Windows::UI;
  using namespace winrt::Windows::UI::ViewManagement;
  Palette palette;
  try {
    UISettings settings;
    palette.background = settings.GetColorValue(UIColorType::Background);
    palette.foreground = settings.GetColorValue(UIColorType::Foreground);
    palette.accent = settings.GetColorValue(UIColorType::Accent);
  } catch (...) {
    palette.background = ColorHelper::FromArgb(255, 250, 250, 252);
    palette.foreground = ColorHelper::FromArgb(255, 28, 30, 36);
    palette.accent = ColorHelper::FromArgb(255, 79, 107, 237);
  }
  palette.rest = ColorHelper::FromArgb(255, 63, 167, 150);
  palette.lunch = ColorHelper::FromArgb(255, 224, 164, 88);
  return palette;
}

inline Color KindColor(const Palette& palette, classtime::SegmentKind kind) {
  switch (kind) {
    case classtime::SegmentKind::Focus: return palette.accent;
    case classtime::SegmentKind::Break:
    case classtime::SegmentKind::LongBreak: return palette.rest;
    case classtime::SegmentKind::Lunch: return palette.lunch;
  }
  return palette.accent;
}

inline Color ScreenEdgeRunColor(const Palette& palette,
                                std::optional<classtime::SegmentKind> kind, bool elapsed) {
  if (!kind) return winrt::Windows::UI::ColorHelper::FromArgb(elapsed ? 172 : 80, 100, 117, 137);
  auto color = KindColor(palette, *kind);
  color.A = elapsed ? 255 : 112;
  return color;
}

struct ScreenEdgeColorStop {
  double position = 0;
  Color color{};
};

inline std::vector<ScreenEdgeColorStop> ScreenEdgeColorStops(
    const std::vector<ScreenEdgeRun>& runs, const Palette& palette) {
  std::vector<ScreenEdgeColorStop> colors;
  for (const auto& stop : BuildScreenEdgeBlendStops(runs)) {
    colors.push_back({stop.position, ScreenEdgeRunColor(palette, stop.kind, stop.elapsed)});
  }
  return colors;
}

inline Color ScreenEdgeColorAt(const std::vector<ScreenEdgeColorStop>& stops, double position) {
  const auto upper = std::lower_bound(stops.begin(), stops.end(), position,
      [](const ScreenEdgeColorStop& stop, double value) { return stop.position < value; });
  if (upper == stops.begin()) return upper->color;
  if (upper == stops.end()) return stops.back().color;
  const auto& before = *(upper - 1);
  const double distance = upper->position - before.position;
  const double fraction = distance > 0 ? (position - before.position) / distance : 1;
  const auto mix = [fraction](uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(std::lround(a + (static_cast<double>(b) - a) * fraction));
  };
  return winrt::Windows::UI::ColorHelper::FromArgb(
      mix(before.color.A, upper->color.A), mix(before.color.R, upper->color.R),
      mix(before.color.G, upper->color.G), mix(before.color.B, upper->color.B));
}

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

// —— 自定义颜色（"RRGGBB" 十六进制，配置层已校验） ——

inline Color ParseHexColor(const std::string& hex) {
  const auto digit = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
  };
  return winrt::Windows::UI::ColorHelper::FromArgb(
      255, static_cast<uint8_t>(digit(hex[0]) * 16 + digit(hex[1])),
      static_cast<uint8_t>(digit(hex[2]) * 16 + digit(hex[3])),
      static_cast<uint8_t>(digit(hex[4]) * 16 + digit(hex[5])));
}

inline std::string ColorToHex(Color color) {
  const char* digits = "0123456789ABCDEF";
  std::string hex;
  hex += digits[color.R >> 4];
  hex += digits[color.R & 15];
  hex += digits[color.G >> 4];
  hex += digits[color.G & 15];
  hex += digits[color.B >> 4];
  hex += digits[color.B & 15];
  return hex;
}

}  // namespace ClassTimeApp::WidgetAppearance
