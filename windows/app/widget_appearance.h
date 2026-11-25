#pragma once

#include <winrt/Windows.UI.ViewManagement.h>

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
