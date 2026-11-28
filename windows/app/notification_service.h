// 系统即时通知：到点调用 AppNotificationManager.Show，不预排系统计划通知。
#pragma once

namespace ClassTimeApp {

// 由当前边界事件统一组装的通知载荷，同时服务系统通知与可选视觉到点动效。
struct BoundaryAlert {
  std::wstring title;
  std::wstring body;
  std::wstring stageBadge;
  std::wstring rangeText;
  classtime::TimeOfDay clockTime{8, 0};
  classtime::SegmentKind accentKind = classtime::SegmentKind::Focus;
};

std::optional<BoundaryAlert> ComposeBoundaryAlert(
    const std::vector<classtime::BoundaryEvent>& events);

class NotificationService {
 public:
  void Register(std::function<void()> onActivated);
  void Unregister();

  bool ShowBoundary(const BoundaryAlert& alert);

 private:
  bool registered_ = false;
};

// 设置页直接查询/测试系统通知，无需持有服务实例。
struct NotificationPermission {
  bool enabled;
  const wchar_t* label;
};
NotificationPermission QueryNotificationPermission();
bool ShowTestSystemNotification();

}  // namespace ClassTimeApp
