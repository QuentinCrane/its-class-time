// 作息引擎对等测试：直接消费 shared/ 下的预设与测试向量。
// 运行：engine_tests <仓库根目录>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "classtime/config.h"
#include "classtime/edit_ops.h"
#include "classtime/json.h"
#include "classtime/schedule.h"
#include "classtime/time_engine.h"
#include "../app/timeline_edit.h"
#include "../app/template_ops.h"
#include "../app/widget_state.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", what.c_str());
  }
}

std::string ReadFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::string Ids(const std::vector<classtime::Segment>& segments) {
  std::string joined;
  for (const auto& segment : segments) {
    if (!joined.empty()) joined += ",";
    joined += segment.id;
  }
  return joined;
}

std::string IdsOfIndices(const std::vector<classtime::Segment>& segments,
                         const std::vector<size_t>& indices) {
  std::string joined;
  for (const size_t index : indices) {
    if (!joined.empty()) joined += ",";
    joined += segments[index].id;
  }
  return joined;
}

bool NearlyEqual(double lhs, double rhs, double tolerance) {
  return std::fabs(lhs - rhs) <= tolerance;
}

classtime::WallClock Clock(const char* iso) {
  const auto parsed = classtime::WallClock::ParseIso(iso);
  Check(parsed.has_value(), std::string("时间可解析：") + iso);
  return parsed.value_or(classtime::WallClock{});
}

void TestPresets(const std::string& root, classtime::Schedule& highSchool,
                 classtime::Schedule& university) {
  std::string error;
  const auto high = classtime::ParsePreset(ReadFile(root + "/shared/presets/high-school.json"), &error);
  Check(high.has_value(), "高中预设可解析：" + error);
  if (high.has_value()) highSchool = *high;

  const auto uni = classtime::ParsePreset(ReadFile(root + "/shared/presets/university.json"), &error);
  Check(uni.has_value(), "大学预设可解析：" + error);
  if (uni.has_value()) university = *uni;

  Check(highSchool.segments.size() == 11, "高中预设包含 11 个时段");
  Check(university.segments.size() == 7, "大学预设包含 7 个时段");
  Check(classtime::FocusCount(highSchool.segments) == 6, "高中 6 节正课");
  Check(classtime::FocusCount(university.segments) == 4, "大学 4 大课");
  Check(highSchool.IsActiveOn(1) && !highSchool.IsActiveOn(6), "高中模板仅工作日启用");
}

void TestTimeVectors(const std::string& root, const classtime::Schedule& highSchool) {
  std::string error;
  const auto parsed =
      classtime::json::Value::Parse(ReadFile(root + "/shared/test-cases/time-engine.json"), &error);
  Check(parsed.has_value(), "时间测试向量可解析：" + error);
  if (!parsed.has_value()) return;

  const auto* cases = parsed->Find("cases");
  Check(cases != nullptr && cases->IsArray(), "测试向量含 cases 数组");
  if (cases == nullptr || !cases->IsArray()) return;

  for (const auto& item : cases->items()) {
    const auto* id = item.Find("id");
    const auto* nowText = item.Find("now");
    const auto* stateText = item.Find("state");
    const auto* segmentText = item.Find("currentSegmentId");
    const auto* boundaryText = item.Find("nextBoundary");
    if (id == nullptr || nowText == nullptr || stateText == nullptr) {
      Check(false, "测试向量字段完整");
      continue;
    }
    const std::string label = "case " + id->AsString();
    const auto now = classtime::WallClock::ParseIso(nowText->AsString());
    Check(now.has_value(), label + " now 可解析");
    if (!now.has_value()) continue;

    const auto snapshot = classtime::Evaluate(highSchool.segments, *now);
    Check(classtime::DayStateToString(snapshot.state) == stateText->AsString(),
          label + " state 期望 " + stateText->AsString() + " 实际 " +
              classtime::DayStateToString(snapshot.state));

    const std::string actualSegment = snapshot.current.has_value() ? snapshot.current->id : "";
    const std::string expectedSegment =
        (segmentText != nullptr && segmentText->IsString()) ? segmentText->AsString() : "";
    Check(actualSegment == expectedSegment,
          label + " currentSegmentId 期望 " + expectedSegment + " 实际 " + actualSegment);

    if (const auto* ordinal = item.Find("focusOrdinal")) {
      const int actual = snapshot.current.has_value() ? snapshot.current->focusOrdinal : 0;
      Check(actual == ordinal->AsInt(0),
            label + " focusOrdinal 期望 " + std::to_string(ordinal->AsInt(0)) + " 实际 " +
                std::to_string(actual));
    }
    if (const auto* progress = item.Find("progress")) {
      Check(NearlyEqual(snapshot.progress, progress->AsNumber(), 1e-8),
            label + " progress 期望 " + std::to_string(progress->AsNumber()) + " 实际 " +
                std::to_string(snapshot.progress));
    }

    const std::string actualBoundary =
        snapshot.nextBoundary.has_value() ? snapshot.nextBoundary->IsoString() : "";
    const std::string expectedBoundary =
        (boundaryText != nullptr && boundaryText->IsString()) ? boundaryText->AsString() : "";
    Check(actualBoundary == expectedBoundary,
          label + " nextBoundary 期望 " + expectedBoundary + " 实际 " + actualBoundary);
  }
}

