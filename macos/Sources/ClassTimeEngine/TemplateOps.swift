// 创建独立的用户模板；由应用服务负责持久化和刷新（与 Windows template_ops.h 规则严格对等）。
import Foundation

@discardableResult
public func createTemplate(
  _ config: inout AppConfig,
  name: String,
  copyCurrent: Bool,
  error: inout String
) -> Bool {
  error = ""
  guard !name.isEmpty else {
    error = "请输入模板名称"
    return false
  }

  var schedule: Schedule
  if copyCurrent {
    guard let current = selectedTemplate(config) else {
      error = "找不到当前模板"
      return false
    }
    schedule = current
  } else {
    schedule = Schedule(
      schemaVersion: 1,
      id: "",
      name: "",
      activeWeekdays: [1, 2, 3, 4, 5],
      segments: []
    )
  }

  // ID 与显示名称分开；重名或删除后重新创建都不会覆盖已有模板。
  var suffix = 1
  var candidateId = ""
  repeat {
    candidateId = "custom-\(suffix)"
    suffix += 1
  } while findTemplate(config, id: candidateId) != nil

  schedule.id = candidateId
  schedule.name = name
  config.selectedTemplateId = schedule.id
  config.templates.append(schedule)
  bumpRevision(&config)
  return true
}
