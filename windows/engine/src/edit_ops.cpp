#include "classtime/edit_ops.h"

#include <algorithm>

namespace classtime {
namespace {

std::vector<std::string> ConflictIds(const std::vector<ValidationIssue>& issues) {
  std::vector<std::string> ids;
  for (const auto& issue : issues) {
    if (issue.segmentId.empty()) continue;
    if (std::find(ids.begin(), ids.end(), issue.segmentId) == ids.end()) {
      ids.push_back(issue.segmentId);
    }
  }
  return ids;
}

EditOutcome Failure(EditStatus status, std::string message, std::vector<std::string> conflicts = {}) {
  EditOutcome outcome;
  outcome.status = status;
  outcome.message = std::move(message);
  outcome.conflictIds = std::move(conflicts);
  return outcome;
}

EditOutcome ValidateAndReturn(std::vector<Segment> segments) {
  std::vector<ValidationIssue> issues;
  if (!ValidateSegments(segments, &issues)) {
    EditOutcome outcome = Failure(EditStatus::Overlap, "修改后时段冲突，请调整时间或选择顺延方案",
                                  ConflictIds(issues));
    outcome.segments = std::move(segments);
    return outcome;
  }
  EditOutcome outcome;
  outcome.segments = SortedByStart(segments);
  return outcome;
}

bool ShiftSegment(Segment& segment, int deltaMinutes, std::string* error) {
  const int newStart = segment.start.TotalMinutes() + deltaMinutes;
  const int newEnd = segment.end.TotalMinutes() + deltaMinutes;
  const auto start = TimeOfDay::FromTotalMinutes(newStart);
  const auto end = TimeOfDay::FromTotalMinutes(newEnd);
  if (!start.has_value() || !end.has_value()) {
    if (error != nullptr) *error = "顺延后超出当天 00:00–23:59";
    return false;
  }
  segment.start = *start;
  segment.end = *end;
  return true;
}

}  // namespace

EditOutcome ApplyEdit(const std::vector<Segment>& base, const EditRequest& request) {
  auto sorted = SortedByStart(base);
  size_t position = sorted.size();
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (sorted[i].id == request.segmentId) {
      position = i;
      break;
    }
  }
  if (position == sorted.size()) {
    return Failure(EditStatus::NotFound, "找不到要修改的时段");
  }

  Segment edited = sorted[position];
  if (request.label.has_value()) {
    edited.label = *request.label;
  }
  if (request.start.has_value()) {
    edited.start = *request.start;
  }

  if (request.durationMinutes.has_value()) {
    if (*request.durationMinutes <= 0) {
      return Failure(EditStatus::InvalidDuration, "时长必须大于 0 分钟");
    }
    const int endTotal = edited.start.TotalMinutes() + *request.durationMinutes;
    const auto end = TimeOfDay::FromTotalMinutes(endTotal);
    if (!end.has_value()) {
      return Failure(EditStatus::InvalidRange, "时长超出当天范围（结束不得晚于 23:59）");
    }
    edited.end = *end;
  } else if (request.end.has_value()) {
    edited.end = *request.end;
  }

  if (edited.end.TotalMinutes() <= edited.start.TotalMinutes()) {
    return Failure(EditStatus::InvalidRange, "结束时间必须晚于开始时间");
  }

  const int delta = edited.end.TotalMinutes() - sorted[position].end.TotalMinutes();
  sorted[position] = edited;

  if (request.strategy == ShiftStrategy::Shift && delta != 0) {
    for (size_t i = position + 1; i < sorted.size(); ++i) {
      std::string error;
      if (!ShiftSegment(sorted[i], delta, &error)) {
        return Failure(EditStatus::InvalidRange, error, {sorted[i].id});
      }
    }
  }

  return ValidateAndReturn(std::move(sorted));
}

EditOutcome InsertSegment(const std::vector<Segment>& base, const Segment& segment) {
  for (const auto& existing : base) {
    if (existing.id == segment.id) {
      return Failure(EditStatus::DuplicateId, "时段 ID 已存在", {segment.id});
    }
  }
  if (segment.end.TotalMinutes() <= segment.start.TotalMinutes()) {
    return Failure(EditStatus::InvalidRange, "结束时间必须晚于开始时间", {segment.id});
  }
  std::vector<Segment> segments = base;
  segments.push_back(segment);
  return ValidateAndReturn(std::move(segments));
}

EditOutcome RemoveSegment(const std::vector<Segment>& base, std::string_view id) {
  std::vector<Segment> segments;
  segments.reserve(base.size());
  bool found = false;
  for (const auto& segment : base) {
    if (segment.id == id) {
      found = true;
      continue;
    }
    segments.push_back(segment);
  }
  if (!found) return Failure(EditStatus::NotFound, "找不到要删除的时段");
  return ValidateAndReturn(std::move(segments));
}

}  // namespace classtime
