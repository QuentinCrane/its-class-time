// 作息引擎对等测试：直接消费 shared/ 下的预设与测试向量。
// 运行：engine_tests <仓库根目录>
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

void TestScope(const classtime::Schedule& highSchool) {
  const auto visibleAt = [&](const char* iso) {
    return IdsOfIndices(highSchool.segments,
                        classtime::VisibleSegmentIndices(highSchool.segments, Clock(iso),
                                                         classtime::Scope::CurrentBlock));
  };

  Check(visibleAt("2026-11-23T07:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上课前展示第一块");
  Check(visibleAt("2026-11-23T09:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上午展示第一块");
  Check(visibleAt("2026-11-23T12:30:00") == "h5,hb4,h6", "午休时展示下一块");
  Check(visibleAt("2026-11-23T16:30:00") == "h5,hb4,h6", "放学后展示最后一块");

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

void TestConfigRoundtrip(const classtime::Schedule& highSchool,
                         const classtime::Schedule& university) {
  classtime::AppConfig config;
  config.templates = {highSchool, university};
  config.selectedTemplateId = "university";
  config.presentation.view = classtime::PresentationSettings::View::Ring;
  config.presentation.scope = classtime::PresentationSettings::Scope::CurrentBlock;
  config.presentation.edge = classtime::PresentationSettings::Edge::Top;
  config.notifications.muted = true;
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
  Check(parsed->notifications.muted, "保留暂停状态");
  Check(parsed->revision == 7, "保留修订号");

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
    TestScope(highSchool);
    TestBoundariesAndDedupe(highSchool);
    TestEditOperations(highSchool);
  }
  TestConfigRoundtrip(highSchool, university);

  std::printf("\n%d 项检查，%d 项失败\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
