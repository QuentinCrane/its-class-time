#include "pch.h"
#include "notification_service.h"
#include "services.h"

using namespace winrt::Microsoft::Windows::AppNotifications;
using namespace winrt::Microsoft::Windows::AppNotifications::Builder;

namespace ClassTimeApp {
namespace {

struct LegacyMessageUnused {
  std::wstring title;
  std::wstring body;
};

std::wstring OrdinalText(const classtime::SegmentRef& segment) {
  return L"第 " + std::to_wstring(segment.focusOrdinal) + L" 节";
}

std::wstring RangeText(const classtime::SegmentRef& segment) {
  return FormatClock(segment.start) + L"–" + FormatClock(segment.end);
}

int Priority(const classtime::BoundaryKind kind) {
  switch (kind) {
    case classtime::BoundaryKind::FocusStart: return 5;
    case classtime::BoundaryKind::LunchStart: return 4;
    case classtime::BoundaryKind::LongBreakStart: return 3;
    case classtime::BoundaryKind::BreakStart: return 2;
    case classtime::BoundaryKind::FocusEnd: return 5;
    case classtime::BoundaryKind::LunchEnd: return 4;
    case classtime::BoundaryKind::LongBreakEnd: return 3;
    case classtime::BoundaryKind::BreakEnd: return 2;
  }
  return 0;
}

bool IsStart(const classtime::BoundaryKind kind) {
  return kind == classtime::BoundaryKind::FocusStart ||
         kind == classtime::BoundaryKind::BreakStart ||
         kind == classtime::BoundaryKind::LongBreakStart ||
         kind == classtime::BoundaryKind::LunchStart;
}

bool Allowed(const classtime::BoundaryKind kind) {
  const auto& settings = Services::Instance().config().notifications;
  if (!settings.enabled || settings.muted) return false;
  switch (kind) {
    case classtime::BoundaryKind::FocusStart:
    case classtime::BoundaryKind::FocusEnd:
      return settings.focusBoundaries;
    case classtime::BoundaryKind::LunchStart:
    case classtime::BoundaryKind::LunchEnd:
      return settings.lunchBoundaries;
    case classtime::BoundaryKind::BreakStart:
    case classtime::BoundaryKind::BreakEnd:
    case classtime::BoundaryKind::LongBreakStart:
    case classtime::BoundaryKind::LongBreakEnd:
      return settings.restBoundaries;
  }
  return true;
}

std::wstring RestText(const classtime::SegmentRef& segment) {
  const int minutes = segment.DurationMinutes();
  switch (segment.kind) {
    case classtime::SegmentKind::Lunch: return L"午休时间，按今天的安排稍作休息";
    case classtime::SegmentKind::LongBreak: return L"长休息 " + FormatMinutes(minutes);
    case classtime::SegmentKind::Break: return L"课间休息 " + FormatMinutes(minutes);
    default: return L"";
  }
}

// 同一秒发生“下课 + 课间开始”时只发一条，避免刷屏。
std::optional<BoundaryAlert> Compose(const std::vector<classtime::BoundaryEvent>& rawEvents) {
  std::vector<classtime::BoundaryEvent> events;
  for (const auto& event : rawEvents) {
    if (Allowed(event.kind)) events.push_back(event);
  }
  if (events.empty()) return std::nullopt;

  const classtime::BoundaryEvent* focusEnd = nullptr;
  const classtime::BoundaryEvent* start = nullptr;
  const classtime::BoundaryEvent* end = nullptr;
  for (const auto& event : events) {
    if (event.kind == classtime::BoundaryKind::FocusEnd) focusEnd = &event;
    if (IsStart(event.kind)) {
      if (start == nullptr || Priority(event.kind) > Priority(start->kind)) start = &event;
    } else if (end == nullptr || Priority(event.kind) > Priority(end->kind)) {
      end = &event;
    }
  }

  BoundaryAlert message;
  if (start != nullptr) {
    const auto& segment = start->segment;
    message.clockTime = segment.start;
    message.accentKind = segment.kind;
    message.rangeText = RangeText(segment);
    switch (start->kind) {
      case classtime::BoundaryKind::FocusStart:
        message.stageBadge = OrdinalText(segment);
        message.title = L"开始啦";
        message.body = OrdinalText(segment) + L"开始了（" + RangeText(segment) + L"）";
        break;
      case classtime::BoundaryKind::LunchStart:
        message.stageBadge = L"午休时间";
        message.title = focusEnd != nullptr ? L"下课了" : L"午休时间";
        message.body = focusEnd != nullptr
                           ? OrdinalText(focusEnd->segment) + L"结束，" + RestText(segment)
                           : RestText(segment);
        break;
      case classtime::BoundaryKind::BreakStart:
      case classtime::BoundaryKind::LongBreakStart:
        message.stageBadge = segment.kind == classtime::SegmentKind::LongBreak
                                 ? L"长休息" : L"课间休息";
        message.title = focusEnd != nullptr ? L"下课了" : L"休息一下";
        message.body = focusEnd != nullptr
                           ? OrdinalText(focusEnd->segment) + L"结束，" + RestText(segment)
                           : RestText(segment);
        break;
      default:
        break;
    }
    return message;
  }

  if (focusEnd != nullptr) {
    message.clockTime = focusEnd->segment.end;
    message.accentKind = focusEnd->segment.kind;
    message.stageBadge = OrdinalText(focusEnd->segment) + L"结束";
    message.rangeText = RangeText(focusEnd->segment);
    message.title = L"下课了";
    message.body = OrdinalText(focusEnd->segment) + L"结束，记得活动一下";
    return message;
  }
  if (end != nullptr) {
    message.clockTime = end->segment.end;
    message.accentKind = end->segment.kind;
    message.rangeText = RangeText(end->segment);
    switch (end->kind) {
      case classtime::BoundaryKind::LunchEnd:
        message.stageBadge = L"午休结束";
        message.title = L"午休结束";
        message.body = L"接下来准备进入下午的安排";
        break;
      case classtime::BoundaryKind::LongBreakEnd:
        message.stageBadge = L"休息结束";
        message.title = L"休息结束";
        message.body = L"准备好下一段安排";
        break;
      case classtime::BoundaryKind::BreakEnd:
        message.stageBadge = L"课间结束";
        message.title = L"继续加油";
        message.body = L"课间结束，准备好下一节";
        break;
      default:
        return std::nullopt;
    }
    return message;
  }
  return std::nullopt;
}

}  // namespace

std::optional<BoundaryAlert> ComposeBoundaryAlert(
    const std::vector<classtime::BoundaryEvent>& events) {
  return Compose(events);
}

void NotificationService::Register(std::function<void()> onActivated) {
  try {
    auto manager = AppNotificationManager::Default();
    if (onActivated) {
      manager.NotificationInvoked(
          [callback = std::move(onActivated)](auto const&, AppNotificationActivatedEventArgs const&) {
            if (callback) callback();
          });
    }
    manager.Register();
    registered_ = true;
  } catch (...) {
    registered_ = false;
  }
}

void NotificationService::Unregister() {
  if (!registered_) return;
  try {
    AppNotificationManager::Default().Unregister();
  } catch (...) {
  }
  registered_ = false;
}

bool NotificationService::ShowBoundary(const BoundaryAlert& alert) {
  if (!registered_ || !QueryNotificationPermission().enabled) return false;
  try {
    AppNotificationBuilder builder;
    builder.AddText(alert.title);
    builder.AddText(alert.body);
    const auto notification = builder.BuildNotification();
    AppNotificationManager::Default().Show(notification);
    return true;
  } catch (...) {
    return false;
  }
}

NotificationPermission QueryNotificationPermission() {
  try {
    switch (AppNotificationManager::Default().Setting()) {
      case AppNotificationSetting::Enabled:
        return {true, L"系统通知已允许"};
      case AppNotificationSetting::DisabledForApplication:
        return {false, L"系统已关闭本应用的通知"};
      case AppNotificationSetting::DisabledForUser:
        return {false, L"系统通知当前被用户关闭"};
      case AppNotificationSetting::DisabledByGroupPolicy:
        return {false, L"通知被组织策略关闭"};
      case AppNotificationSetting::DisabledByManifest:
        return {false, L"应用清单未声明通知能力"};
      default:
        return {false, L"通知状态未知"};
    }
  } catch (...) {
    return {false, L"尚未注册通知"};
  }
}

bool ShowTestSystemNotification() {
  if (!QueryNotificationPermission().enabled) return false;
  try {
    AppNotificationBuilder builder;
    builder.AddText(L"通知正常");
    builder.AddText(L"到边界时，会像这样提醒你");
    AppNotificationManager::Default().Show(builder.BuildNotification());
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace ClassTimeApp
