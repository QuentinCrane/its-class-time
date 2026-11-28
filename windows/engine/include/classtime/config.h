// 用户配置、预设解析、今日覆盖与运行态去重数据。
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "classtime/schedule.h"

namespace classtime {

struct WidgetColors {
  // RRGGBB. Empty values follow the current Windows theme or the stock phase colors.
  std::string barBackground;
  std::string ringBackground;
  std::string focusColor;
  std::string restColor;
  std::string longRestColor;
  std::string lunchColor;
  std::string trackColor;
  std::string inkColor;

  bool operator==(const WidgetColors& other) const { return Values() == other.Values(); }

 private:
  using ValuesTuple = std::tuple<const std::string&, const std::string&, const std::string&,
                                 const std::string&, const std::string&, const std::string&,
                                 const std::string&, const std::string&>;
  ValuesTuple Values() const {
    return std::tie(barBackground, ringBackground, focusColor, restColor, longRestColor, lunchColor,
                    trackColor, inkColor);
  }
};

struct WidgetStyle {
  enum class BarStyle { Classic, Blocks, FineLine };
  enum class RingStyle { Classic, Instrument, RoundedSquare };
  enum class RingReadout { Remaining, Progress, EndTime };
  enum class ScreenEdgeStyle { Solid, DoubleRail, SoftLine };
  enum class ScreenEdgeColors { Schedule, CurrentState };

  static constexpr int kMinScreenEdgeThickness = 4;
  static constexpr int kMaxScreenEdgeThickness = 24;
  static constexpr int kDefaultScreenEdgeThickness = 10;
  static constexpr int kMinBarLength = 240;
  static constexpr int kMaxBarLength = 1600;
  // 0 保留旧版本按方向设置的默认长度；非零值为用户指定的 DIP。
  static constexpr int kDefaultBarLength = 0;
  static constexpr int kDefaultHorizontalBarLength = 680;
  static constexpr int kDefaultVerticalBarLength = 420;
  static constexpr int kMinBarThickness = 24;
  static constexpr int kMaxBarThickness = 60;
  static constexpr int kMaxBarCorner = 30;
  static constexpr int kMaxBarGap = 16;
  static constexpr int kMinRingDiameter = 112;
  static constexpr int kMaxRingDiameter = 256;
  static constexpr int kDefaultRingDiameter = 156;
  static constexpr int kMinRingStroke = 3;
  static constexpr int kMaxRingStroke = 14;
  static constexpr int kMaxScreenEdgeInset = 32;

  BarStyle barStyle = BarStyle::Classic;
  int barLengthDip = kDefaultBarLength;
  int barThicknessDip = 0;  // 0 uses the default thickness for the current orientation.
  int barCornerDip = -1;    // -1 uses the largest corner allowed by the thickness.
  int barGapDip = 5;
  RingStyle ringStyle = RingStyle::Classic;
  RingReadout ringReadout = RingReadout::Remaining;
  int ringDiameterDip = kDefaultRingDiameter;
  int ringStrokeDip = 7;
  ScreenEdgeStyle screenEdgeStyle = ScreenEdgeStyle::Solid;
  int screenEdgeThickness = kDefaultScreenEdgeThickness;
  int screenEdgeInsetDip = 0;
  ScreenEdgeColors screenEdgeColors = ScreenEdgeColors::Schedule;
  bool screenEdgeGradient = true;
  bool screenEdgeHighlight = true;
  WidgetColors colors;

  int EffectiveBarLength(bool vertical) const {
    if (barLengthDip != kDefaultBarLength) return barLengthDip;
    return vertical ? kDefaultVerticalBarLength : kDefaultHorizontalBarLength;
  }

  int EffectiveBarThickness(bool vertical) const {
    if (barThicknessDip != 0) return barThicknessDip;
    return vertical ? 34 : 32;
  }

  double EffectiveBarCorner(bool vertical) const;
  bool operator==(const WidgetStyle& other) const;

 private:
  auto Values() const {
    return std::tie(barStyle, barLengthDip, barThicknessDip, barCornerDip, barGapDip, ringStyle,
                    ringReadout, ringDiameterDip, ringStrokeDip, screenEdgeStyle, screenEdgeThickness,
                    screenEdgeInsetDip, screenEdgeColors, screenEdgeGradient, screenEdgeHighlight,
                    colors);
  }
};

struct PresentationSettings {
  enum class View { Bar, Ring, ScreenEdge };
  enum class Scope { AllDay, CurrentBlock };
  enum class Edge { Top, Bottom, Left, Right };

