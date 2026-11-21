// 系统即时通知：到点调用 AppNotificationManager.Show，不预排系统计划通知。
#pragma once

namespace ClassTimeApp {

class NotificationService {
 public:
  void Register(std::function<void()> onActivated);
  void Unregister();

  // 返回是否真正发送（可能被开关/权限/合并规则过滤）。
  bool ShowBoundary(const std::vector<classtime::BoundaryEvent>& events,
                    const std::vector<classtime::Segment>& plan);

  bool ShowTest();

  // 系统通知权限状态（用于设置页提示）。
  std::wstring PermissionLabel() const;
  bool SystemNotificationsEnabled() const;

 private:
  bool registered_ = false;
};

// 设置页直接查询/测试系统通知，无需持有服务实例。
std::wstring QueryNotificationPermissionLabel();
bool QuerySystemNotificationsEnabled();
bool ShowTestSystemNotification();

}  // namespace ClassTimeApp
