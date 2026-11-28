#include "classtime/config.h"

#include <algorithm>

#include "classtime/json.h"

namespace classtime {
namespace {

json::Value SegmentToJson(const Segment& segment) {
  json::Value item = json::Value::MakeObject();
  item.Set("id", json::Value::MakeString(segment.id));
  item.Set("kind", json::Value::MakeString(SegmentKindToString(segment.kind)));
  item.Set("label", json::Value::MakeString(segment.label));
  item.Set("start", json::Value::MakeString(segment.start.ToString()));
  item.Set("end", json::Value::MakeString(segment.end.ToString()));
  return item;
}

json::Value SegmentsToJson(const std::vector<Segment>& segments) {
  json::Value array = json::Value::MakeArray();
  for (const auto& segment : segments) array.Append(SegmentToJson(segment));
  return array;
}

bool ParseSegment(const json::Value& item, Segment& out, std::string* error) {
  if (!item.IsObject()) {
    if (error) *error = "时段必须是对象";
    return false;
  }
  const json::Value* id = item.Find("id");
  const json::Value* kind = item.Find("kind");
  const json::Value* label = item.Find("label");
  const json::Value* start = item.Find("start");
  const json::Value* end = item.Find("end");
  if (id == nullptr || kind == nullptr || label == nullptr || start == nullptr || end == nullptr) {
    if (error) *error = "时段缺少 id/kind/label/start/end";
    return false;
  }
  const auto parsedKind = SegmentKindFromString(kind->AsString());
  if (!parsedKind.has_value()) {
    if (error) *error = "未知的时段类型：" + kind->AsString();
    return false;
  }
  const auto parsedStart = TimeOfDay::Parse(start->AsString());
  const auto parsedEnd = TimeOfDay::Parse(end->AsString());
  if (!parsedStart.has_value() || !parsedEnd.has_value()) {
    if (error) *error = "时间必须是 HH:mm（00:00–23:59）";
    return false;
  }
  out.id = id->AsString();
  out.kind = *parsedKind;
  out.label = label->AsString();
  out.start = *parsedStart;
  out.end = *parsedEnd;
  return true;
}

bool ParseSegmentsArray(const json::Value& array, std::vector<Segment>& out, std::string* error) {
  if (!array.IsArray()) {
    if (error) *error = "segments 必须是数组";
    return false;
  }
  out.clear();
  for (const auto& item : array.items()) {
    Segment segment;
    if (!ParseSegment(item, segment, error)) return false;
    out.push_back(std::move(segment));
  }
  return true;
}

const char* ViewToString(PresentationSettings::View view) {
  if (view == PresentationSettings::View::ScreenEdge) return "screen_edge";
  return view == PresentationSettings::View::Ring ? "ring" : "bar";
}

const char* ScopeToString(PresentationSettings::Scope scope) {
  return scope == PresentationSettings::Scope::CurrentBlock ? "current_block" : "all_day";
}

const char* EdgeToString(PresentationSettings::Edge edge) {
  switch (edge) {
    case PresentationSettings::Edge::Top: return "top";
    case PresentationSettings::Edge::Left: return "left";
    case PresentationSettings::Edge::Right: return "right";
    default: return "bottom";
  }
}

PresentationSettings::View ViewFromString(const std::string& text) {
  if (text == "screen_edge") return PresentationSettings::View::ScreenEdge;
  return text == "ring" ? PresentationSettings::View::Ring : PresentationSettings::View::Bar;
}

PresentationSettings::Scope ScopeFromString(const std::string& text) {
  return text == "current_block" ? PresentationSettings::Scope::CurrentBlock
                                 : PresentationSettings::Scope::AllDay;
}

PresentationSettings::Edge EdgeFromString(const std::string& text) {
  if (text == "top") return PresentationSettings::Edge::Top;
  if (text == "left") return PresentationSettings::Edge::Left;
  if (text == "right") return PresentationSettings::Edge::Right;
  return PresentationSettings::Edge::Bottom;
}

const char* AlertClockStyleToString(NotificationSettings::AlertClockStyle style) {
  using Style = NotificationSettings::AlertClockStyle;
  switch (style) {
    case Style::Mechanical3D: return "mechanical_3d";
    case Style::Panel3D: return "panel_3d";
    case Style::Roman3D: return "roman_3d";
    default: return "digital_3d";
  }
}

NotificationSettings::AlertClockStyle AlertClockStyleFromString(const std::string& text) {
  using Style = NotificationSettings::AlertClockStyle;
  if (text == "mechanical_3d") return Style::Mechanical3D;
  if (text == "panel_3d") return Style::Panel3D;
  if (text == "roman_3d") return Style::Roman3D;
  return Style::Digital3D;
}

const char* AlertClockPositionToString(NotificationSettings::AlertClockPosition position) {
  switch (position) {
    case NotificationSettings::AlertClockPosition::TopRight: return "top_right";
    case NotificationSettings::AlertClockPosition::TopLeft: return "top_left";
    default: return "top_center";
  }
}

NotificationSettings::AlertClockPosition AlertClockPositionFromString(const std::string& text) {
  if (text == "top_right") return NotificationSettings::AlertClockPosition::TopRight;
  if (text == "top_left") return NotificationSettings::AlertClockPosition::TopLeft;
  return NotificationSettings::AlertClockPosition::TopCenter;
}

const char* AlertClockSkinToString(NotificationSettings::AlertClockSkin skin) {
  switch (skin) {
    case NotificationSettings::AlertClockSkin::Ivory: return "ivory";
    case NotificationSettings::AlertClockSkin::VintageGold: return "vintage_gold";
    case NotificationSettings::AlertClockSkin::CyberNeon: return "cyber_neon";
    case NotificationSettings::AlertClockSkin::RetroMatrix: return "retro_matrix";
    case NotificationSettings::AlertClockSkin::OceanBlue: return "ocean_blue";
    default: return "graphite";
  }
}

NotificationSettings::AlertClockSkin AlertClockSkinFromString(const std::string& text) {
  if (text == "ivory") return NotificationSettings::AlertClockSkin::Ivory;
  if (text == "vintage_gold") return NotificationSettings::AlertClockSkin::VintageGold;
  if (text == "cyber_neon") return NotificationSettings::AlertClockSkin::CyberNeon;
  if (text == "retro_matrix") return NotificationSettings::AlertClockSkin::RetroMatrix;
  if (text == "ocean_blue") return NotificationSettings::AlertClockSkin::OceanBlue;
  return NotificationSettings::AlertClockSkin::Graphite;
}

// "RRGGBB"（大小写均可）；无效或空字符串都视为“跟随系统”。
bool IsValidHexColor(const std::string& text) {
  if (text.size() != 6) return false;
  for (const char c : text) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    const bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !upper) return false;
  }
  return true;
}

