// 作息引擎对等测试：直接消费 shared/ 下的预设与测试向量，并与 Windows engine_tests.cpp 保持完全对等。
// 运行：swift run engine-tests <仓库根目录>
import ClassTimeEngine
import Foundation

private var totalChecks = 0
private var totalFailures = 0

private func check(_ condition: Bool, _ what: String) {
  totalChecks += 1
  if !condition {
    totalFailures += 1
    print("FAIL  \(what)")
  }
}

private func readText(_ path: String) -> String {
  (try? String(contentsOfFile: path, encoding: .utf8)) ?? ""
}

private func nearlyEqual(_ lhs: Double, _ rhs: Double, tolerance: Double = 1e-8) -> Bool {
  abs(lhs - rhs) <= tolerance
}

private func clock(_ iso: String) -> WallClock {
  let parsed = WallClock.parseIso(iso)
  check(parsed != nil, "时间可解析：\(iso)")
  return parsed ?? WallClock()
}

private func idsOfIndices(_ segments: [Segment], _ indices: [Int]) -> String {
  indices.map { segments[$0].id }.joined(separator: ",")
}

private func testJson() {
  let source = "{\"name\":\"第1节\",\"n\":1.5,\"ok\":true,\"list\":[1,2],\"none\":null,\"escaped\":\"a\\nb\\u4e2d\\u6587\"}"
  guard let data = source.data(using: .utf8),
        let parsed = (try? JSONSerialization.jsonObject(with: data, options: [])) as? [String: Any] else {
    check(false, "JSON 解析成功")
    return
  }
  check(true, "JSON 解析成功")
  check((parsed["name"] as? String) == "第1节", "UTF-8 字符串保持正确")
  check((parsed["escaped"] as? String) == "a\nb中文", "转义与 \\u 解码正确")
  if let dumped = try? JSONSerialization.data(withJSONObject: parsed, options: [.prettyPrinted]),
     let roundtrip = try? JSONSerialization.jsonObject(with: dumped, options: []) {
    check(roundtrip is [String: Any], "JSON 往返可解析")
  } else {
    check(false, "JSON 往返可解析")
  }
  var trailingErr = ""
  check(parseConfig("{\"a\":1,}", error: &trailingErr) == nil, "尾随逗号被拒绝")
  check("中文".utf8.count == 6, "UTF-8 编码字节数正确")
  check(String(decoding: Array("中文".utf8), as: UTF8.self) == "中文", "UTF-8 往返解码正确")
}

private func testPresets(root: String) -> (Schedule, Schedule) {
  var error = ""
  let highOpt = parsePreset(readText("\(root)/shared/presets/high-school.json"), error: &error)
  check(highOpt != nil, "高中预设可解析：\(error)")
  let highSchool = highOpt ?? Schedule()

  let uniOpt = parsePreset(readText("\(root)/shared/presets/university.json"), error: &error)
  check(uniOpt != nil, "大学预设可解析：\(error)")
  let university = uniOpt ?? Schedule()

  check(highSchool.segments.count == 11, "高中预设包含 11 个时段")
  check(university.segments.count == 7, "大学预设包含 7 个时段")
  check(focusCount(highSchool.segments) == 6, "高中 6 节正课")
  check(focusCount(university.segments) == 4, "大学 4 大课")
  check(highSchool.isActive(on: 1) && !highSchool.isActive(on: 6), "高中模板仅工作日启用")
  return (highSchool, university)
}

