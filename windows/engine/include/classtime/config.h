// 用户配置、预设解析、今日覆盖与运行态去重数据。
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "classtime/schedule.h"

namespace classtime {

struct PresentationSettings {
  enum class View { Bar, Ring, ScreenEdge };
  enum class Scope { AllDay, CurrentBlock };
  enum class Edge { Top, Bottom, Left, Right };
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
  int screenEdgeThickness = kDefaultScreenEdgeThickness;  // DIP
  ScreenEdgeColors screenEdgeColors = ScreenEdgeColors::Schedule;
  // 全天按时段分色边框是否在颜色交界处启用平滑渐变；关闭则清晰分段。
  bool screenEdgeGradient = true;
  // 屏幕边框是否在当前进度位置叠加前沿高光，便于分辨进度位置。
  bool screenEdgeHighlight = true;
  // 分节长条沿贴边方向的整体长度（DIP）；顶部/底部控制宽度，左右控制竖向高度。
  // 圆环与屏幕边框不受影响。
  int barLengthDip = kDefaultBarLength;
  int timelineSnapMinutes = 5;
  // 长条背景色，"RRGGBB" 十六进制字符串；空表示跟随系统背景色。
  std::string barBackground;
  // 各时段自定义颜色，"RRGGBB" 十六进制字符串；空表示使用默认色（上课跟随系统强调色，课间 #3FA796，午休 #E0A458）。
  std::string focusColor;
  std::string restColor;
  std::string lunchColor;
  std::string monitorId;  // 显示器设备名；空表示主屏
  int offsetX = 0;        // 相对工作区水平居中位置的 DIP 偏移
  int offsetY = 12;       // 距离工作区边缘的 DIP

  int EffectiveBarLength() const {
    if (barLengthDip != kDefaultBarLength) return barLengthDip;
    return edge == Edge::Left || edge == Edge::Right ? kDefaultVerticalBarLength
                                                    : kDefaultHorizontalBarLength;
  }
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
