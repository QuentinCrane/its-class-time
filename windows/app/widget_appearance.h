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

inline Palette ResolvePalette(const classtime::PresentationSettings& presentation) {
  Palette palette = CurrentPalette();
  if (!presentation.focusColor.empty()) palette.accent = ParseHexColor(presentation.focusColor);
  if (!presentation.restColor.empty()) palette.rest = ParseHexColor(presentation.restColor);
  if (!presentation.lunchColor.empty()) palette.lunch = ParseHexColor(presentation.lunchColor);
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

inline Color ScreenEdgeHighlightColor(double intensity) {
  const auto alpha = static_cast<uint8_t>(std::lround(std::clamp(intensity, 0.0, 1.0) * 255.0));
  return winrt::Windows::UI::ColorHelper::FromArgb(alpha, 255, 255, 255);
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

// —— 3D 时钟皮肤与自定义配色解析 ——

inline Color MixRgb(Color a, Color b, double t) {
  const double u = std::clamp(t, 0.0, 1.0);
  const auto mix = [u](uint8_t ca, uint8_t cb) {
    return static_cast<uint8_t>(std::clamp<long>(
        std::lround(ca + (static_cast<double>(cb) - ca) * u), 0, 255));
  };
  return winrt::Windows::UI::ColorHelper::FromArgb(
      255, mix(a.R, b.R), mix(a.G, b.G), mix(a.B, b.B));
}

inline bool IsLightColor(Color c) {
  return (299 * c.R + 587 * c.G + 114 * c.B) >= 145000;
}

struct ClockSkinPreset {
  Color body{};
  Color face{};
  Color bezel0{};
  Color bezel1{};
  Color bezel2{};
  Color faceText{};
  Color bodyText{};
  Color sideWall{};
};

inline ClockSkinPreset GetClockSkinPreset(classtime::NotificationSettings::AlertClockSkin skin) {
  using Skin = classtime::NotificationSettings::AlertClockSkin;
  using winrt::Windows::UI::ColorHelper;
  switch (skin) {
    case Skin::Ivory:
      return {
          ColorHelper::FromArgb(255, 237, 233, 224),
          ColorHelper::FromArgb(255, 252, 251, 248),
          ColorHelper::FromArgb(255, 247, 244, 236),
          ColorHelper::FromArgb(255, 200, 193, 178),
          ColorHelper::FromArgb(255, 141, 134, 120),
          ColorHelper::FromArgb(255, 32, 37, 48),
          ColorHelper::FromArgb(255, 30, 36, 48),
          ColorHelper::FromArgb(255, 196, 190, 178),
      };
    case Skin::VintageGold:
      return {
          ColorHelper::FromArgb(255, 35, 27, 20),
          ColorHelper::FromArgb(255, 243, 231, 204),
          ColorHelper::FromArgb(255, 230, 198, 135),
          ColorHelper::FromArgb(255, 176, 136, 68),
          ColorHelper::FromArgb(255, 92, 67, 28),
          ColorHelper::FromArgb(255, 43, 32, 22),
          ColorHelper::FromArgb(255, 245, 232, 206),
          ColorHelper::FromArgb(255, 142, 110, 59),
      };
    case Skin::CyberNeon:
      return {
          ColorHelper::FromArgb(255, 14, 17, 34),
          ColorHelper::FromArgb(255, 22, 27, 56),
          ColorHelper::FromArgb(255, 112, 232, 255),
          ColorHelper::FromArgb(255, 59, 91, 219),
          ColorHelper::FromArgb(255, 25, 30, 64),
          ColorHelper::FromArgb(255, 124, 247, 255),
          ColorHelper::FromArgb(255, 230, 249, 255),
          ColorHelper::FromArgb(255, 52, 75, 138),
      };
    case Skin::RetroMatrix:
      return {
          ColorHelper::FromArgb(255, 18, 27, 22),
          ColorHelper::FromArgb(255, 11, 21, 16),
          ColorHelper::FromArgb(255, 105, 184, 138),
          ColorHelper::FromArgb(255, 45, 92, 67),
          ColorHelper::FromArgb(255, 21, 41, 30),
          ColorHelper::FromArgb(255, 93, 245, 155),
          ColorHelper::FromArgb(255, 212, 255, 228),
          ColorHelper::FromArgb(255, 44, 84, 62),
      };
    case Skin::OceanBlue:
      return {
          ColorHelper::FromArgb(255, 16, 34, 58),
          ColorHelper::FromArgb(255, 25, 55, 95),
          ColorHelper::FromArgb(255, 199, 220, 245),
          ColorHelper::FromArgb(255, 91, 131, 180),
          ColorHelper::FromArgb(255, 32, 58, 94),
          ColorHelper::FromArgb(255, 240, 246, 255),
          ColorHelper::FromArgb(255, 240, 246, 255),
          ColorHelper::FromArgb(255, 56, 98, 150),
      };
    default:  // Graphite
      return {
          ColorHelper::FromArgb(255, 22, 26, 35),
          ColorHelper::FromArgb(255, 36, 42, 54),
          ColorHelper::FromArgb(255, 195, 204, 218),
          ColorHelper::FromArgb(255, 110, 120, 138),
          ColorHelper::FromArgb(255, 54, 61, 74),
          ColorHelper::FromArgb(255, 244, 248, 255),
          ColorHelper::FromArgb(255, 248, 250, 255),
          ColorHelper::FromArgb(255, 72, 82, 102),
      };
  }
}

struct ClockAppearance {
  Color accent{};
  Color flash{};
  Color bodyTop{};
  Color bodyMid{};
  Color bodyBottom{};
  Color rim{};
  float glossAlphaTop = 0.14f;
  float glossAlphaMid = 0.03f;
  Color tileTop{};
  Color tileBottom{};
  Color tileBorder{};
  Color hinge{};
  Color bezel0{};
  Color bezel1{};
  Color bezel2{};
  Color dial0{};
  Color dial1{};
  Color dial2{};
  Color digit{};
  Color tickMajor{};
  Color tickMinor{};
  Color numeral{};
  Color digitalSmall{};
  Color handMain{};
  Color title{};
  Color footer{};
  Color badgeText{};
  Color plateBg{};
  Color plateRim{};
  Color sideWall{};
  bool lightBody = false;
  bool lightFace = false;
};

inline ClockAppearance ResolveClockAppearance(const classtime::NotificationSettings& notify,
                                              Color phaseAccent) {
  using winrt::Windows::UI::ColorHelper;
  const ClockSkinPreset preset = GetClockSkinPreset(notify.dropClockSkin);
  const bool customBody = !notify.dropClockBodyColor.empty();
  const bool customFace = !notify.dropClockFaceColor.empty();
  const bool customText = !notify.dropClockTextColor.empty();

  const Color body = customBody ? ParseHexColor(notify.dropClockBodyColor) : preset.body;
  const Color face = customFace ? ParseHexColor(notify.dropClockFaceColor) : preset.face;
  const bool lightBody = IsLightColor(body);
  const bool lightFace = IsLightColor(face);

  const Color white = ColorHelper::FromArgb(255, 255, 255, 255);
  const Color black = ColorHelper::FromArgb(255, 10, 12, 16);

  ClockAppearance app;
  app.lightBody = lightBody;
  app.lightFace = lightFace;
  app.accent = !notify.dropClockAccentColor.empty()
                   ? ParseHexColor(notify.dropClockAccentColor)
                   : phaseAccent;
  app.flash = !notify.screenEdgeFlashColor.empty()
                  ? ParseHexColor(notify.screenEdgeFlashColor)
                  : phaseAccent;

  app.bodyTop = WithAlpha(MixRgb(body, white, lightBody ? 0.24 : 0.14), 252);
  app.bodyMid = WithAlpha(body, 253);
  app.bodyBottom = WithAlpha(MixRgb(body, black, lightBody ? 0.14 : 0.30), 253);
  app.rim = lightBody ? ColorHelper::FromArgb(185, 255, 255, 255)
                      : ColorHelper::FromArgb(56, 255, 255, 255);
  app.glossAlphaTop = lightBody ? 0.34f : 0.14f;
  app.glossAlphaMid = lightBody ? 0.09f : 0.03f;

  // 翻页牌面与机械表盘均围绕有效 face 色彩衍生
  app.tileTop = MixRgb(face, white, lightFace ? 0.28 : 0.12);
  app.tileBottom = MixRgb(face, black, lightFace ? 0.07 : 0.22);
  app.tileBorder = lightFace ? ColorHelper::FromArgb(44, 24, 30, 42)
                             : ColorHelper::FromArgb(40, 255, 255, 255);
  app.hinge = lightFace ? ColorHelper::FromArgb(72, 24, 30, 42)
                        : ColorHelper::FromArgb(215, 8, 10, 14);
  app.dial0 = MixRgb(face, white, lightFace ? 0.30 : 0.16);
  app.dial1 = face;
  app.dial2 = MixRgb(face, black, lightFace ? 0.10 : 0.34);

  if (!customBody) {
    app.bezel0 = preset.bezel0;
    app.bezel1 = preset.bezel1;
    app.bezel2 = preset.bezel2;
    app.sideWall = preset.sideWall;
  } else if (lightBody) {
    app.bezel0 = MixRgb(body, white, 0.45);
    app.bezel1 = MixRgb(body, ColorHelper::FromArgb(255, 178, 186, 200), 0.45);
    app.bezel2 = MixRgb(body, ColorHelper::FromArgb(255, 118, 128, 146), 0.55);
    app.sideWall = MixRgb(body, ColorHelper::FromArgb(255, 154, 166, 184), 0.35);
  } else {
    app.bezel0 = MixRgb(body, ColorHelper::FromArgb(255, 210, 218, 232), 0.55);
    app.bezel1 = MixRgb(body, ColorHelper::FromArgb(255, 120, 130, 148), 0.45);
    app.bezel2 = MixRgb(body, ColorHelper::FromArgb(255, 42, 48, 60), 0.65);
    app.sideWall = MixRgb(body, ColorHelper::FromArgb(255, 118, 132, 158), 0.42);
  }

  app.plateBg = WithAlpha(MixRgb(body, white, lightBody ? 0.18 : 0.04), 246);
  app.plateRim = lightBody ? ColorHelper::FromArgb(46, 24, 30, 44)
                           : ColorHelper::FromArgb(52, 255, 255, 255);

  const Color faceInk = customText
                            ? ParseHexColor(notify.dropClockTextColor)
                            : (!customFace
                                   ? preset.faceText
                                   : (lightFace ? ColorHelper::FromArgb(255, 22, 27, 38)
                                                : ColorHelper::FromArgb(255, 244, 248, 255)));
  const Color bodyInk = customText
                            ? ParseHexColor(notify.dropClockTextColor)
                            : (!customBody
                                   ? preset.bodyText
                                   : (lightBody ? ColorHelper::FromArgb(255, 20, 25, 36)
                                                : ColorHelper::FromArgb(255, 248, 250, 255)));

  app.digit = faceInk;
  app.tickMajor = WithAlpha(faceInk, 235);
  app.tickMinor = WithAlpha(faceInk, 115);
  app.numeral = WithAlpha(faceInk, 238);
  app.digitalSmall = WithAlpha(faceInk, 204);
  app.handMain = faceInk;

  app.title = WithAlpha(bodyInk, 250);
  app.badgeText = WithAlpha(bodyInk, 245);
  app.footer = WithAlpha(MixRgb(bodyInk, body, 0.25), 224);
  return app;
}

}  // namespace ClassTimeApp::WidgetAppearance
