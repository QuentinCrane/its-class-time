// 作息编辑：时长/起止修改、顺延或保持、增删，并在保存前统一校验。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "classtime/schedule.h"

namespace classtime {

enum class ShiftStrategy { Shift, Keep };

struct EditRequest {
  std::string segmentId;
  std::optional<TimeOfDay> start;
  std::optional<TimeOfDay> end;
  std::optional<int> durationMinutes;  // 优先于 end：end = start + duration
  std::optional<std::string> label;
  ShiftStrategy strategy = ShiftStrategy::Shift;
};

enum class EditStatus { Ok, NotFound, InvalidDuration, InvalidRange, Overlap, DuplicateId };

struct EditOutcome {
  EditStatus status = EditStatus::Ok;
  std::vector<Segment> segments;      // 成功时为完整的新时段列表（已按开始时间排序）
  std::vector<std::string> conflictIds;
  std::string message;                // 可直接展示的中文原因

  bool ok() const noexcept { return status == EditStatus::Ok; }
};

EditOutcome ApplyEdit(const std::vector<Segment>& base, const EditRequest& request);

EditOutcome InsertSegment(const std::vector<Segment>& base, const Segment& segment);

EditOutcome RemoveSegment(const std::vector<Segment>& base, std::string_view id);

}  // namespace classtime