constexpr const char* kBarStyles[] = {"classic", "blocks", "fine_line"};
constexpr const char* kRingStyles[] = {"classic", "instrument", "rounded_square"};
constexpr const char* kRingReadouts[] = {"remaining", "progress", "end_time"};
constexpr const char* kEdgeStyles[] = {"solid", "double_rail", "soft_line"};

template <typename Choice, size_t N>
Choice ReadChoice(const json::Value& object, const char* key, const char* const (&names)[N]) {
  const auto* field = object.Find(key);
  const auto text = field ? field->AsString() : std::string{};
  const auto end = names + N;
  const auto found = std::find(names, end, text);
  return static_cast<Choice>(found == end ? 0 : found - names);
}

int ReadDimension(const json::Value& object, const char* key, int fallback, int minimum,
                  int maximum) {
  const auto* field = object.Find(key);
  const int value = field ? field->AsInt(fallback) : fallback;
  return value == fallback ? fallback : std::clamp(value, minimum, maximum);
}

constexpr std::pair<const char*, std::string WidgetColors::*> kColorFields[] = {
    {"barBackground", &WidgetColors::barBackground}, {"ringBackground", &WidgetColors::ringBackground},
    {"focusColor", &WidgetColors::focusColor}, {"restColor", &WidgetColors::restColor},
    {"longRestColor", &WidgetColors::longRestColor}, {"lunchColor", &WidgetColors::lunchColor},
    {"trackColor", &WidgetColors::trackColor}, {"inkColor", &WidgetColors::inkColor},
};