private func testTimeVectors(root: String, highSchool: Schedule) {
  let raw = readText("\(root)/shared/test-cases/time-engine.json")
  guard let data = raw.data(using: .utf8),
        let rootObj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
    check(false, "时间测试向量可解析")
    return
  }
  check(true, "时间测试向量可解析")

  guard let cases = rootObj["cases"] as? [[String: Any]] else {
    check(false, "测试向量含 cases 数组")
    return
  }
  check(true, "测试向量含 cases 数组")

  for item in cases {
    guard let id = item["id"] as? String,
          let nowText = item["now"] as? String,
          let stateText = item["state"] as? String else {
      check(false, "测试向量字段完整")
      continue
    }
    let label = "case \(id)"
    let nowOpt = WallClock.parseIso(nowText)
    check(nowOpt != nil, "\(label) now 可解析")
    guard let now = nowOpt else { continue }

    let snapshot = evaluate(highSchool.segments, now: now)
    check(
      snapshot.state.rawValue == stateText,
      "\(label) state 期望 \(stateText) 实际 \(snapshot.state.rawValue)"
    )

    let actualSegment = snapshot.current?.id ?? ""
    let expectedSegment = (item["currentSegmentId"] as? String) ?? ""
    check(
      actualSegment == expectedSegment,
      "\(label) currentSegmentId 期望 \(expectedSegment) 实际 \(actualSegment)"
    )

    if let ordinal = (item["focusOrdinal"] as? NSNumber)?.intValue {
      let actual = snapshot.current?.focusOrdinal ?? 0
      check(actual == ordinal, "\(label) focusOrdinal 期望 \(ordinal) 实际 \(actual)")
    }
    if let progress = (item["progress"] as? NSNumber)?.doubleValue {
      check(
        nearlyEqual(snapshot.progress, progress),
        "\(label) progress 期望 \(progress) 实际 \(snapshot.progress)"
      )
    }

    let actualBoundary = snapshot.nextBoundary?.isoString ?? ""
    let expectedBoundary = (item["nextBoundary"] as? String) ?? ""
    check(
      actualBoundary == expectedBoundary,
      "\(label) nextBoundary 期望 \(expectedBoundary) 实际 \(actualBoundary)"
    )
  }
}

private func testEditVectors(root: String, highSchool: Schedule) {
  let raw = readText("\(root)/shared/test-cases/time-engine.json")
  guard let data = raw.data(using: .utf8),
        let rootObj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
        let cases = rootObj["editCases"] as? [[String: Any]] else {
    check(false, "测试向量含 editCases 数组")
    return
  }

  for item in cases {
    guard let id = item["id"] as? String,
          let originalId = item["originalId"] as? String,
          let newEnd = item["newEnd"] as? String,
          let strategyText = item["strategy"] as? String else {
      check(false, "编辑向量字段完整")
      continue
    }
    let label = "edit \(id)"
    let request = EditRequest(
      segmentId: originalId,
      end: TimeOfDay.parse(newEnd),
      strategy: strategyText == "keep" ? .keep : .shift
    )
    let outcome = applyEdit(highSchool.segments, request: request)
    let expectedValid = (item["valid"] as? Bool) ?? true
    check(outcome.ok == expectedValid, "\(label) 合法性符合预期")

    if let expected = item["expected"] as? [String: [String: Any]] {
      for (segId, fields) in expected.sorted(by: { $0.key < $1.key }) {
        let seg = findSegmentById(outcome.segments, segId)
        check(seg != nil, "\(label) 结果包含 \(segId)")
        guard let seg else { continue }
        if let start = fields["start"] as? String {
          check(
            seg.start.descriptionText == start,
            "\(label) \(segId) start 期望 \(start) 实际 \(seg.start.descriptionText)"
          )
        }
        if let end = fields["end"] as? String {
          check(
            seg.end.descriptionText == end,
            "\(label) \(segId) end 期望 \(end) 实际 \(seg.end.descriptionText)"
          )
        }
      }
    }
    if !expectedValid {
      check(!outcome.conflictIds.isEmpty, "\(label) 冲突时报告冲突时段")
    }
  }
}