void TestEditVectors(const std::string& root, const classtime::Schedule& highSchool) {
  std::string error;
  const auto parsed =
      classtime::json::Value::Parse(ReadFile(root + "/shared/test-cases/time-engine.json"), &error);
  if (!parsed.has_value()) return;
  const auto* cases = parsed->Find("editCases");
  if (cases == nullptr || !cases->IsArray()) {
    Check(false, "测试向量含 editCases 数组");
    return;
  }

  for (const auto& item : cases->items()) {
    const auto* id = item.Find("id");
    const auto* originalId = item.Find("originalId");
    const auto* newEnd = item.Find("newEnd");
    const auto* strategy = item.Find("strategy");
    if (id == nullptr || originalId == nullptr || newEnd == nullptr || strategy == nullptr) {
      Check(false, "编辑向量字段完整");
      continue;
    }
    const std::string label = "edit " + id->AsString();

    classtime::EditRequest request;
    request.segmentId = originalId->AsString();
    request.end = classtime::TimeOfDay::Parse(newEnd->AsString());
    request.strategy = strategy->AsString() == "keep" ? classtime::ShiftStrategy::Keep
                                                      : classtime::ShiftStrategy::Shift;
    const auto outcome = classtime::ApplyEdit(highSchool.segments, request);
    const bool expectedValid = item.Find("valid") == nullptr || item.Find("valid")->AsBool();
    Check(outcome.ok() == expectedValid, label + " 合法性符合预期");

    if (const auto* expected = item.Find("expected")) {
      for (const auto& member : expected->members()) {
        const classtime::Segment* segment =
            classtime::FindSegmentById(outcome.segments, member.first);
        Check(segment != nullptr, label + " 结果包含 " + member.first);
        if (segment == nullptr) continue;
        const auto* start = member.second.Find("start");
        const auto* end = member.second.Find("end");
        if (start != nullptr) {
          Check(segment->start.ToString() == start->AsString(),
                label + " " + member.first + " start 期望 " + start->AsString() + " 实际 " +
                    segment->start.ToString());
        }
        if (end != nullptr) {
          Check(segment->end.ToString() == end->AsString(),
                label + " " + member.first + " end 期望 " + end->AsString() + " 实际 " +
                    segment->end.ToString());
        }
      }
    }
    if (!expectedValid) {
      Check(!outcome.conflictIds.empty(), label + " 冲突时报告冲突时段");
    }
  }
}