WidgetStyle ReadWidgetStyle(const json::Value& object) {
  WidgetStyle style;
  style.barStyle = ReadChoice<WidgetStyle::BarStyle>(object, "barStyle", kBarStyles);
  style.barLengthDip = ReadDimension(object, "barLengthDip", style.barLengthDip,
                                     WidgetStyle::kMinBarLength, WidgetStyle::kMaxBarLength);
  style.barThicknessDip = ReadDimension(object, "barThicknessDip", style.barThicknessDip,
                                        WidgetStyle::kMinBarThickness, WidgetStyle::kMaxBarThickness);
  style.barCornerDip = ReadDimension(object, "barCornerDip", style.barCornerDip,
                                     0, WidgetStyle::kMaxBarCorner);
  style.barGapDip = ReadDimension(object, "barGapDip", style.barGapDip, 0, WidgetStyle::kMaxBarGap);
  style.ringStyle = ReadChoice<WidgetStyle::RingStyle>(object, "ringStyle", kRingStyles);
  style.ringReadout = ReadChoice<WidgetStyle::RingReadout>(object, "ringReadout", kRingReadouts);
  style.ringDiameterDip = ReadDimension(object, "ringDiameterDip", style.ringDiameterDip,
                                         WidgetStyle::kMinRingDiameter,
                                         WidgetStyle::kMaxRingDiameter);
  style.ringStrokeDip = ReadDimension(object, "ringStrokeDip", style.ringStrokeDip,
                                      WidgetStyle::kMinRingStroke, WidgetStyle::kMaxRingStroke);
  style.screenEdgeStyle = ReadChoice<WidgetStyle::ScreenEdgeStyle>(object, "screenEdgeStyle",
                                                                    kEdgeStyles);
  style.screenEdgeThickness = ReadDimension(object, "screenEdgeThickness", style.screenEdgeThickness,
                                             WidgetStyle::kMinScreenEdgeThickness,
                                             WidgetStyle::kMaxScreenEdgeThickness);
  style.screenEdgeInsetDip = ReadDimension(object, "screenEdgeInsetDip", style.screenEdgeInsetDip,
                                            0, WidgetStyle::kMaxScreenEdgeInset);
  if (const auto* colors = object.Find("screenEdgeColors")) {
    if (colors->AsString() == "current_state") {
      style.screenEdgeColors = WidgetStyle::ScreenEdgeColors::CurrentState;
    }
  }
  if (const auto* field = object.Find("screenEdgeGradient")) {
    style.screenEdgeGradient = field->AsBool(style.screenEdgeGradient);
  }
  if (const auto* field = object.Find("screenEdgeHighlight")) {
    style.screenEdgeHighlight = field->AsBool(style.screenEdgeHighlight);
  }
  for (const auto& [name, member] : kColorFields) {
    if (const auto* field = object.Find(name)) {
      auto value = field->AsString();
      if (IsValidHexColor(value)) {
        for (char& digit : value) {
          if (digit >= 'a' && digit <= 'f') digit -= 'a' - 'A';
        }
        style.colors.*member = std::move(value);
      }
    }
  }
  return style;
}

void WriteWidgetStyle(json::Value& object, const WidgetStyle& style) {
  object.Set("barStyle", json::Value::MakeString(kBarStyles[static_cast<int>(style.barStyle)]));
  object.Set("barLengthDip", json::Value::MakeNumber(style.barLengthDip));
  object.Set("barThicknessDip", json::Value::MakeNumber(style.barThicknessDip));
  object.Set("barCornerDip", json::Value::MakeNumber(style.barCornerDip));
  object.Set("barGapDip", json::Value::MakeNumber(style.barGapDip));
  object.Set("ringStyle", json::Value::MakeString(kRingStyles[static_cast<int>(style.ringStyle)]));
  object.Set("ringReadout", json::Value::MakeString(kRingReadouts[static_cast<int>(style.ringReadout)]));
  object.Set("ringDiameterDip", json::Value::MakeNumber(style.ringDiameterDip));
  object.Set("ringStrokeDip", json::Value::MakeNumber(style.ringStrokeDip));
  object.Set("screenEdgeStyle", json::Value::MakeString(
      kEdgeStyles[static_cast<int>(style.screenEdgeStyle)]));
  object.Set("screenEdgeThickness", json::Value::MakeNumber(style.screenEdgeThickness));
  object.Set("screenEdgeInsetDip", json::Value::MakeNumber(style.screenEdgeInsetDip));
  object.Set("screenEdgeColors", json::Value::MakeString(
      style.screenEdgeColors == WidgetStyle::ScreenEdgeColors::CurrentState ? "current_state"
                                                                           : "schedule"));
  object.Set("screenEdgeGradient", json::Value::MakeBool(style.screenEdgeGradient));
  object.Set("screenEdgeHighlight", json::Value::MakeBool(style.screenEdgeHighlight));
  for (const auto& [name, member] : kColorFields) {
    object.Set(name, json::Value::MakeString(style.colors.*member));
  }
}