private func testScope(root: String, highSchool: Schedule) {
  func visibleAt(_ iso: String) -> String {
    idsOfIndices(
      highSchool.segments,
      visibleSegmentIndices(highSchool.segments, now: clock(iso), scope: .currentBlock)
    )
  }

  check(visibleAt("2026-11-23T07:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上课前展示第一块")
  check(visibleAt("2026-11-23T09:00:00") == "h1,hb1,h2,hb2,h3,hb3,h4", "上午展示第一块")
  check(visibleAt("2026-11-23T12:30:00") == "hl,h5,hb4,h6", "午休时展示休息和下一块")
  check(visibleAt("2026-11-23T16:30:00") == "h5,hb4,h6", "放学后展示最后一块")

  let raw = readText("\(root)/shared/test-cases/time-engine.json")
  let rootObj = (raw.data(using: .utf8).flatMap { try? JSONSerialization.jsonObject(with: $0) }) as? [String: Any]
  let scopeCases = rootObj?["scopeCases"] as? [[String: Any]]
  check(scopeCases != nil, "共享向量包含显示范围用例")
  if let scopeCases {
    for item in scopeCases {
      guard let id = item["id"] as? String,
            let now = item["now"] as? String,
            let expected = item["visibleSegmentIds"] as? [String] else {
        check(false, "显示范围用例字段完整")
        continue
      }
      let expectedIds = expected.joined(separator: ",")
      let actual = visibleAt(now)
      check(actual == expectedIds, "共享显示范围 \(id) 期望 \(expectedIds) 实际 \(actual)")
    }
  }

  var longBreakPlan = highSchool.segments
  for i in longBreakPlan.indices where longBreakPlan[i].id == "hl" {
    longBreakPlan[i].kind = .longBreak
  }
  let lunchTime = clock("2026-11-23T12:30:00")
  check(
    idsOfIndices(longBreakPlan, visibleSegmentIndices(longBreakPlan, now: lunchTime, scope: .currentBlock))
      == "hl,h5,hb4,h6",
    "长休息与下一块同时显示"
  )

  let unsortedPlan = Array(longBreakPlan.reversed())
  check(
    idsOfIndices(unsortedPlan, visibleSegmentIndices(unsortedPlan, now: lunchTime, scope: .currentBlock))
      == "hl,h5,hb4,h6",
    "未排序的日程也按时间顺序展示长休息和下一块"
  )

  longBreakPlan.removeAll { $0.start.totalMinutes >= 14 * 60 }
  check(
    idsOfIndices(longBreakPlan, visibleSegmentIndices(longBreakPlan, now: lunchTime, scope: .currentBlock))
      == "h1,hb1,h2,hb2,h3,hb3,h4,hl",
    "末尾长休息与上一块按时间顺序显示"
  )

  let restOnly = [longBreakPlan.last!]
  check(
    idsOfIndices(restOnly, visibleSegmentIndices(restOnly, now: lunchTime, scope: .currentBlock)) == "hl",
    "仅有长休息的日程仍显示当前休息"
  )

  let allDay = visibleSegmentIndices(
    highSchool.segments,
    now: clock("2026-11-23T12:30:00"),
    scope: .allDay
  )
  check(allDay.count == highSchool.segments.count, "全天范围包含全部时段")

  let bounds = currentBlockBounds(highSchool.segments, now: clock("2026-11-23T12:30:00"))
  check(bounds?.begin.descriptionText == "14:00", "午休时下一块从 14:00 开始")
}

private func testBoundariesAndDedupe(highSchool: Schedule) {
  let atBreak = eventsAt(highSchool.segments, now: clock("2026-11-23T08:45:00"))
  check(atBreak.count == 2, "08:45 同时产生下课与课间两个边界事件")
  if atBreak.count == 2 {
    check(
      atBreak[0].kind == .focusStart || atBreak[0].kind == .focusEnd,
      "边界事件类型合法"
    )
    let hasFocusEnd = atBreak.contains { $0.kind == .focusEnd }
    let hasBreakStart = atBreak.contains { $0.kind == .breakStart }
    check(hasFocusEnd && hasBreakStart, "包含 focus_end 与 break_start")
  }

  let atLunch = eventsAt(highSchool.segments, now: clock("2026-11-23T11:30:00"))
  check(atLunch.count == 2, "11:30 同时产生第4节结束与午休开始")

  let key = eventKey(dateKey: "2026-11-23", segmentId: "h1", kind: .focusEnd, planRevision: 3)
  var state = RuntimeState()
  check(!hasFired(state, key: key), "初始状态未触发")
  markFired(&state, key: key)
  check(hasFired(state, key: key), "标记后视为已触发")
  let again = parseRuntimeState(serializeRuntimeState(state))
  check(hasFired(again, key: key), "运行态可持久化去重键")
  check(
    eventKey(dateKey: "2026-11-23", segmentId: "h1", kind: .focusEnd, planRevision: 4) != key,
    "计划修订号参与去重键"
  )
}

private func testTemplateCreation(highSchool: Schedule, university: Schedule) {
  var config = AppConfig()
  config.templates = [highSchool, university]
  config.selectedTemplateId = highSchool.id
  config.revision = 7
  let now = clock("2026-11-23T09:00:00")
  setOverride(&config, dateKey: now.dateKey, segments: university.segments)
  var error = ""

  check(createTemplate(&config, name: "我的课表", copyCurrent: true, error: &error), "可以复制当前模板")
  check(
    config.templates.count == 3 && config.selectedTemplateId == "custom-1" && config.revision == 8,
    "新模板有独立 ID，创建后选中并更新修订号"
  )
  let copy = selectedTemplate(config)
  check(
    copy?.name == "我的课表"
      && copy?.activeWeekdays == highSchool.activeWeekdays
      && copy?.segments == highSchool.segments,
    "副本保留当前模板的时段和生效星期"
  )
  let eff = effectiveSegments(config, now: now)
  check(eff.segments == university.segments && eff.fromOverride, "创建模板不覆盖今天的临时计划")

  if let idx = config.templates.firstIndex(where: { $0.id == "custom-1" }),
     !config.templates[idx].segments.isEmpty {
    config.templates[idx].segments[0].label = "副本修改"
  }
  check(findTemplate(config, id: highSchool.id)?.segments == highSchool.segments, "编辑副本不影响原模板")

  check(createTemplate(&config, name: "空白课表", copyCurrent: false, error: &error), "可以创建空白模板")
  let blank = selectedTemplate(config)
  check(
    blank?.id == "custom-2" && blank?.segments.isEmpty == true && blank?.activeWeekdays == [1, 2, 3, 4, 5],
    "空白模板使用不同 ID，并默认在工作日生效"
  )

  let parsed = parseConfig(serializeConfig(config), error: &error)
  check(
    parsed?.templates.count == 4
      && parsed?.selectedTemplateId == "custom-2"
      && findTemplate(parsed!, id: "custom-1")?.segments.first?.label == "副本修改"
      && hasOverride(parsed!, dateKey: now.dateKey),
    "新模板、选择状态和今日覆盖可一起保存并恢复"
  )

  removeOverride(&config, dateKey: now.dateKey)
  check(effectiveSegments(config, now: now).segments.isEmpty, "移除今日覆盖后使用选中的空白模板")

  let previousCount = config.templates.count
  let previousId = config.selectedTemplateId
  check(
    !createTemplate(&config, name: "", copyCurrent: true, error: &error)
      && config.templates.count == previousCount
      && config.selectedTemplateId == previousId,
    "拒绝空名称且不改变原配置"
  )

  config.selectedTemplateId = "missing"
  check(
    !createTemplate(&config, name: "新模板", copyCurrent: true, error: &error)
      && config.templates.count == previousCount
      && config.selectedTemplateId == "missing",
    "复制来源不存在时不改变配置"
  )
}

private func testEditOperations(highSchool: Schedule) {
  let rename = EditRequest(segmentId: "h3", label: "数学课")
  let renamed = applyEdit(highSchool.segments, request: rename)
  check(renamed.ok, "仅改名可保存")
  if renamed.ok {
    let seg = findSegmentById(renamed.segments, "h3")
    check(seg?.label == "数学课", "改名生效")
    check(seg?.start.descriptionText == "09:50", "改名不改变时间")
  }

  let startOnly = EditRequest(segmentId: "h1", start: TimeOfDay.parse("08:10"))
  let moved = applyEdit(highSchool.segments, request: startOnly)
  check(moved.ok, "只改开始时间可保存（结束保持不变）")
  if moved.ok {
    let seg = findSegmentById(moved.segments, "h1")
    check(seg?.end.descriptionText == "08:45", "结束时间不被隐式重排")
  }

  let overlap = EditRequest(
    segmentId: "h1",
    end: TimeOfDay.parse("09:00"),
    strategy: .keep
  )
  let conflict = applyEdit(highSchool.segments, request: overlap)
  check(!conflict.ok && !conflict.conflictIds.isEmpty, "保持原位时冲突被阻止")

  let tooLong = EditRequest(segmentId: "h6", durationMinutes: 600)
  let outOfRange = applyEdit(highSchool.segments, request: tooLong)
  check(!outOfRange.ok, "溢出到次日被拒绝")

  let extra = Segment(
    id: "hx",
    label: "临时课",
    kind: .focus,
    start: TimeOfDay.parse("08:30")!,
    end: TimeOfDay.parse("08:40")!
  )
  let inserted = insertSegment(highSchool.segments, segment: extra)
  check(!inserted.ok, "插入重叠时段被拒绝")

  let gap = Segment(
    id: "hy",
    label: "晚自习",
    kind: .focus,
    start: TimeOfDay.parse("16:00")!,
    end: TimeOfDay.parse("16:45")!
  )
  let appended = insertSegment(highSchool.segments, segment: gap)
  check(appended.ok, "插入不重叠时段成功")
  check(appended.ok && appended.segments.last?.id == "hy", "插入后按时间排序")

  let removed = removeSegment(highSchool.segments, id: "hb3")
  check(removed.ok, "删除短课间后仍有合法空档")
  check(removed.ok && removed.segments.count == 10, "删除后数量正确")
}

private func testConfigRoundtrip(highSchool: Schedule, university: Schedule) {
  var config = AppConfig()
  config.templates = [highSchool, university]
  config.selectedTemplateId = "university"
  config.presentation.view = .ring
  config.presentation.scope = .currentBlock
  config.presentation.edge = .top
  config.presentation.monitorId = "display-2"
  config.presentation.menuBarDisplay = .detailed
  config.presentation.glassStyle = .regular
  config.presentation.autoHideAtEdge = true
  config.presentation.flushWithEdge = false
  config.presentation.barLengthDip = 1040
  config.presentation.barBackground = "2B4C7E"
  config.presentation.timelineSnapMinutes = 1
  config.notifications.muted = true
  config.revision = 7

  let now = clock("2026-11-23T09:00:00")
  setOverride(&config, dateKey: "2026-11-23", segments: highSchool.segments)

  let serialized = serializeConfig(config)
  var error = ""
  let parsedOpt = parseConfig(serialized, error: &error)
  check(parsedOpt != nil, "配置可往返解析：\(error)")
  guard let parsed = parsedOpt else { return }

  check(parsed.templates.count == 2, "配置保留两份模板")
  check(parsed.selectedTemplateId == "university", "配置保留选中模板")
  check(parsed.presentation.view == .ring, "保留圆环视图")
  check(parsed.presentation.edge == .top, "保留顶部贴边")
  check(parsed.presentation.monitorId == "display-2", "配置保留选择的显示器")
  check(parsed.presentation.menuBarDisplay == .detailed, "保留顶部状态栏显示模式")
  check(parsed.presentation.glassStyle == .regular, "保留液态玻璃通透度样式")
  check(parsed.presentation.autoHideAtEdge, "保留贴边自动隐藏")
  check(!parsed.presentation.flushWithEdge, "保留悬浮间距设置")
  check(parsed.presentation.barLengthDip == 1040, "保留自定义长条长度")
  check(parsed.presentation.barBackground == "2B4C7E", "保留自定义长条背景色")
  check(parsed.presentation.timelineSnapMinutes == 1, "保留时间轴步进")

  config.presentation.edge = .left
  let leftConfig = parseConfig(serializeConfig(config), error: &error)
  check(leftConfig?.presentation.edge == .left, "保留左侧贴边")
  check(parsed.notifications.muted, "保留暂停状态")
  check(parsed.revision == 7, "保留修订号")

  config.presentation.view = .screenEdge
  config.presentation.screenEdgeThickness = 17
  config.presentation.screenEdgeColors = .currentState
  let edgeConfig = parseConfig(serializeConfig(config), error: &error)
  check(edgeConfig?.presentation.view == .screenEdge, "保留屏幕边框视图")
  check(edgeConfig?.presentation.screenEdgeThickness == 17, "保留屏幕边框粗细")
  check(edgeConfig?.presentation.screenEdgeColors == .currentState, "保留屏幕边框的跟随当前时段配色")

  let withTemplate = "{\"templates\":[\(serializeSchedule(highSchool, pretty: false))],\"presentation\":"
  let legacy = parseConfig("\(withTemplate){\"view\":\"screen_edge\"}}", error: &error)
  check(legacy?.presentation.screenEdgeThickness == 10, "旧配置默认使用加粗边框")
  check(legacy?.presentation.screenEdgeColors == .schedule, "旧配置默认使用按时段分色")

  let unknownColors = parseConfig("\(withTemplate){\"screenEdgeColors\":\"unknown\"}}", error: &error)
  check(unknownColors?.presentation.screenEdgeColors == .schedule, "未知边框配色回退到按时段分色")

  check(legacy?.presentation.flushWithEdge == true, "旧配置默认紧贴边缘")
  check(legacy?.presentation.menuBarDisplay == .countdown, "旧配置默认顶部状态栏显示剩余时间")
  check(legacy?.presentation.glassStyle == .clear, "旧配置默认通透液态玻璃")
  check(
    legacy?.presentation.barLengthDip == 0 && legacy?.presentation.effectiveBarLength == 680,
    "旧配置保持横向长条默认长度 680"
  )
  if var side = legacy?.presentation {
    side.edge = .left
    check(side.effectiveBarLength == 420, "旧配置保持竖向长条默认长度 420")
  }
  check(legacy?.presentation.timelineSnapMinutes == 5, "旧配置默认 5 分钟拖动步进")
  check(legacy?.presentation.barBackground.isEmpty == true, "旧配置默认跟随系统背景色")

  let oversized = parseConfig("\(withTemplate){\"screenEdgeThickness\":100}}", error: &error)
  check(oversized?.presentation.screenEdgeThickness == 24, "边框粗细限制在滑块范围内")

  let unknownEdge = parseConfig("\(withTemplate){\"edge\":\"diagonal\"}}", error: &error)
  check(unknownEdge?.presentation.edge == .bottom, "未知贴边位置回退到底部")

  let tooLong = parseConfig("\(withTemplate){\"barLengthDip\":99999}}", error: &error)
  check(tooLong?.presentation.barLengthDip == 1600, "长条长度上限 1600")

  let tooShort = parseConfig("\(withTemplate){\"barLengthDip\":30}}", error: &error)
  check(tooShort?.presentation.barLengthDip == 240, "长条长度下限 240")

  let badColor = parseConfig("\(withTemplate){\"barBackground\":\"XYZ123\"}}", error: &error)
  check(badColor?.presentation.barBackground.isEmpty == true, "非法背景色回退跟随系统")

  let longColor = parseConfig("\(withTemplate){\"barBackground\":\"#A1B2C3\"}}", error: &error)
  check(longColor?.presentation.barBackground.isEmpty == true, "带井号的颜色视为非法")

  let invalidSnap = parseConfig("\(withTemplate){\"timelineSnapMinutes\":2}}", error: &error)
  check(invalidSnap?.presentation.timelineSnapMinutes == 5, "非法步进回退默认值")

  let effective = effectiveSegments(parsed, now: now)
  check(effective.fromOverride && effective.segments.count == highSchool.segments.count, "今日覆盖优先于常规模板")

  var other = parsed
  removeOverride(&other, dateKey: "2026-11-23")
  let uniSegments = effectiveSegments(other, now: now)
  check(!uniSegments.fromOverride && uniSegments.segments.count == university.segments.count, "移除覆盖后回到模板")

  let saturday = clock("2026-11-28T09:00:00")
  let weekend = effectiveSegments(other, now: saturday)
  check(weekend.segments.isEmpty && !weekend.fromOverride, "周末无日程时返回空计划")

  pruneOverrides(&config, keepFromDate: "2026-11-24")
  check(config.dailyOverrides.isEmpty, "过期覆盖会被清理")
}

private func testConfigVersions(highSchool: Schedule) {
  let templateField = "\"templates\":[\(serializeSchedule(highSchool, pretty: false))]"
  var error = ""

  let current = parseConfig("{\"schemaVersion\":1,\(templateField)}", error: &error)
  check(current?.schemaVersion == 1, "当前版本配置可以读取")

  let legacy = parseConfig("{\(templateField)}", error: &error)
  check(legacy?.schemaVersion == 1, "缺少版本号的旧配置仍按版本 1 读取")

  for version in ["2", "0", "-1", "1.5", "\"1\"", "null", "true"] {
    let parsed = parseConfig("{\"schemaVersion\":\(version),\(templateField)}", error: &error)
    check(parsed == nil && !error.isEmpty, "拒绝不兼容的配置版本 \(version)")
  }
}

private func testTimelineDrag() {
  let original = TimelineRange(start: 8 * 60 + 2, end: 8 * 60 + 47)
  let untouched = draggedTimelineRange(original: original, mode: .resizeEnd, deltaMinutes: 0, stepMinutes: 5)
  check(untouched.start == 482 && untouched.end == 527, "拖动未移动时不舍入原始分钟")
  let resized = draggedTimelineRange(original: original, mode: .resizeEnd, deltaMinutes: 5, stepMinutes: 5)
  check(resized.start == 482 && resized.end == 532, "调整结束时保留原分钟偏移")
  let moved = draggedTimelineRange(original: original, mode: .move, deltaMinutes: -5, stepMinutes: 5)
  check(moved.start == 477 && moved.end == 522, "移动保留时长和分钟精度")
  let precise = draggedTimelineRange(original: original, mode: .resizeStart, deltaMinutes: 1, stepMinutes: 1)
  check(precise.start == 483 && precise.end == 527, "1 分钟步进支持精确调整")
  let shortest = draggedTimelineRange(original: TimelineRange(start: 10, end: 11), mode: .resizeStart, deltaMinutes: 5, stepMinutes: 5)
  check(shortest.start == 10 && shortest.end == 11, "短时段不会被自动扩展到 5 分钟")
  let upper = draggedTimelineRange(original: TimelineRange(start: 1400, end: 1438), mode: .move, deltaMinutes: 15, stepMinutes: 15)
  check(upper.start == 1401 && upper.end == 1439, "整体拖动受当天结束边界约束")
}

private func testWidgetState(highSchool: Schedule) {
  let start = clock("2026-11-23T08:00:00")
  let later = clock("2026-11-23T08:15:00")
  let atStart = buildWidgetState(segments: highSchool.segments, now: start, scope: .allDay)
  let atLater = buildWidgetState(segments: highSchool.segments, now: later, scope: .allDay)
  check(atLater.visibleSegments.count == highSchool.segments.count, "预览全天显示实际时段")
  check(atLater.snapshot.current?.id == "h1", "预览使用当前实际时段")
  check(
    nearlyEqual(
      widgetSegmentProgress(segment: atStart.visibleSegments[0], now: start, snapshot: atStart.snapshot),
      0.0,
      tolerance: 0.0001
    )
      && nearlyEqual(
        widgetSegmentProgress(segment: atLater.visibleSegments[0], now: later, snapshot: atLater.snapshot),
        1.0 / 3.0,
        tolerance: 0.0001
      ),
    "组件与预览按当前时钟更新同一时段进度"
  )
  check(atLater.edgeProgress > atStart.edgeProgress, "全天边框随当前时钟推进")

  let lunch = clock("2026-11-23T12:30:00")
  let block = buildWidgetState(segments: highSchool.segments, now: lunch, scope: .currentBlock)
  check(
    block.visibleSegments.first?.id == "hl"
      && block.snapshot.current?.id == "hl"
      && nearlyEqual(
        widgetSegmentProgress(segment: block.visibleSegments[0], now: lunch, snapshot: block.snapshot),
        0.4,
        tolerance: 0.0001
      ),
    "当前时段预览与组件显示午休及其进度"
  )
  check(block.focusCompleted == 4 && block.focusTotal == 6, "圆环统计来自实际作息")

  let empty = buildWidgetState(segments: [], now: later, scope: .allDay)
  check(empty.visibleSegments.isEmpty && !empty.snapshot.hasSchedule && empty.edgeProgress == 0, "空计划预览没有虚构课程或进度")
}

private func testScreenEdgeTimeline(highSchool: Schedule) {
  let noon = clock("2026-11-23T12:30:00")
  let runs = buildScreenEdgeRuns(segments: highSchool.segments, now: noon)
  check(
    !runs.isEmpty && nearlyEqual(runs.first!.begin, 0) && nearlyEqual(runs.last!.end, 1),
    "全天边框覆盖首段到末段"
  )
  var contiguous = true
  for i in 1..<runs.count {
    contiguous = contiguous && nearlyEqual(runs[i - 1].end, runs[i].begin)
  }
  check(contiguous, "全天边框各段连续且不重叠")

  let lunch = runs.filter { $0.kind == .lunch }
  check(
    lunch.count == 2 && lunch[0].elapsed && !lunch[1].elapsed
      && nearlyEqual(lunch[0].begin, 210.0 / 460.0)
      && nearlyEqual(lunch[0].end, 270.0 / 460.0)
      && nearlyEqual(lunch[1].end, 360.0 / 460.0),
    "午休按真实时长分配并在当前时间分为已过和未到"
  )

  var first = highSchool.segments[0]
  first.start = TimeOfDay.parse("08:00")!
  first.end = TimeOfDay.parse("09:00")!
  var second = first
  second.id = "later"
  second.start = TimeOfDay.parse("10:00")!
  second.end = TimeOfDay.parse("11:00")!
  let gap = buildScreenEdgeRuns(segments: [second, first], now: clock("2026-11-23T09:30:00"))
  check(
    gap.count == 4 && gap[1].kind == nil && gap[1].elapsed && gap[2].kind == nil && !gap[2].elapsed
      && nearlyEqual(gap[1].begin, 1.0 / 3.0)
      && nearlyEqual(gap[1].end, 0.5)
      && nearlyEqual(gap[2].end, 2.0 / 3.0),
    "空档按实际时长占位，且不依赖输入顺序"
  )

  let before = buildScreenEdgeRuns(segments: [first, second], now: clock("2026-11-23T07:00:00"))
  let after = buildScreenEdgeRuns(segments: [first, second], now: clock("2026-11-23T12:00:00"))
  check(
    before.allSatisfy { !$0.elapsed } && after.allSatisfy { $0.elapsed },
    "开始前全部为未到，结束后全部为已过"
  )
  check(buildScreenEdgeRuns(segments: [], now: noon).isEmpty, "无日程时没有虚构的分色时段")

  let stops = buildScreenEdgeBlendStops(runs)
  var ordered = !stops.isEmpty
  for i in 1..<stops.count {
    ordered = ordered && stops[i - 1].position <= stops[i].position
  }
  check(
    ordered && nearlyEqual(stops.first!.position, 0) && nearlyEqual(stops.last!.position, 1),
    "颜色渐变按时间顺序覆盖完整边框"
  )

  let shortBreak: [ScreenEdgeRun] = [
    ScreenEdgeRun(begin: 0, end: 0.2, kind: .focus, elapsed: true),
    ScreenEdgeRun(begin: 0.2, end: 0.202, kind: .shortBreak, elapsed: true),
    ScreenEdgeRun(begin: 0.202, end: 1, kind: .focus, elapsed: true),
  ]
  let shortStops = buildScreenEdgeBlendStops(shortBreak)
  check(
    shortStops.count == 6
      && nearlyEqual(shortStops[1].position, 0.1992)
      && nearlyEqual(shortStops[2].position, 0.2008)
      && nearlyEqual(shortStops[3].position, 0.2012)
      && shortStops[2].position < shortStops[3].position,
    "短课间两端的渐变收窄，保留可辨认的中段"
  )

  let sameColor = buildScreenEdgeBlendStops([
    ScreenEdgeRun(begin: 0, end: 0.5, kind: .focus, elapsed: true),
    ScreenEdgeRun(begin: 0.5, end: 1, kind: .focus, elapsed: true),
  ])
  check(sameColor.count == 2, "相同颜色的相邻时段不制造额外渐变")
  check(buildScreenEdgeBlendStops([]).isEmpty, "无日程时不生成渐变")

  let acrossCorner = [ScreenEdgeRun(begin: 0.3, end: 0.6, kind: .lunch, elapsed: true)]
  let projected = projectScreenEdgeRuns(acrossCorner, lengths: [100, 60, 90, 50])
  check(
    projected.count == 3
      && projected[0].side == 0 && nearlyEqual(projected[0].begin, 90) && nearlyEqual(projected[0].end, 100)
      && projected[1].side == 1 && nearlyEqual(projected[1].begin, 0) && nearlyEqual(projected[1].end, 60)
      && projected[2].side == 2 && nearlyEqual(projected[2].begin, 0) && nearlyEqual(projected[2].end, 20),
    "跨越边角的时段沿四边顺时针保持正确比例"
  )
}

let args = CommandLine.arguments
let root = args.count > 1 ? args[1] : ".."
print("ClassTime macOS 引擎测试，数据目录：\(root)")

testJson()
let (highSchool, university) = testPresets(root: root)
if !highSchool.segments.isEmpty {
  testTimeVectors(root: root, highSchool: highSchool)
  testEditVectors(root: root, highSchool: highSchool)
  testScope(root: root, highSchool: highSchool)
  testBoundariesAndDedupe(highSchool: highSchool)
  testEditOperations(highSchool: highSchool)
  testWidgetState(highSchool: highSchool)
  testScreenEdgeTimeline(highSchool: highSchool)
}
testConfigRoundtrip(highSchool: highSchool, university: university)
testTemplateCreation(highSchool: highSchool, university: university)
testConfigVersions(highSchool: highSchool)
testTimelineDrag()

print("\n\(totalChecks) 项检查，\(totalFailures) 项失败")
if totalFailures > 0 {
  exit(1)
}
