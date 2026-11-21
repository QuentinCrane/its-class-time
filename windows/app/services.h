// 应用级状态：配置、今日覆盖、当前快照与持久化。
// UI 与后台边界线程都通过这里取同一份“有效作息”，避免各算各的。
#pragma once

#include <filesystem>

namespace ClassTimeApp {

classtime::WallClock NowClock();
std::wstring ToWide(const std::string& text);
std::string ToUtf8(const std::wstring& text);

class Services {
 public:
  static Services& Instance();

  void Initialize();
  bool initialized() const { return initialized_; }

  // —— 只读状态 ——
  const classtime::AppConfig& config() const { return config_; }
  const classtime::Snapshot& snapshot() const { return snapshot_; }
  const std::vector<classtime::Segment>& effectiveSegments() const { return segments_; }
  bool usingTodayOverride() const { return usingOverride_; }
  std::string todayKey() const;
  const classtime::RuntimeState& runtimeState() const { return runtime_; }

  // 按当前显示范围过滤后的时段（仅展示用）。
  std::vector<classtime::Segment> VisibleSegments() const;

  // 后台边界线程使用：取当下的有效计划与修订号。
  struct PlanSnapshot {
    std::vector<classtime::Segment> segments;
    int revision = 1;
    std::string dateKey;
  };
  PlanSnapshot PlanFor(const classtime::WallClock& now) const;

  // —— 变更 ——
  enum class SaveScope { TodayOnly, Permanent };

  // 保存编辑结果。segments 为完整的新时段列表。
  bool ApplyScheduleEdit(const std::vector<classtime::Segment>& segments, SaveScope scope,
                         std::string* error);

  void UpdatePresentation(const classtime::PresentationSettings& presentation);
  void UpdateNotifications(const classtime::NotificationSettings& settings);
  void SetMuted(bool muted);
  void SetLaunchAtLogin(bool enabled);

  // 切换当前模板（保留各模板自身编辑结果）。
  bool SelectTemplate(const std::string& templateId, std::string* error);

  // 修改当前模板的生效星期。
  void SetActiveWeekdays(const std::vector<int>& weekdays);

  // 将选中模板恢复为随包预设（仅影响当前模板）。
  bool RestorePreset(std::string* error);

  // 撤销今天的临时覆盖。
  void ClearTodayOverride();

  // 重新按真实时钟定位，并通知界面。
  void Refresh();

  void Save();

  // 通知去重（运行态持久化）。
  bool WasNotified(const std::string& key) const;
  void MarkNotified(const std::string& key);
  void ForgetNotificationsForOtherDays(const std::string& dateKey);

  // —— UI 订阅 ——
  void SetChangeCallback(std::function<void()> callback) { changeCallback_ = std::move(callback); }
  void NotifyChanged();

  std::filesystem::path dataDirectory() const { return dataDir_; }
  std::filesystem::path configPath() const;
  std::filesystem::path runtimePath() const;

 private:
  Services() = default;
  ~Services() = default;

  void LoadOrCreate();
  void LoadPresets(std::vector<classtime::Schedule>* out);
  void PersistConfig();
  void PersistRuntime();
  void RecomputeLocked();

  mutable std::mutex mutex_;
  classtime::AppConfig config_;
  classtime::RuntimeState runtime_;
  std::vector<classtime::Segment> segments_;
  classtime::Snapshot snapshot_;
  bool usingOverride_ = false;
  bool initialized_ = false;
  std::filesystem::path dataDir_;
  std::function<void()> changeCallback_;
};

// 中文界面文案
std::wstring DayStateLabel(classtime::DayState state);
std::wstring SegmentKindLabel(classtime::SegmentKind kind);
std::wstring FormatClock(const classtime::TimeOfDay& time);
std::wstring FormatRange(const classtime::Segment& segment);
std::wstring FormatMinutes(int minutes);
std::wstring FormatPercent(double progress);

}  // namespace ClassTimeApp