bool ParseScheduleValue(const json::Value& root, Schedule& schedule, std::string* error) {
  if (!root.IsObject()) {
    if (error) *error = "模板根节点必须是对象";
    return false;
  }
  const json::Value* schemaVersion = root.Find("schemaVersion");
  const json::Value* id = root.Find("id");
  const json::Value* name = root.Find("name");
  const json::Value* weekdays = root.Find("activeWeekdays");
  const json::Value* segments = root.Find("segments");
  if (schemaVersion == nullptr || id == nullptr || name == nullptr || weekdays == nullptr ||
      segments == nullptr) {
    if (error) *error = "模板缺少 schemaVersion/id/name/activeWeekdays/segments";
    return false;
  }
  if (schemaVersion->AsInt(0) != 1) {
    if (error) *error = "不支持的 schemaVersion";
    return false;
  }
  schedule = Schedule{};
  schedule.schemaVersion = 1;
  schedule.id = id->AsString();
  schedule.name = name->AsString();
  if (!weekdays->IsArray()) {
    if (error) *error = "activeWeekdays 必须是数组";
    return false;
  }
  for (const auto& item : weekdays->items()) {
    schedule.activeWeekdays.push_back(item.AsInt(0));
  }
  if (!ParseSegmentsArray(*segments, schedule.segments, error)) return false;

  if (!ValidateSchedule(schedule, nullptr)) {
    if (error) *error = "模板内容不合法（时间重叠或字段缺失）";
    return false;
  }
  return true;
}

}  // namespace

double WidgetStyle::EffectiveBarCorner(bool vertical) const {
  const double half = EffectiveBarThickness(vertical) / 2.0;
  return barCornerDip < 0 ? half : std::min(half, static_cast<double>(barCornerDip));
}

bool WidgetStyle::operator==(const WidgetStyle& other) const { return Values() == other.Values(); }

std::optional<Schedule> ParsePreset(std::string_view text, std::string* error) {
  if (error) error->clear();
  const auto parsed = json::Value::Parse(text, error);
  if (!parsed.has_value()) return std::nullopt;
  Schedule schedule;
  if (!ParseScheduleValue(*parsed, schedule, error)) return std::nullopt;
  return schedule;
}

std::string SerializeSchedule(const Schedule& schedule, int indent) {
  json::Value root = json::Value::MakeObject();
  root.Set("schemaVersion", json::Value::MakeNumber(schedule.schemaVersion));
  root.Set("id", json::Value::MakeString(schedule.id));
  root.Set("name", json::Value::MakeString(schedule.name));
  json::Value weekdays = json::Value::MakeArray();
  for (const int weekday : schedule.activeWeekdays) {
    weekdays.Append(json::Value::MakeNumber(weekday));
  }
  root.Set("activeWeekdays", std::move(weekdays));
  root.Set("segments", SegmentsToJson(schedule.segments));
  return root.Dump(indent);
}