void TestScope(const std::string& root, const classtime::Schedule& highSchool) {
  const auto visibleAt = [&](const char* iso) {
    return IdsOfIndices(highSchool.segments,
                        classtime::VisibleSegmentIndices(highSchool.segments, Clock(iso),
                                                         classtime::Scope::CurrentBlock));
  };

  Check(visibleAt("2026-11-23T07:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上课前展示第一块");
  Check(visibleAt("2026-11-23T09:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上午展示第一块");
  Check(visibleAt("2026-11-23T12:30:00") == "hl,h5,hb4,h6", "午休时展示休息和下一块");
  Check(visibleAt("2026-11-23T16:30:00") == "h5,hb4,h6", "放学后展示最后一块");

  std::string error;
  const auto vectors = classtime::json::Value::Parse(
      ReadFile(root + "/shared/test-cases/time-engine.json"), &error);
  const auto* scopeCases = vectors ? vectors->Find("scopeCases") : nullptr;
  Check(scopeCases && scopeCases->IsArray(), "共享向量包含显示范围用例");
  if (scopeCases && scopeCases->IsArray()) {
    for (const auto& item : scopeCases->items()) {
      const auto* id = item.Find("id");
      const auto* now = item.Find("now");
      const auto* expected = item.Find("visibleSegmentIds");
      if (!id || !now || !expected || !expected->IsArray()) {
        Check(false, "显示范围用例字段完整");
        continue;
      }
      std::string expectedIds;
      for (const auto& segment : expected->items()) {
        if (!expectedIds.empty()) expectedIds += ",";
        expectedIds += segment.AsString();
      }
      const auto actual = visibleAt(now->AsString().c_str());
      Check(actual == expectedIds, "共享显示范围 " + id->AsString() + " 期望 " +
                                       expectedIds + " 实际 " + actual);
    }
  }

  auto longBreakPlan = highSchool.segments;
  for (auto& segment : longBreakPlan) {
    if (segment.id == "hl") segment.kind = classtime::SegmentKind::LongBreak;
  }
  const auto lunchTime = Clock("2026-11-23T12:30:00");
  Check(IdsOfIndices(longBreakPlan, classtime::VisibleSegmentIndices(
                                        longBreakPlan, lunchTime, classtime::Scope::CurrentBlock)) ==
            "hl,h5,hb4,h6",
        "长休息与下一块同时显示");
  auto unsortedPlan = longBreakPlan;
  std::reverse(unsortedPlan.begin(), unsortedPlan.end());
  Check(IdsOfIndices(unsortedPlan, classtime::VisibleSegmentIndices(
                                       unsortedPlan, lunchTime, classtime::Scope::CurrentBlock)) ==
            "hl,h5,hb4,h6",
        "未排序的日程也按时间顺序展示长休息和下一块");
  longBreakPlan.erase(std::remove_if(longBreakPlan.begin(), longBreakPlan.end(),
                                    [](const classtime::Segment& segment) {
                                      return segment.start.TotalMinutes() >= 14 * 60;
                                    }),
                      longBreakPlan.end());
  Check(IdsOfIndices(longBreakPlan, classtime::VisibleSegmentIndices(
                                        longBreakPlan, lunchTime, classtime::Scope::CurrentBlock)) ==
            "h1,hb1,h2,hb2,h3,hb3,h4,hl",
        "末尾长休息与上一块按时间顺序显示");
  const auto rest = std::vector<classtime::Segment>{longBreakPlan.back()};
  Check(IdsOfIndices(rest, classtime::VisibleSegmentIndices(
                               rest, lunchTime, classtime::Scope::CurrentBlock)) == "hl",
        "仅有长休息的日程仍显示当前休息");

  const auto allDay =
      classtime::VisibleSegmentIndices(highSchool.segments, Clock("2026-11-23T12:30:00"),
                                       classtime::Scope::AllDay);
  Check(allDay.size() == highSchool.segments.size(), "全天范围包含全部时段");

  const auto bounds = classtime::CurrentBlockBounds(highSchool.segments, Clock("2026-11-23T12:30:00"));
  Check(bounds.has_value() && bounds->begin.ToString() == "14:00", "午休时下一块从 14:00 开始");
}

void TestBoundariesAndDedupe(const classtime::Schedule& highSchool) {
  const auto atBreak = classtime::EventsAt(highSchool.segments, Clock("2026-11-23T08:45:00"));
  Check(atBreak.size() == 2, "08:45 同时产生下课与课间两个边界事件");
  if (atBreak.size() == 2) {
    Check(atBreak[0].kind == classtime::BoundaryKind::FocusStart ||
              atBreak[0].kind == classtime::BoundaryKind::FocusEnd,
          "边界事件类型合法");
    const bool hasFocusEnd = atBreak[0].kind == classtime::BoundaryKind::FocusEnd ||
                             atBreak[1].kind == classtime::BoundaryKind::FocusEnd;
    const bool hasBreakStart = atBreak[0].kind == classtime::BoundaryKind::BreakStart ||
                               atBreak[1].kind == classtime::BoundaryKind::BreakStart;
    Check(hasFocusEnd && hasBreakStart, "包含 focus_end 与 break_start");
  }

  const auto atLunch = classtime::EventsAt(highSchool.segments, Clock("2026-11-23T11:30:00"));
  Check(atLunch.size() == 2, "11:30 同时产生第4节结束与午休开始");

  const auto key = classtime::EventKey("2026-11-23", "h1", classtime::BoundaryKind::FocusEnd, 3);
  classtime::RuntimeState state;
  Check(!classtime::HasFired(state, key), "初始状态未触发");
  classtime::MarkFired(state, key);
  Check(classtime::HasFired(state, key), "标记后视为已触发");
  const auto again = classtime::ParseRuntimeState(classtime::SerializeRuntimeState(state));
  Check(classtime::HasFired(again, key), "运行态可持久化去重键");
  Check(classtime::EventKey("2026-11-23", "h1", classtime::BoundaryKind::FocusEnd, 4) != key,
        "计划修订号参与去重键");
}

void TestTemplateCreation(const classtime::Schedule& highSchool,
                          const classtime::Schedule& university) {
  classtime::AppConfig config;
  config.templates = {highSchool, university};
  config.selectedTemplateId = highSchool.id;
  config.revision = 7;
  const auto now = Clock("2026-11-23T09:00:00");
  classtime::SetOverride(config, now.DateKey(), university.segments);
  std::string error;

  Check(ClassTimeApp::CreateTemplate(config, "我的课表", true, &error), "可以复制当前模板");
  Check(config.templates.size() == 3 && config.selectedTemplateId == "custom-1" &&
            config.revision == 8,
        "新模板有独立 ID，创建后选中并更新修订号");
  const auto* copy = classtime::SelectedTemplate(config);
  Check(copy && copy->name == "我的课表" && copy->activeWeekdays == highSchool.activeWeekdays &&
            copy->segments == highSchool.segments,
        "副本保留当前模板的时段和生效星期");
  bool fromOverride = false;
  Check(classtime::EffectiveSegments(config, now, &fromOverride) == university.segments && fromOverride,
        "创建模板不覆盖今天的临时计划");

  auto* editableCopy = classtime::FindTemplateMutable(config, "custom-1");
  if (editableCopy && !editableCopy->segments.empty()) editableCopy->segments[0].label = "副本修改";
  Check(classtime::FindTemplate(config, highSchool.id)->segments == highSchool.segments,
        "编辑副本不影响原模板");
  Check(ClassTimeApp::CreateTemplate(config, "空白课表", false, &error), "可以创建空白模板");
  const auto* blank = classtime::SelectedTemplate(config);
  Check(blank && blank->id == "custom-2" && blank->segments.empty() &&
            blank->activeWeekdays == std::vector<int>({1, 2, 3, 4, 5}),
        "空白模板使用不同 ID，并默认在工作日生效");
  const auto parsed = classtime::ParseConfig(classtime::SerializeConfig(config), &error);
  Check(parsed && parsed->templates.size() == 4 && parsed->selectedTemplateId == "custom-2" &&
            classtime::FindTemplate(*parsed, "custom-1") &&
            classtime::FindTemplate(*parsed, "custom-1")->segments[0].label == "副本修改" &&
            classtime::HasOverride(*parsed, now.DateKey()),
        "新模板、选择状态和今日覆盖可一起保存并恢复");
  classtime::RemoveOverride(config, now.DateKey());
  Check(classtime::EffectiveSegments(config, now).empty(), "移除今日覆盖后使用选中的空白模板");
  const auto previousCount = config.templates.size();
  const auto previousId = config.selectedTemplateId;
  Check(!ClassTimeApp::CreateTemplate(config, "", true, &error) &&
            config.templates.size() == previousCount && config.selectedTemplateId == previousId,
        "拒绝空名称且不改变原配置");
  config.selectedTemplateId = "missing";
  Check(!ClassTimeApp::CreateTemplate(config, "新模板", true, &error) &&
            config.templates.size() == previousCount && config.selectedTemplateId == "missing",
        "复制来源不存在时不改变配置");
}

void TestConfigRoundtrip(const classtime::Schedule& highSchool,
                         const classtime::Schedule& university) {
  classtime::AppConfig config;
  config.templates = {highSchool, university};
  config.selectedTemplateId = "university";
  config.presentation.view = classtime::PresentationSettings::View::Ring;
  config.presentation.scope = classtime::PresentationSettings::Scope::CurrentBlock;
  config.presentation.edge = classtime::PresentationSettings::Edge::Top;
  config.presentation.monitorId = "\\\\.\\DISPLAY2";
  config.presentation.autoHideAtEdge = true;
  config.presentation.flushWithEdge = false;
  config.presentation.hideInFullscreen = true;
  config.presentation.coverTaskbar = true;
  config.presentation.barLengthDip = 1040;
  config.presentation.barBackground = "2B4C7E";
  config.presentation.timelineSnapMinutes = 1;
  config.notifications.muted = true;
  config.notifications.screenEdgeFlash = true;
  config.notifications.dropClockAlert = true;
  config.notifications.dropClockStyle =
      classtime::NotificationSettings::AlertClockStyle::Mechanical3D;
  config.notifications.dropClockPosition =
      classtime::NotificationSettings::AlertClockPosition::TopRight;
  config.revision = 7;

  const auto now = Clock("2026-11-23T09:00:00");
  classtime::SetOverride(config, "2026-11-23", highSchool.segments);

  const std::string serialized = classtime::SerializeConfig(config);
  std::string error;
  const auto parsed = classtime::ParseConfig(serialized, &error);
  Check(parsed.has_value(), "配置可往返解析：" + error);
  if (!parsed.has_value()) return;

  Check(parsed->templates.size() == 2, "配置保留两份模板");
  Check(parsed->selectedTemplateId == "university", "配置保留选中模板");
  Check(parsed->presentation.view == classtime::PresentationSettings::View::Ring, "保留圆环视图");
  Check(parsed->presentation.edge == classtime::PresentationSettings::Edge::Top, "保留顶部贴边");
  Check(parsed->presentation.monitorId == "\\\\.\\DISPLAY2", "配置保留选择的显示器");
  Check(parsed->presentation.autoHideAtEdge, "保留贴边自动隐藏");
  Check(!parsed->presentation.flushWithEdge, "保留悬浮间距设置");
  Check(parsed->presentation.hideInFullscreen, "保留全屏自动隐藏设置");
  Check(parsed->presentation.coverTaskbar, "保留允许覆盖任务栏设置");
  Check(parsed->presentation.barLengthDip == 1040, "保留自定义长条长度");
  Check(parsed->presentation.barBackground == "2B4C7E", "保留自定义长条背景色");
  Check(parsed->presentation.timelineSnapMinutes == 1, "保留时间轴步进");

  config.presentation.edge = classtime::PresentationSettings::Edge::Left;
  const auto leftConfig = classtime::ParseConfig(classtime::SerializeConfig(config), &error);
  Check(leftConfig && leftConfig->presentation.edge == classtime::PresentationSettings::Edge::Left,
        "保留左侧贴边");
  Check(parsed->notifications.muted, "保留暂停状态");
  Check(parsed->notifications.screenEdgeFlash, "保留全屏边缘闪光提醒开关");
  Check(parsed->notifications.dropClockAlert, "保留掉落 3D 时钟提醒开关");
  Check(parsed->notifications.dropClockStyle ==
            classtime::NotificationSettings::AlertClockStyle::Mechanical3D,
        "保留 3D 机械表盘样式");
  Check(parsed->notifications.dropClockPosition ==
            classtime::NotificationSettings::AlertClockPosition::TopRight,
        "保留掉落 3D 时钟位置");
  Check(parsed->revision == 7, "保留修订号");

  config.presentation.view = classtime::PresentationSettings::View::ScreenEdge;
  config.presentation.screenEdgeThickness = 17;
  config.presentation.screenEdgeColors = classtime::PresentationSettings::ScreenEdgeColors::CurrentState;
  config.presentation.screenEdgeGradient = false;
  config.presentation.screenEdgeHighlight = false;
  const auto edgeConfig = classtime::ParseConfig(classtime::SerializeConfig(config), &error);
  Check(edgeConfig && edgeConfig->presentation.view == classtime::PresentationSettings::View::ScreenEdge,
        "保留屏幕边框视图");
  Check(edgeConfig && edgeConfig->presentation.screenEdgeThickness == 17, "保留屏幕边框粗细");
  Check(edgeConfig && edgeConfig->presentation.screenEdgeColors ==
                          classtime::PresentationSettings::ScreenEdgeColors::CurrentState,
        "保留屏幕边框的跟随当前时段配色");
  Check(edgeConfig && !edgeConfig->presentation.screenEdgeGradient, "保留屏幕边框非渐变设置");
  Check(edgeConfig && !edgeConfig->presentation.screenEdgeHighlight, "保留屏幕边框关闭高光设置");
  const auto withTemplate = "{\"templates\":[" + classtime::SerializeSchedule(highSchool, 0) +
                            "],\"presentation\":";
  const auto legacy = classtime::ParseConfig(withTemplate + R"({"view":"screen_edge"}})", &error);
  Check(legacy && legacy->presentation.screenEdgeThickness == 10, "旧配置默认使用加粗边框");
  Check(legacy && legacy->presentation.screenEdgeColors ==
                       classtime::PresentationSettings::ScreenEdgeColors::Schedule,
        "旧配置默认使用按时段分色");
  Check(legacy && !legacy->presentation.hideInFullscreen, "旧配置默认不启用全屏自动隐藏");
  Check(legacy && !legacy->presentation.coverTaskbar, "旧配置默认不覆盖任务栏");
  Check(legacy && legacy->presentation.screenEdgeGradient, "旧配置默认开启边框颜色渐变");
  Check(legacy && legacy->presentation.screenEdgeHighlight, "旧配置默认开启边框进度高光");
  const auto unknownColors =
      classtime::ParseConfig(withTemplate + R"({"screenEdgeColors":"unknown"}})", &error);
  Check(unknownColors && unknownColors->presentation.screenEdgeColors ==
                              classtime::PresentationSettings::ScreenEdgeColors::Schedule,
        "未知边框配色回退到按时段分色");
  Check(legacy && legacy->presentation.flushWithEdge, "旧配置默认紧贴边缘");
  Check(legacy && legacy->presentation.barLengthDip == 0 &&
            legacy->presentation.EffectiveBarLength() == 680,
        "旧配置保持横向长条默认长度 680");
  if (legacy) {
    auto side = legacy->presentation;
    side.edge = classtime::PresentationSettings::Edge::Left;
    Check(side.EffectiveBarLength() == 420, "旧配置保持竖向长条默认长度 420");
  }
  Check(legacy && legacy->presentation.timelineSnapMinutes == 5, "旧配置默认 5 分钟拖动步进");
  Check(legacy && legacy->presentation.barBackground.empty(), "旧配置默认跟随系统背景色");
  const auto oversized =
      classtime::ParseConfig(withTemplate + R"({"screenEdgeThickness":100}})", &error);
  Check(oversized && oversized->presentation.screenEdgeThickness == 24, "边框粗细限制在滑块范围内");
  const auto unknownEdge =
      classtime::ParseConfig(withTemplate + R"({"edge":"diagonal"}})", &error);
  Check(unknownEdge && unknownEdge->presentation.edge == classtime::PresentationSettings::Edge::Bottom,
        "未知贴边位置回退到底部");
  const auto tooLong = classtime::ParseConfig(withTemplate + R"({"barLengthDip":99999}})", &error);
  Check(tooLong && tooLong->presentation.barLengthDip == 1600, "长条长度上限 1600");
  const auto tooShort = classtime::ParseConfig(withTemplate + R"({"barLengthDip":30}})", &error);
  Check(tooShort && tooShort->presentation.barLengthDip == 240, "长条长度下限 240");
  const auto badColor =
      classtime::ParseConfig(withTemplate + R"({"barBackground":"XYZ123"}})", &error);
  Check(badColor && badColor->presentation.barBackground.empty(), "非法背景色回退跟随系统");
  const auto longColor =
      classtime::ParseConfig(withTemplate + R"({"barBackground":"#A1B2C3"}})", &error);
  Check(longColor && longColor->presentation.barBackground.empty(), "带井号的颜色视为非法");
  const auto invalidSnap =
      classtime::ParseConfig(withTemplate + R"({"timelineSnapMinutes":2}})", &error);
  Check(invalidSnap && invalidSnap->presentation.timelineSnapMinutes == 5, "非法步进回退默认值");
  Check(legacy && !legacy->notifications.screenEdgeFlash && !legacy->notifications.dropClockAlert &&
            legacy->notifications.dropClockStyle ==
                classtime::NotificationSettings::AlertClockStyle::Digital3D &&
            legacy->notifications.dropClockPosition ==
                classtime::NotificationSettings::AlertClockPosition::TopCenter,
        "旧配置默认关闭视觉到点动效并使用 3D 数字顶部居中");
  const auto invalidVisualNotify = classtime::ParseConfig(
      "{\"templates\":[" + classtime::SerializeSchedule(highSchool, 0) +
          "],\"notifications\":{\"dropClockStyle\":\"unknown\",\"dropClockPosition\":\"bottom\"}}",
      &error);
  Check(invalidVisualNotify &&
            invalidVisualNotify->notifications.dropClockStyle ==
                classtime::NotificationSettings::AlertClockStyle::Digital3D &&
            invalidVisualNotify->notifications.dropClockPosition ==
                classtime::NotificationSettings::AlertClockPosition::TopCenter &&
            invalidVisualNotify->notifications.dropClockSkin ==
                classtime::NotificationSettings::AlertClockSkin::Graphite,
        "未知 3D 时钟样式、皮肤或掉落位置回退默认值");
  Check(legacy && legacy->presentation.focusColor.empty() && legacy->presentation.restColor.empty() &&
            legacy->presentation.lunchColor.empty() &&
            legacy->notifications.screenEdgeFlashColor.empty() &&
            legacy->notifications.dropClockBodyColor.empty() &&
            legacy->notifications.dropClockFaceColor.empty() &&
            legacy->notifications.dropClockTextColor.empty() &&
            legacy->notifications.dropClockAccentColor.empty() &&
            legacy->notifications.dropClockSkin ==
                classtime::NotificationSettings::AlertClockSkin::Graphite,
        "旧配置默认全部自定义阶段色与视觉提醒颜色为空且皮肤为深邃金属");

  classtime::AppConfig colorConfig = config;
  colorConfig.presentation.focusColor = "1E6BFF";
  colorConfig.presentation.restColor = "22AA88";
  colorConfig.presentation.lunchColor = "F59E0B";
  colorConfig.notifications.dropClockSkin =
      classtime::NotificationSettings::AlertClockSkin::VintageGold;
  colorConfig.notifications.screenEdgeFlashColor = "FF5533";
  colorConfig.notifications.dropClockBodyColor = "231B14";
  colorConfig.notifications.dropClockFaceColor = "F3E7CC";
  colorConfig.notifications.dropClockTextColor = "2B2016";
  colorConfig.notifications.dropClockAccentColor = "00C2A8";
  const auto colorParsed = classtime::ParseConfig(classtime::SerializeConfig(colorConfig), &error);
  Check(colorParsed && colorParsed->presentation.focusColor == "1E6BFF" &&
            colorParsed->presentation.restColor == "22AA88" &&
            colorParsed->presentation.lunchColor == "F59E0B" &&
            colorParsed->notifications.dropClockSkin ==
                classtime::NotificationSettings::AlertClockSkin::VintageGold &&
            colorParsed->notifications.screenEdgeFlashColor == "FF5533" &&
            colorParsed->notifications.dropClockBodyColor == "231B14" &&
            colorParsed->notifications.dropClockFaceColor == "F3E7CC" &&
            colorParsed->notifications.dropClockTextColor == "2B2016" &&
            colorParsed->notifications.dropClockAccentColor == "00C2A8",
        "阶段配色、3D 时钟皮肤与自定义颜色往返读写保持一致");

  using Skin = classtime::NotificationSettings::AlertClockSkin;
  for (const auto skin : {Skin::Graphite, Skin::Ivory, Skin::VintageGold,
                          Skin::CyberNeon, Skin::RetroMatrix, Skin::OceanBlue}) {
    classtime::AppConfig skinCfg = config;
    skinCfg.notifications.dropClockSkin = skin;
    const auto parsedSkin = classtime::ParseConfig(classtime::SerializeConfig(skinCfg), &error);
    Check(parsedSkin && parsedSkin->notifications.dropClockSkin == skin,
          "3D 时钟皮肤枚举往返序列化一致");
  }

  const auto badColors = classtime::ParseConfig(
      "{\"templates\":[" + classtime::SerializeSchedule(highSchool, 0) +
          "],\"presentation\":{\"focusColor\":\"#112233\",\"restColor\":\"GGGGGG\",\"lunchColor\":\"12345\"},"
          "\"notifications\":{\"dropClockSkin\":\"unknown_skin\",\"screenEdgeFlashColor\":\"bad\",\"dropClockBodyColor\":\"1234567\",\"dropClockFaceColor\":\"xyz\",\"dropClockTextColor\":\"123\",\"dropClockAccentColor\":\"\"}}",
      &error);
  Check(badColors && badColors->presentation.focusColor.empty() &&
            badColors->presentation.restColor.empty() && badColors->presentation.lunchColor.empty() &&
            badColors->notifications.dropClockSkin ==
                classtime::NotificationSettings::AlertClockSkin::Graphite &&
            badColors->notifications.screenEdgeFlashColor.empty() &&
            badColors->notifications.dropClockBodyColor.empty() &&
            badColors->notifications.dropClockFaceColor.empty() &&
            badColors->notifications.dropClockTextColor.empty() &&
            badColors->notifications.dropClockAccentColor.empty(),
        "非法阶段色、未知皮肤或提醒颜色静默回退为空/默认，不阻断配置读取");

  bool fromOverride = false;
  const auto effective = classtime::EffectiveSegments(*parsed, now, &fromOverride);
  Check(fromOverride && effective.size() == highSchool.segments.size(), "今日覆盖优先于常规模板");

  classtime::AppConfig other = *parsed;
  classtime::RemoveOverride(other, "2026-11-23");
  bool otherOverride = true;
  const auto uniSegments = classtime::EffectiveSegments(other, now, &otherOverride);
  Check(!otherOverride && uniSegments.size() == university.segments.size(), "移除覆盖后回到模板");

  const auto saturday = Clock("2026-11-28T09:00:00");
  bool weekendOverride = true;
  const auto weekend = classtime::EffectiveSegments(other, saturday, &weekendOverride);
  Check(weekend.empty() && !weekendOverride, "周末无日程时返回空计划");

  classtime::PruneOverrides(config, "2026-11-24");
  Check(config.dailyOverrides.empty(), "过期覆盖会被清理");
}

void TestConfigVersions(const classtime::Schedule& highSchool) {
  const std::string templateField =
      "\"templates\":[" + classtime::SerializeSchedule(highSchool, 0) + "]";
  std::string error;

  const auto current = classtime::ParseConfig("{\"schemaVersion\":1," + templateField + "}", &error);
  Check(current && current->schemaVersion == 1, "当前版本配置可以读取");

  const auto legacy = classtime::ParseConfig("{" + templateField + "}", &error);
  Check(legacy && legacy->schemaVersion == 1, "缺少版本号的旧配置仍按版本 1 读取");

  for (const auto& version : {"2", "0", "-1", "1.5", "\"1\"", "null", "true"}) {
    const auto parsed = classtime::ParseConfig(
        "{\"schemaVersion\":" + std::string(version) + "," + templateField + "}", &error);
    Check(!parsed && !error.empty(), std::string("拒绝不兼容的配置版本 ") + version);
  }
}

void TestEditOperations(const classtime::Schedule& highSchool) {
  classtime::EditRequest rename;
  rename.segmentId = "h3";
  rename.label = std::string("数学课");
  const auto renamed = classtime::ApplyEdit(highSchool.segments, rename);
  Check(renamed.ok(), "仅改名可保存");
  if (renamed.ok()) {
    const classtime::Segment* segment = classtime::FindSegmentById(renamed.segments, "h3");
    Check(segment != nullptr && segment->label == "数学课", "改名生效");
    Check(segment != nullptr && segment->start.ToString() == "09:50", "改名不改变时间");
  }

  classtime::EditRequest startOnly;
  startOnly.segmentId = "h1";
  startOnly.start = classtime::TimeOfDay::Parse("08:10");
  const auto moved = classtime::ApplyEdit(highSchool.segments, startOnly);
  Check(moved.ok(), "只改开始时间可保存（结束保持不变）");
  if (moved.ok()) {
    const classtime::Segment* segment = classtime::FindSegmentById(moved.segments, "h1");
    Check(segment != nullptr && segment->end.ToString() == "08:45", "结束时间不被隐式重排");
  }

  classtime::EditRequest overlap;
  overlap.segmentId = "h1";
  overlap.end = classtime::TimeOfDay::Parse("09:00");
  overlap.strategy = classtime::ShiftStrategy::Keep;
  const auto conflict = classtime::ApplyEdit(highSchool.segments, overlap);
  Check(!conflict.ok() && !conflict.conflictIds.empty(), "保持原位时冲突被阻止");

  classtime::EditRequest tooLong;
  tooLong.segmentId = "h6";
  tooLong.durationMinutes = 600;
  const auto outOfRange = classtime::ApplyEdit(highSchool.segments, tooLong);
  Check(!outOfRange.ok(), "溢出到次日被拒绝");

  classtime::Segment extra;
  extra.id = "hx";
  extra.label = "临时课";
  extra.kind = classtime::SegmentKind::Focus;
  extra.start = *classtime::TimeOfDay::Parse("08:30");
  extra.end = *classtime::TimeOfDay::Parse("08:40");
  const auto inserted = classtime::InsertSegment(highSchool.segments, extra);
  Check(!inserted.ok(), "插入重叠时段被拒绝");

  classtime::Segment gap;
  gap.id = "hy";
  gap.label = "晚自习";
  gap.kind = classtime::SegmentKind::Focus;
  gap.start = *classtime::TimeOfDay::Parse("16:00");
  gap.end = *classtime::TimeOfDay::Parse("16:45");
  const auto appended = classtime::InsertSegment(highSchool.segments, gap);
  Check(appended.ok(), "插入不重叠时段成功");
  Check(appended.ok() && appended.segments.back().id == "hy", "插入后按时间排序");

  const auto removed = classtime::RemoveSegment(highSchool.segments, "hb3");
  Check(removed.ok(), "删除短课间后仍有合法空档");
  Check(removed.ok() && removed.segments.size() == 10, "删除后数量正确");
}

void TestJson() {
  const std::string source =
      "{\"name\":\"第1节\",\"n\":1.5,\"ok\":true,\"list\":[1,2],\"none\":null,"
      "\"escaped\":\"a\\nb\\u4e2d\\u6587\"}";
  std::string error;
  const auto parsed = classtime::json::Value::Parse(source, &error);
  Check(parsed.has_value(), "JSON 解析成功：" + error);
  if (!parsed.has_value()) return;
  Check(parsed->Find("name") != nullptr && parsed->Find("name")->AsString() == "第1节",
        "UTF-8 字符串保持正确");
  Check(parsed->Find("escaped") != nullptr && parsed->Find("escaped")->AsString() == "a\nb中文",
        "转义与 \\u 解码正确");
  const auto roundtrip = classtime::json::Value::Parse(parsed->Dump(2), &error);
  Check(roundtrip.has_value(), "JSON 往返可解析：" + error);
  Check(!classtime::json::Value::Parse("{\"a\":1,}", &error).has_value(), "尾随逗号被拒绝");
  Check(classtime::json::Utf8ToWide("中文") == L"中文", "UTF-8 转宽字符");
  Check(classtime::json::WideToUtf8(L"中文") == "中文", "宽字符转 UTF-8");
}

void TestTimelineDrag() {
  using ClassTimeApp::DraggedTimelineRange;
  using ClassTimeApp::TimelineDragMode;
  const ClassTimeApp::TimelineRange original{8 * 60 + 2, 8 * 60 + 47};
  const auto untouched = DraggedTimelineRange(original, TimelineDragMode::ResizeEnd, 0, 5);
  Check(untouched.start == 482 && untouched.end == 527, "拖动未移动时不舍入原始分钟");
  const auto resized = DraggedTimelineRange(original, TimelineDragMode::ResizeEnd, 5, 5);
  Check(resized.start == 482 && resized.end == 532, "调整结束时保留原分钟偏移");
  const auto moved = DraggedTimelineRange(original, TimelineDragMode::Move, -5, 5);
  Check(moved.start == 477 && moved.end == 522, "移动保留时长和分钟精度");
  const auto precise = DraggedTimelineRange(original, TimelineDragMode::ResizeStart, 1, 1);
  Check(precise.start == 483 && precise.end == 527, "1 分钟步进支持精确调整");
  const auto shortest = DraggedTimelineRange({10, 11}, TimelineDragMode::ResizeStart, 5, 5);
  Check(shortest.start == 10 && shortest.end == 11, "短时段不会被自动扩展到 5 分钟");
  const auto upper = DraggedTimelineRange({1400, 1438}, TimelineDragMode::Move, 15, 15);
  Check(upper.start == 1401 && upper.end == 1439, "整体拖动受当天结束边界约束");
}

void TestWidgetState(const classtime::Schedule& highSchool) {
  using Scope = classtime::PresentationSettings::Scope;
  const auto start = Clock("2026-11-23T08:00:00");
  const auto later = Clock("2026-11-23T08:15:00");
  const auto atStart = ClassTimeApp::BuildWidgetState(highSchool.segments, start, Scope::AllDay);
  const auto atLater = ClassTimeApp::BuildWidgetState(highSchool.segments, later, Scope::AllDay);
  Check(atLater.visibleSegments.size() == highSchool.segments.size(), "预览全天显示实际时段");
  Check(atLater.snapshot.current && atLater.snapshot.current->id == "h1", "预览使用当前实际时段");
  Check(NearlyEqual(ClassTimeApp::WidgetSegmentProgress(atStart.visibleSegments.front(), start,
                                                         atStart.snapshot), 0.0, 0.0001) &&
            NearlyEqual(ClassTimeApp::WidgetSegmentProgress(atLater.visibleSegments.front(), later,
                                                             atLater.snapshot), 1.0 / 3.0, 0.0001),
        "组件与预览按当前时钟更新同一时段进度");
  Check(atLater.edgeProgress > atStart.edgeProgress, "全天边框随当前时钟推进");
  const auto lunch = Clock("2026-11-23T12:30:00");
  const auto block = ClassTimeApp::BuildWidgetState(highSchool.segments, lunch, Scope::CurrentBlock);
  Check(!block.visibleSegments.empty() && block.visibleSegments.front().id == "hl" &&
            block.snapshot.current && block.snapshot.current->id == "hl" &&
            NearlyEqual(ClassTimeApp::WidgetSegmentProgress(block.visibleSegments.front(), lunch,
                                                            block.snapshot), 0.4, 0.0001),
        "当前时段预览与组件显示午休及其进度");
  Check(block.focusCompleted == 4 && block.focusTotal == 6, "圆环统计来自实际作息");
  const auto empty = ClassTimeApp::BuildWidgetState({}, later, Scope::AllDay);
  Check(empty.visibleSegments.empty() && !empty.snapshot.hasSchedule && empty.edgeProgress == 0,
        "空计划预览没有虚构课程或进度");
}

void TestScreenEdgeTimeline(const classtime::Schedule& highSchool) {
  using namespace ClassTimeApp;
  using Kind = classtime::SegmentKind;
  const auto noon = Clock("2026-11-23T12:30:00");
  const auto runs = BuildScreenEdgeRuns(highSchool.segments, noon);
  Check(!runs.empty() && NearlyEqual(runs.front().begin, 0, 1e-8) &&
            NearlyEqual(runs.back().end, 1, 1e-8),
        "全天边框覆盖首段到末段");
  bool contiguous = true;
  for (size_t i = 1; i < runs.size(); ++i) {
    contiguous = contiguous && NearlyEqual(runs[i - 1].end, runs[i].begin, 1e-8);
  }
  Check(contiguous, "全天边框各段连续且不重叠");
  std::vector<ScreenEdgeRun> lunch;
  for (const auto& run : runs) if (run.kind == Kind::Lunch) lunch.push_back(run);
  Check(lunch.size() == 2 && lunch[0].elapsed && !lunch[1].elapsed &&
            NearlyEqual(lunch[0].begin, 210.0 / 460, 1e-8) &&
            NearlyEqual(lunch[0].end, 270.0 / 460, 1e-8) &&
            NearlyEqual(lunch[1].end, 360.0 / 460, 1e-8),
        "午休按真实时长分配并在当前时间分为已过和未到");

  auto first = highSchool.segments.front();
  first.start = *classtime::TimeOfDay::Parse("08:00");
  first.end = *classtime::TimeOfDay::Parse("09:00");
  auto second = first;
  second.id = "later";
  second.start = *classtime::TimeOfDay::Parse("10:00");
  second.end = *classtime::TimeOfDay::Parse("11:00");
  const auto gap = BuildScreenEdgeRuns({second, first}, Clock("2026-11-23T09:30:00"));
  Check(gap.size() == 4 && !gap[1].kind && gap[1].elapsed && !gap[2].kind &&
            !gap[2].elapsed && NearlyEqual(gap[1].begin, 1.0 / 3, 1e-8) &&
            NearlyEqual(gap[1].end, 0.5, 1e-8) && NearlyEqual(gap[2].end, 2.0 / 3, 1e-8),
        "空档按实际时长占位，且不依赖输入顺序");
  const auto before = BuildScreenEdgeRuns({first, second}, Clock("2026-11-23T07:00:00"));
  const auto after = BuildScreenEdgeRuns({first, second}, Clock("2026-11-23T12:00:00"));
  Check(std::all_of(before.begin(), before.end(), [](const auto& run) { return !run.elapsed; }) &&
            std::all_of(after.begin(), after.end(), [](const auto& run) { return run.elapsed; }),
        "开始前全部为未到，结束后全部为已过");
  Check(BuildScreenEdgeRuns({}, noon).empty(), "无日程时没有虚构的分色时段");

  const auto stops = BuildScreenEdgeBlendStops(runs);
  bool ordered = !stops.empty();
  for (size_t i = 1; i < stops.size(); ++i) ordered = ordered &&
      stops[i - 1].position <= stops[i].position;
  Check(ordered && NearlyEqual(stops.front().position, 0, 1e-8) &&
            NearlyEqual(stops.back().position, 1, 1e-8),
        "颜色渐变按时间顺序覆盖完整边框");
  const std::vector<ScreenEdgeRun> shortBreak = {
      {0, 0.2, Kind::Focus, true}, {0.2, 0.202, Kind::Break, true},
      {0.202, 1, Kind::Focus, true}};
  const auto shortStops = BuildScreenEdgeBlendStops(shortBreak);
  Check(shortStops.size() == 6 && NearlyEqual(shortStops[1].position, 0.1992, 1e-8) &&
            NearlyEqual(shortStops[2].position, 0.2008, 1e-8) &&
            NearlyEqual(shortStops[3].position, 0.2012, 1e-8) &&
            shortStops[2].position < shortStops[3].position,
        "短课间两端的渐变收窄，保留可辨认的中段");
  const auto sameColor = BuildScreenEdgeBlendStops(
      {{0, 0.5, Kind::Focus, true}, {0.5, 1, Kind::Focus, true}});
  Check(sameColor.size() == 2, "相同颜色的相邻时段不制造额外渐变");
  Check(BuildScreenEdgeBlendStops({}).empty(), "无日程时不生成渐变");

  const std::vector<ScreenEdgeRun> acrossCorner = {{0.3, 0.6, Kind::Lunch, true}};
  const auto projected = ProjectScreenEdgeRuns(acrossCorner, {100, 60, 90, 50});
  Check(projected.size() == 3 && projected[0].side == 0 &&
            NearlyEqual(projected[0].begin, 90, 1e-8) && NearlyEqual(projected[0].end, 100, 1e-8) &&
            projected[1].side == 1 && NearlyEqual(projected[1].begin, 0, 1e-8) &&
            NearlyEqual(projected[1].end, 60, 1e-8) && projected[2].side == 2 &&
            NearlyEqual(projected[2].begin, 0, 1e-8) && NearlyEqual(projected[2].end, 20, 1e-8),
        "跨越边角的时段沿四边顺时针保持正确比例");

  // 屏幕边框进度前沿高光：0% / 100% 不显示；流光扫向当前点、前沿脉冲闪烁且保持色带连贯。
  const double perimeter = 6000.0;
  const double thickness = 10.0;
  const auto atZero = SampleScreenEdgeHighlight(0.0, 0.0, perimeter, thickness, 0.65);
  const auto atFull = SampleScreenEdgeHighlight(perimeter, perimeter, perimeter, thickness, 0.65);
  Check(atZero.boost == 0.0 && atZero.core == 0.0 && atZero.lip == 0.0 &&
            atFull.boost == 0.0 && atFull.core == 0.0 && atFull.lip == 0.0,
        "进度为 0 或 100% 时不叠加边框高光");
  const double head = 1500.0;
  const auto spec = BuildScreenEdgeHighlightSpec(perimeter, thickness);
  const auto peakHead = SampleScreenEdgeHighlight(head, head, perimeter, thickness, 0.65);
  const auto dimHead = SampleScreenEdgeHighlight(head, head, perimeter, thickness, 0.15);
  const auto behindCore = SampleScreenEdgeHighlight(head - spec.headSpan * 0.5, head, perimeter, thickness, 0.65);
  const auto behindEnd = SampleScreenEdgeHighlight(head - spec.tailLength - 1.0, head, perimeter, thickness, 0.65);
  const auto aheadLip = SampleScreenEdgeHighlight(head + spec.leadLength * 0.4, head, perimeter, thickness, 0.65);
  const auto aheadEnd = SampleScreenEdgeHighlight(head + spec.leadLength + 1.0, head, perimeter, thickness, 0.65);
  const auto shimmerWave = SampleScreenEdgeHighlight(head - spec.tailLength * 0.5, head, perimeter, thickness, 0.36);
  Check(peakHead.core > 0.9 && peakHead.boost > 0.8 && behindCore.core > 0.2 &&
            behindCore.core < peakHead.core && behindEnd.core == 0.0 && behindEnd.boost == 0.0,
        "边框高光峰值在前沿达到最高并在尾端以外归零");
  Check(peakHead.core > dimHead.core + 0.5,
        "边框前沿高光随闪烁周期在明暗之间明显脉冲");
  Check(shimmerWave.core > 0.6 && shimmerWave.boost > 0.5,
        "边框流光在已过尾段向当前进度点推进时形成明显波峰");
  Check(aheadLip.lip > 0.1 && aheadLip.core == 0.0 && aheadEnd.lip == 0.0,
        "边框前沿未到侧提供窄对比边且不溢出界外");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  std::printf("ClassTime 引擎测试，数据目录：%s\n", root.c_str());

  classtime::Schedule highSchool;
  classtime::Schedule university;
  TestJson();
  TestPresets(root, highSchool, university);
  if (!highSchool.segments.empty()) {
    TestTimeVectors(root, highSchool);
    TestEditVectors(root, highSchool);
    TestScope(root, highSchool);
    TestBoundariesAndDedupe(highSchool);
    TestEditOperations(highSchool);
    TestWidgetState(highSchool);
    TestScreenEdgeTimeline(highSchool);
  }
  TestConfigRoundtrip(highSchool, university);
  TestTemplateCreation(highSchool, university);
  TestConfigVersions(highSchool);
  TestTimelineDrag();

  std::printf("\n%d 项检查，%d 项失败\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