  View view = View::Bar;
  Scope scope = Scope::AllDay;
  Edge edge = Edge::Bottom;
  bool visible = true;
  bool autoHideAtEdge = false;
  // 贴边时是否与任务栏/屏幕边缘无缝衔接（0 间距），否则保留悬浮间距。
  bool flushWithEdge = true;
  // 目标显示器存在全屏应用时临时隐藏桌面组件，不影响后台提醒。
  bool hideInFullscreen = false;
  // 是否允许组件覆盖在任务栏上方（使用完整显示器区域而非工作区）。
  bool coverTaskbar = false;
  int timelineSnapMinutes = 5;
  WidgetStyle style;
  std::string monitorId;  // 显示器设备名；空表示主屏
  int offsetX = 0;        // 相对工作区水平居中位置的 DIP 偏移
  int offsetY = 12;       // 距离工作区边缘的 DIP

  bool IsVertical() const { return edge == Edge::Left || edge == Edge::Right; }
};

struct WidgetOutfit {
  static constexpr size_t kMaxSaved = 32;
  static constexpr size_t kMaxNameLength = 36;
  std::string name;
  PresentationSettings::View view = PresentationSettings::View::Bar;
  WidgetStyle style;
};

struct NotificationSettings {
  enum class AlertClockStyle { Digital3D, Mechanical3D, Panel3D, Roman3D };
  enum class AlertClockPosition { TopCenter, TopRight, TopLeft };
  enum class AlertClockSkin { Graphite, Ivory, VintageGold, CyberNeon, RetroMatrix, OceanBlue };

  bool enabled = true;
  bool muted = false;
  bool focusBoundaries = true;
  bool restBoundaries = true;
  bool lunchBoundaries = true;
  // 系统通知之外的可选视觉到点动效（不替代或绕过系统通知权限）。
  bool screenEdgeFlash = false;
  bool dropClockAlert = false;
  AlertClockStyle dropClockStyle = AlertClockStyle::Digital3D;
  AlertClockPosition dropClockPosition = AlertClockPosition::TopCenter;
  int dropClockScalePercent = 100;
  int dropClockHoldSeconds = 4;
  static bool IsDial(AlertClockStyle style) {
    return style == AlertClockStyle::Mechanical3D || style == AlertClockStyle::Roman3D;
  }
  AlertClockSkin dropClockSkin = AlertClockSkin::Graphite;
  // 视觉到点动效自定义颜色，"RRGGBB" 十六进制字符串；空表示跟随所选皮肤或当前时段。
  std::string screenEdgeFlashColor;
  std::string dropClockBodyColor;
  std::string dropClockFaceColor;
  std::string dropClockTextColor;
  std::string dropClockAccentColor;
};

struct DailyOverride {
  std::vector<Segment> segments;
};

struct AppConfig {
  int schemaVersion = 1;
  int revision = 1;  // 每次保存递增，参与通知去重键
  std::string selectedTemplateId = "high-school";
  std::vector<Schedule> templates;
  std::map<std::string, DailyOverride> dailyOverrides;  // YYYY-MM-DD
  PresentationSettings presentation;
  std::vector<WidgetOutfit> widgetOutfits;
  NotificationSettings notifications;
  bool launchAtLogin = false;
};

// —— 预设 ——
std::optional<Schedule> ParsePreset(std::string_view text, std::string* error);
std::string SerializeSchedule(const Schedule& schedule, int indent = 2);

// —— 配置 ——
std::optional<AppConfig> ParseConfig(std::string_view text, std::string* error);
std::string SerializeConfig(const AppConfig& config);

const Schedule* FindTemplate(const AppConfig& config, std::string_view id);
Schedule* FindTemplateMutable(AppConfig& config, std::string_view id);
const Schedule* SelectedTemplate(const AppConfig& config);

// 今日覆盖优先；否则按星期取常规模板；无安排返回空。
std::vector<Segment> EffectiveSegments(const AppConfig& config, const WallClock& now,
                                       bool* fromOverride = nullptr);

bool HasOverride(const AppConfig& config, const std::string& dateKey);
void SetOverride(AppConfig& config, const std::string& dateKey, std::vector<Segment> segments);
void RemoveOverride(AppConfig& config, const std::string& dateKey);

// 清理早于 keepFromDate 的覆盖，避免无限增长。
void PruneOverrides(AppConfig& config, const std::string& keepFromDate);

void BumpRevision(AppConfig& config);

// —— 运行态（通知去重）——
struct RuntimeState {
  std::vector<std::string> firedKeys;  // 最近触发过的边界键
  std::string lastActiveDate;
};

std::string SerializeRuntimeState(const RuntimeState& state);
RuntimeState ParseRuntimeState(std::string_view text);
bool HasFired(const RuntimeState& state, const std::string& key);
void MarkFired(RuntimeState& state, const std::string& key, size_t cap = 64);

}  // namespace classtime