std::optional<AppConfig> ParseConfig(std::string_view text, std::string* error) {
  if (error) error->clear();
  const auto parsed = json::Value::Parse(text, error);
  if (!parsed.has_value()) return std::nullopt;
  const json::Value& root = *parsed;
  if (!root.IsObject()) {
    if (error) *error = "配置根节点必须是对象";
    return std::nullopt;
  }

  AppConfig config;
  if (const json::Value* version = root.Find("schemaVersion")) {
    // 没有版本号的早期配置按版本 1 兼容；显式版本必须准确匹配。
    // 不能把字符串、null 或小数回退/截断为 1，否则重写配置时会丢失未知字段。
    if (!version->IsNumber() || version->AsNumber() != 1.0) {
      if (error) *error = "不支持的配置 schemaVersion";
      return std::nullopt;
    }
  }
  if (const json::Value* revision = root.Find("revision")) {
    config.revision = std::max(1, revision->AsInt(1));
  }
  if (const json::Value* selected = root.Find("selectedTemplateId")) {
    config.selectedTemplateId = selected->AsString();
  }

  if (const json::Value* templates = root.Find("templates")) {
    if (!templates->IsArray()) {
      if (error) *error = "templates 必须是数组";
      return std::nullopt;
    }
    for (const auto& item : templates->items()) {
      Schedule schedule;
      if (!ParseScheduleValue(item, schedule, error)) return std::nullopt;
      config.templates.push_back(std::move(schedule));
    }
  }

  if (const json::Value* overrides = root.Find("dailyOverrides")) {
    if (overrides->IsObject()) {
      for (const auto& member : overrides->members()) {
        const json::Value* segments = member.second.Find("segments");
        if (segments == nullptr) continue;
        DailyOverride entry;
        if (!ParseSegmentsArray(*segments, entry.segments, error)) return std::nullopt;
        config.dailyOverrides[member.first] = std::move(entry);
      }
    }
  }

  if (const json::Value* presentation = root.Find("presentation")) {
    if (const json::Value* view = presentation->Find("view")) {
      config.presentation.view = ViewFromString(view->AsString());
    }
    if (const json::Value* scope = presentation->Find("scope")) {
      config.presentation.scope = ScopeFromString(scope->AsString());
    }
    if (const json::Value* edge = presentation->Find("edge")) {
      config.presentation.edge = EdgeFromString(edge->AsString());
    }
    if (const json::Value* visible = presentation->Find("visible")) {
      config.presentation.visible = visible->AsBool(true);
    }
    if (const json::Value* autoHide = presentation->Find("autoHideAtEdge")) {
      config.presentation.autoHideAtEdge = autoHide->AsBool(false);
    }
    if (const json::Value* flush = presentation->Find("flushWithEdge")) {
      config.presentation.flushWithEdge = flush->AsBool(true);
    }
    if (const json::Value* hideFullscreen = presentation->Find("hideInFullscreen")) {
      config.presentation.hideInFullscreen = hideFullscreen->AsBool(false);
    }
    if (const json::Value* coverTaskbar = presentation->Find("coverTaskbar")) {
      config.presentation.coverTaskbar = coverTaskbar->AsBool(false);
    }
    config.presentation.style = ReadWidgetStyle(*presentation);
    if (const auto* snap = presentation->Find("timelineSnapMinutes")) {
      const int value = snap->AsInt(5);
      if (value == 1 || value == 5 || value == 15) config.presentation.timelineSnapMinutes = value;
    }
    if (const json::Value* monitor = presentation->Find("monitorId")) {
      config.presentation.monitorId = monitor->AsString();
    }
    if (const json::Value* offsetX = presentation->Find("offsetX")) {
      config.presentation.offsetX = offsetX->AsInt(0);
    }
    if (const json::Value* offsetY = presentation->Find("offsetY")) {
      config.presentation.offsetY = offsetY->AsInt(12);
    }
  }

  if (const auto* outfits = root.Find("widgetOutfits"); outfits && outfits->IsArray()) {
    for (const auto& item : outfits->items()) {
      if (config.widgetOutfits.size() == WidgetOutfit::kMaxSaved) break;
      const auto* label = item.Find("name");
      const auto* view = item.Find("view");
      const auto* style = item.Find("style");
      if (!label || !view || !view->IsString() || !style || !style->IsObject()) continue;
      auto name = label->AsString();
      if (name.empty() || name.size() > 4 * WidgetOutfit::kMaxNameLength ||
          name.find_first_not_of(" \t\r\n") == std::string::npos ||
          name.find_first_of("\r\n\t") != std::string::npos) continue;
      const auto match = [&](const WidgetOutfit& outfit) { return outfit.name == name; };
      if (std::any_of(config.widgetOutfits.begin(), config.widgetOutfits.end(), match)) continue;
      config.widgetOutfits.push_back({std::move(name), ViewFromString(view->AsString()),
                                      ReadWidgetStyle(*style)});
    }
  }

  if (const json::Value* notifications = root.Find("notifications")) {
    if (const json::Value* value = notifications->Find("enabled")) {
      config.notifications.enabled = value->AsBool(true);
    }
    if (const json::Value* value = notifications->Find("muted")) {
      config.notifications.muted = value->AsBool(false);
    }
    if (const json::Value* value = notifications->Find("focusBoundaries")) {
      config.notifications.focusBoundaries = value->AsBool(true);
    }
    if (const json::Value* value = notifications->Find("restBoundaries")) {
      config.notifications.restBoundaries = value->AsBool(true);
    }
    if (const json::Value* value = notifications->Find("lunchBoundaries")) {
      config.notifications.lunchBoundaries = value->AsBool(true);
    }
    if (const json::Value* value = notifications->Find("screenEdgeFlash")) {
      config.notifications.screenEdgeFlash = value->AsBool(false);
    }
    if (const json::Value* value = notifications->Find("dropClockAlert")) {
      config.notifications.dropClockAlert = value->AsBool(false);
    }
    if (const json::Value* value = notifications->Find("dropClockStyle")) {
      config.notifications.dropClockStyle = AlertClockStyleFromString(value->AsString());
    }
    if (const auto* scale = notifications->Find("dropClockScalePercent")) {
      const double value = scale->AsNumber(100);
      if (value == 75 || value == 125) config.notifications.dropClockScalePercent = static_cast<int>(value);
    }
    if (const auto* hold = notifications->Find("dropClockHoldSeconds")) {
      const double value = hold->AsNumber(4);
      if (value == 2 || value == 8) config.notifications.dropClockHoldSeconds = static_cast<int>(value);
    }
    if (const json::Value* value = notifications->Find("dropClockPosition")) {
      config.notifications.dropClockPosition = AlertClockPositionFromString(value->AsString());
    }
    if (const json::Value* value = notifications->Find("dropClockSkin")) {
      config.notifications.dropClockSkin = AlertClockSkinFromString(value->AsString());
    }
    if (const json::Value* value = notifications->Find("screenEdgeFlashColor")) {
      const auto hex = value->AsString();
      if (IsValidHexColor(hex)) config.notifications.screenEdgeFlashColor = hex;
    }
    if (const json::Value* value = notifications->Find("dropClockBodyColor")) {
      const auto hex = value->AsString();
      if (IsValidHexColor(hex)) config.notifications.dropClockBodyColor = hex;
    }
    if (const json::Value* value = notifications->Find("dropClockFaceColor")) {
      const auto hex = value->AsString();
      if (IsValidHexColor(hex)) config.notifications.dropClockFaceColor = hex;
    }
    if (const json::Value* value = notifications->Find("dropClockTextColor")) {
      const auto hex = value->AsString();
      if (IsValidHexColor(hex)) config.notifications.dropClockTextColor = hex;
    }
    if (const json::Value* value = notifications->Find("dropClockAccentColor")) {
      const auto hex = value->AsString();
      if (IsValidHexColor(hex)) config.notifications.dropClockAccentColor = hex;
    }
  }

  if (const json::Value* launch = root.Find("launchAtLogin")) {
    config.launchAtLogin = launch->AsBool(false);
  }

  if (config.templates.empty()) {
    if (error) *error = "配置中没有任何作息模板";
    return std::nullopt;
  }
  if (FindTemplate(config, config.selectedTemplateId) == nullptr) {
    config.selectedTemplateId = config.templates.front().id;
  }
  return config;
}

