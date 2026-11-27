// 创建独立的用户模板；由应用服务负责持久化和刷新。
#pragma once

#include <utility>

#include "classtime/config.h"

namespace ClassTimeApp {

inline bool CreateTemplate(classtime::AppConfig& config, std::string name, bool copyCurrent,
                           std::string* error) {
  if (name.empty()) {
    if (error) *error = "请输入模板名称";
    return false;
  }

  classtime::Schedule schedule;
  if (copyCurrent) {
    const auto* current = classtime::SelectedTemplate(config);
    if (!current) {
      if (error) *error = "找不到当前模板";
      return false;
    }
    schedule = *current;
  } else {
    schedule.activeWeekdays = {1, 2, 3, 4, 5};
  }

  // ID 与显示名称分开；重名或删除后重新创建都不会覆盖已有模板。
  std::size_t suffix = 1;
  do {
    schedule.id = "custom-" + std::to_string(suffix++);
  } while (classtime::FindTemplate(config, schedule.id));
  schedule.name = std::move(name);
  config.selectedTemplateId = schedule.id;
  config.templates.push_back(std::move(schedule));
  classtime::BumpRevision(config);
  return true;
}

}  // namespace ClassTimeApp