std::string SerializeConfig(const AppConfig& config) {
  json::Value root = json::Value::MakeObject();
  root.Set("schemaVersion", json::Value::MakeNumber(config.schemaVersion));
  root.Set("revision", json::Value::MakeNumber(config.revision));
  root.Set("selectedTemplateId", json::Value::MakeString(config.selectedTemplateId));

  json::Value templates = json::Value::MakeArray();
  for (const auto& schedule : config.templates) {
    const auto parsed = json::Value::Parse(SerializeSchedule(schedule, 0));
    if (parsed.has_value()) templates.Append(*parsed);
  }
  root.Set("templates", std::move(templates));

  json::Value overrides = json::Value::MakeObject();
  for (const auto& entry : config.dailyOverrides) {
    json::Value value = json::Value::MakeObject();
    value.Set("segments", SegmentsToJson(entry.second.segments));
    overrides.Set(entry.first, std::move(value));
  }
  root.Set("dailyOverrides", std::move(overrides));

  json::Value presentation = json::Value::MakeObject();
  presentation.Set("view", json::Value::MakeString(ViewToString(config.presentation.view)));
  presentation.Set("scope", json::Value::MakeString(ScopeToString(config.presentation.scope)));
  presentation.Set("edge", json::Value::MakeString(EdgeToString(config.presentation.edge)));
  presentation.Set("visible", json::Value::MakeBool(config.presentation.visible));
  presentation.Set("autoHideAtEdge", json::Value::MakeBool(config.presentation.autoHideAtEdge));
  presentation.Set("flushWithEdge", json::Value::MakeBool(config.presentation.flushWithEdge));
  presentation.Set("hideInFullscreen", json::Value::MakeBool(config.presentation.hideInFullscreen));
  presentation.Set("coverTaskbar", json::Value::MakeBool(config.presentation.coverTaskbar));
  WriteWidgetStyle(presentation, config.presentation.style);
  presentation.Set("timelineSnapMinutes", json::Value::MakeNumber(config.presentation.timelineSnapMinutes));
  presentation.Set("monitorId", json::Value::MakeString(config.presentation.monitorId));
  presentation.Set("offsetX", json::Value::MakeNumber(config.presentation.offsetX));
  presentation.Set("offsetY", json::Value::MakeNumber(config.presentation.offsetY));
  root.Set("presentation", std::move(presentation));

  if (!config.widgetOutfits.empty()) {
    auto outfits = json::Value::MakeArray();
    for (const auto& outfit : config.widgetOutfits) {
      auto item = json::Value::MakeObject();
      item.Set("name", json::Value::MakeString(outfit.name));
      item.Set("view", json::Value::MakeString(ViewToString(outfit.view)));
      auto style = json::Value::MakeObject();
      WriteWidgetStyle(style, outfit.style);
      item.Set("style", std::move(style));
      outfits.Append(std::move(item));
    }
    root.Set("widgetOutfits", std::move(outfits));
  }

  json::Value notifications = json::Value::MakeObject();
  notifications.Set("enabled", json::Value::MakeBool(config.notifications.enabled));
  notifications.Set("muted", json::Value::MakeBool(config.notifications.muted));
  notifications.Set("focusBoundaries", json::Value::MakeBool(config.notifications.focusBoundaries));
  notifications.Set("restBoundaries", json::Value::MakeBool(config.notifications.restBoundaries));
  notifications.Set("lunchBoundaries", json::Value::MakeBool(config.notifications.lunchBoundaries));
  notifications.Set("screenEdgeFlash", json::Value::MakeBool(config.notifications.screenEdgeFlash));
  notifications.Set("dropClockAlert", json::Value::MakeBool(config.notifications.dropClockAlert));
  notifications.Set("dropClockScalePercent", json::Value::MakeNumber(config.notifications.dropClockScalePercent));
  notifications.Set("dropClockHoldSeconds", json::Value::MakeNumber(config.notifications.dropClockHoldSeconds));
  notifications.Set("dropClockStyle", json::Value::MakeString(
      AlertClockStyleToString(config.notifications.dropClockStyle)));
  notifications.Set("dropClockPosition", json::Value::MakeString(
      AlertClockPositionToString(config.notifications.dropClockPosition)));
  notifications.Set("dropClockSkin", json::Value::MakeString(
      AlertClockSkinToString(config.notifications.dropClockSkin)));
  notifications.Set("screenEdgeFlashColor",
                    json::Value::MakeString(config.notifications.screenEdgeFlashColor));
  notifications.Set("dropClockBodyColor",
                    json::Value::MakeString(config.notifications.dropClockBodyColor));
  notifications.Set("dropClockFaceColor",
                    json::Value::MakeString(config.notifications.dropClockFaceColor));
  notifications.Set("dropClockTextColor",
                    json::Value::MakeString(config.notifications.dropClockTextColor));
  notifications.Set("dropClockAccentColor",
                    json::Value::MakeString(config.notifications.dropClockAccentColor));
  root.Set("notifications", std::move(notifications));

  root.Set("launchAtLogin", json::Value::MakeBool(config.launchAtLogin));
  return root.Dump(2);
}

const Schedule* FindTemplate(const AppConfig& config, std::string_view id) {
  for (const auto& schedule : config.templates) {
    if (schedule.id == id) return &schedule;
  }
  return nullptr;
}

Schedule* FindTemplateMutable(AppConfig& config, std::string_view id) {
  for (auto& schedule : config.templates) {
    if (schedule.id == id) return &schedule;
  }
  return nullptr;
}

const Schedule* SelectedTemplate(const AppConfig& config) {
  return FindTemplate(config, config.selectedTemplateId);
}

std::vector<Segment> EffectiveSegments(const AppConfig& config, const WallClock& now,
                                       bool* fromOverride) {
  if (fromOverride != nullptr) *fromOverride = false;
  const auto override = config.dailyOverrides.find(now.DateKey());
  if (override != config.dailyOverrides.end()) {
    if (fromOverride != nullptr) *fromOverride = true;
    return override->second.segments;
  }
  const Schedule* schedule = SelectedTemplate(config);
  if (schedule == nullptr) return {};
  if (!schedule->IsActiveOn(now.Weekday())) return {};
  return schedule->segments;
}

bool HasOverride(const AppConfig& config, const std::string& dateKey) {
  return config.dailyOverrides.find(dateKey) != config.dailyOverrides.end();
}

void SetOverride(AppConfig& config, const std::string& dateKey, std::vector<Segment> segments) {
  DailyOverride entry;
  entry.segments = std::move(segments);
  config.dailyOverrides[dateKey] = std::move(entry);
}

void RemoveOverride(AppConfig& config, const std::string& dateKey) {
  config.dailyOverrides.erase(dateKey);
}

void PruneOverrides(AppConfig& config, const std::string& keepFromDate) {
  for (auto it = config.dailyOverrides.begin(); it != config.dailyOverrides.end();) {
    if (it->first < keepFromDate) {
      it = config.dailyOverrides.erase(it);
    } else {
      ++it;
    }
  }
}

void BumpRevision(AppConfig& config) {
  if (config.revision < 2147483000) {
    ++config.revision;
  } else {
    config.revision = 1;
  }
}

std::string SerializeRuntimeState(const RuntimeState& state) {
  json::Value root = json::Value::MakeObject();
  json::Value keys = json::Value::MakeArray();
  for (const auto& key : state.firedKeys) keys.Append(json::Value::MakeString(key));
  root.Set("firedKeys", std::move(keys));
  root.Set("lastActiveDate", json::Value::MakeString(state.lastActiveDate));
  return root.Dump(2);
}

RuntimeState ParseRuntimeState(std::string_view text) {
  RuntimeState state;
  const auto parsed = json::Value::Parse(text, nullptr);
  if (!parsed.has_value() || !parsed->IsObject()) return state;
  if (const json::Value* keys = parsed->Find("firedKeys")) {
    if (keys->IsArray()) {
      for (const auto& item : keys->items()) {
        if (item.IsString()) state.firedKeys.push_back(item.AsString());
      }
    }
  }
  if (const json::Value* date = parsed->Find("lastActiveDate")) {
    state.lastActiveDate = date->AsString();
  }
  return state;
}

bool HasFired(const RuntimeState& state, const std::string& key) {
  return std::find(state.firedKeys.begin(), state.firedKeys.end(), key) != state.firedKeys.end();
}

void MarkFired(RuntimeState& state, const std::string& key, size_t cap) {
  if (!HasFired(state, key)) state.firedKeys.push_back(key);
  while (state.firedKeys.size() > cap) {
    state.firedKeys.erase(state.firedKeys.begin());
  }
}

}  // namespace classtime
