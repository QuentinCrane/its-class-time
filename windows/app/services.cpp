#include "pch.h"
#include "services.h"
#include "template_ops.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.h>

namespace ClassTimeApp {
namespace {

using winrt::Windows::Storage::ApplicationData;
using winrt::Windows::ApplicationModel::Package;

std::string ReadAllBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

bool WriteAtomic(const std::filesystem::path& path, const std::string& content) {
  if (ReadAllBytes(path) == content) return true;

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path temporary = path.wstring() + L".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  stream.close();
  if (stream && MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  std::filesystem::remove(temporary, ec);
  return false;
}

bool BackupCorrupt(const std::filesystem::path& path) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t suffix[32];
  swprintf_s(suffix, L".corrupt-%04d%02d%02d-%02d%02d%02d", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
  // 同一秒内多次恢复时，不能覆盖之前保留下来的备份。
  for (int attempt = 0; attempt < 1000; ++attempt) {
    std::filesystem::path backup = path.wstring() + suffix;
    if (attempt > 0) backup += L"-" + std::to_wstring(attempt);
    std::error_code ec;
    if (std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, ec)) {
      return true;
    }
    if (ec != std::errc::file_exists) return false;
  }
  return false;
}

classtime::Schedule FallbackTemplate() {
  classtime::Schedule schedule;
  schedule.id = "custom";
  schedule.name = "空白模板";
  schedule.activeWeekdays = {1, 2, 3, 4, 5};
  return schedule;
}

}  // namespace

classtime::WallClock NowClock() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  classtime::WallClock clock;
  clock.year = now.wYear;
  clock.month = now.wMonth;
  clock.day = now.wDay;
  clock.hour = now.wHour;
  clock.minute = now.wMinute;
  clock.second = now.wSecond;
  return clock;
}

std::wstring ToWide(const std::string& text) { return classtime::json::Utf8ToWide(text); }
std::string ToUtf8(const std::wstring& text) { return classtime::json::WideToUtf8(text); }

Services& Services::Instance() {
  static Services instance;
  return instance;
}

bool Services::Initialize() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (initialized_) return true;

  // 允许用环境变量指定独立数据目录（技术预览用，避免污染正式配置）。
  wchar_t customDir[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"CLASSTIME_DATA_DIR", customDir, MAX_PATH) > 0) {
    dataDir_ = std::filesystem::path(customDir);
  } else {
    // 打包运行时使用 LocalState；解包运行时回退到 %LOCALAPPDATA%。
    try {
      dataDir_ = std::filesystem::path(ApplicationData::Current().LocalFolder().Path().c_str());
    } catch (...) {
      wchar_t buffer[MAX_PATH]{};
      const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
      dataDir_ = length > 0 ? std::filesystem::path(buffer) / L"ClassTime"
                            : std::filesystem::temp_directory_path() / L"ClassTime";
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(dataDir_, ec);

  runtime_ = classtime::ParseRuntimeState(ReadAllBytes(runtimePath()));

  const auto configFile = configPath();
  bool loaded = false;
  const bool configExists = std::filesystem::exists(configFile, ec);
  if (ec) return false;
  if (configExists) {
    std::string error;
    const auto parsed = classtime::ParseConfig(ReadAllBytes(configFile), &error);
    if (parsed.has_value()) {
      config_ = *parsed;
      loaded = true;
    } else if (!BackupCorrupt(configFile)) {
      // 在原文件得到可靠备份之前，绝不能写入默认配置。
      return false;
    }
  }

  if (!loaded) {
    std::vector<classtime::Schedule> presets;
    LoadPresets(&presets);
    config_ = classtime::AppConfig{};
    config_.templates = presets;
    config_.selectedTemplateId = presets.empty() ? "custom" : presets.front().id;
    config_.revision = 1;
  }

  const auto overrideCount = config_.dailyOverrides.size();
  classtime::PruneOverrides(config_, todayKey());
  // A restart with the same plan keeps both the revision and the persisted notification keys.
  if (!loaded || config_.dailyOverrides.size() != overrideCount) classtime::BumpRevision(config_);
  RecomputeLocked();
  if (!PersistConfig()) return false;
  initialized_ = true;
  return true;
}

void Services::LoadPresets(std::vector<classtime::Schedule>* out) {
  out->clear();
  std::filesystem::path presetDir;
  try {
    presetDir = std::filesystem::path(Package::Current().InstalledLocation().Path().c_str()) /
                L"presets";
  } catch (...) {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    presetDir = std::filesystem::path(exePath).parent_path() / L"presets";
  }

  for (const wchar_t* name : {L"high-school.json", L"university.json"}) {
    const std::string text = ReadAllBytes(presetDir / name);
    if (text.empty()) continue;
    std::string error;
    const auto schedule = classtime::ParsePreset(text, &error);
    if (schedule.has_value()) out->push_back(*schedule);
  }
  if (out->empty()) out->push_back(FallbackTemplate());
}

bool Services::PersistConfig() { return WriteAtomic(configPath(), classtime::SerializeConfig(config_)); }

void Services::PersistRuntime() {
  WriteAtomic(runtimePath(), classtime::SerializeRuntimeState(runtime_));
}

std::filesystem::path Services::configPath() const { return dataDir_ / L"config.json"; }
std::filesystem::path Services::runtimePath() const { return dataDir_ / L"runtime-state.json"; }

std::string Services::todayKey() const { return NowClock().DateKey(); }

void Services::RecomputeLocked() {
  const auto now = NowClock();
  segments_ = classtime::EffectiveSegments(config_, now, &usingOverride_);
  snapshot_ = classtime::Evaluate(segments_, now);
}

std::vector<classtime::Segment> Services::VisibleSegments() const {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto now = NowClock();
  const auto scope = config_.presentation.scope == classtime::PresentationSettings::Scope::AllDay
                         ? classtime::Scope::AllDay
                         : classtime::Scope::CurrentBlock;
  const auto indices = classtime::VisibleSegmentIndices(segments_, now, scope);
  std::vector<classtime::Segment> visible;
  visible.reserve(indices.size());
  for (const size_t index : indices) {
    if (index < segments_.size()) visible.push_back(segments_[index]);
  }
  return visible;
}

Services::PlanSnapshot Services::PlanFor(const classtime::WallClock& now) const {
  std::lock_guard<std::mutex> guard(mutex_);
  PlanSnapshot plan;
  bool fromOverride = false;
  plan.segments = classtime::EffectiveSegments(config_, now, &fromOverride);
  plan.revision = config_.revision;
  plan.dateKey = now.DateKey();
  return plan;
}

bool Services::ApplyScheduleEdit(const std::vector<classtime::Segment>& segments, SaveScope scope,
                                 std::string* error) {
  if (!classtime::ValidateSegments(segments, nullptr)) {
    if (error != nullptr) *error = "时段存在重叠或时间不合法";
    return false;
  }
  {
    std::lock_guard<std::mutex> guard(mutex_);
    classtime::AppConfig next = config_;
    if (scope == SaveScope::TodayOnly) {
      classtime::SetOverride(next, todayKey(), segments);
    } else {
      classtime::Schedule* schedule =
          classtime::FindTemplateMutable(next, next.selectedTemplateId);
      if (schedule == nullptr) {
        if (error != nullptr) *error = "找不到当前模板";
        return false;
      }
      // 只改常规模板；今日覆盖不会在这里被悄悄改写。
      schedule->segments = segments;
    }
    classtime::BumpRevision(next);
    config_ = std::move(next);
    PersistConfig();
    RecomputeLocked();
    PersistRuntime();
  }
  NotifyChanged();
  return true;
}

void Services::UpdatePresentation(const classtime::PresentationSettings& presentation) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    config_.presentation = presentation;
    PersistConfig();
    RecomputeLocked();
  }
  NotifyChanged(Change::Presentation);
}

void Services::UpdateNotifications(const classtime::NotificationSettings& settings) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    config_.notifications = settings;
    PersistConfig();
  }
  NotifyChanged();
}

void Services::SetMuted(bool muted) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (config_.notifications.muted == muted) return;
    config_.notifications.muted = muted;
    PersistConfig();
  }
  NotifyChanged();
}

void Services::SetLaunchAtLogin(bool enabled) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    config_.launchAtLogin = enabled;
    PersistConfig();
  }
  NotifyChanged();
}

bool Services::AddTemplate(const std::string& name, bool copyCurrent, std::string* error) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto next = config_;
    if (!CreateTemplate(next, name, copyCurrent, error)) return false;
    if (!WriteAtomic(configPath(), classtime::SerializeConfig(next))) {
      if (error) *error = "无法保存配置，请检查配置文件夹的写入权限";
      return false;
    }
    config_ = std::move(next);
    RecomputeLocked();
  }
  NotifyChanged();
  return true;
}

bool Services::SelectTemplate(const std::string& templateId, std::string* error) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (classtime::FindTemplate(config_, templateId) == nullptr) {
      if (error != nullptr) *error = "模板不存在";
      return false;
    }
    if (config_.selectedTemplateId == templateId) return true;
    config_.selectedTemplateId = templateId;
    classtime::BumpRevision(config_);
    PersistConfig();
    RecomputeLocked();
  }
  NotifyChanged();
  return true;
}

bool Services::SetActiveWeekdays(const std::vector<int>& weekdays, std::string* error) {
  auto days = weekdays;
  std::sort(days.begin(), days.end());
  days.erase(std::unique(days.begin(), days.end()), days.end());
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto next = config_;
    auto* schedule = classtime::FindTemplateMutable(next, next.selectedTemplateId);
    if (!schedule) {
      if (error) *error = "找不到当前模板";
      return false;
    }
    if (schedule->activeWeekdays == days) return true;
    schedule->activeWeekdays = std::move(days);
    if (!classtime::ValidateSchedule(*schedule, nullptr)) {
      if (error) *error = "无法应用所选星期，请检查模板的时段和重复设置";
      return false;
    }
    classtime::BumpRevision(next);
    if (!WriteAtomic(configPath(), classtime::SerializeConfig(next))) {
      if (error) *error = "无法保存重复星期，请检查配置文件夹的写入权限";
      return false;
    }
    config_ = std::move(next);
    RecomputeLocked();
  }
  NotifyChanged();
  return true;
}

void Services::RenameTemplate(const std::string& templateId, const std::string& name) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (name.empty()) return;
    classtime::Schedule* schedule =
        classtime::FindTemplateMutable(config_, templateId);
    if (schedule == nullptr || schedule->name == name) return;
    schedule->name = name;
    classtime::BumpRevision(config_);
    PersistConfig();
    RecomputeLocked();
  }
  NotifyChanged();
}

bool Services::RestorePreset(std::string* error) {
  std::vector<classtime::Schedule> presets;
  LoadPresets(&presets);
  {
    std::lock_guard<std::mutex> guard(mutex_);
    classtime::Schedule* target = classtime::FindTemplateMutable(config_, config_.selectedTemplateId);
    if (target == nullptr) {
      if (error != nullptr) *error = "当前模板不存在";
      return false;
    }
    const auto match = std::find_if(presets.begin(), presets.end(),
                                    [this](const classtime::Schedule& item) {
                                      return item.id == config_.selectedTemplateId;
                                    });
    if (match == presets.end()) {
      if (error != nullptr) *error = "这是自定义模板，没有可恢复的随包预设";
      return false;
    }
    target->segments = match->segments;
    target->name = match->name;
    target->activeWeekdays = match->activeWeekdays;
    classtime::BumpRevision(config_);
    PersistConfig();
    RecomputeLocked();
  }
  NotifyChanged();
  return true;
}

void Services::ClearTodayOverride() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    classtime::RemoveOverride(config_, todayKey());
    classtime::BumpRevision(config_);
    PersistConfig();
    RecomputeLocked();
  }
  NotifyChanged();
}

void Services::Refresh() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    classtime::PruneOverrides(config_, todayKey());
    RecomputeLocked();
  }
  NotifyChanged();
}

void Services::Save() {
  std::lock_guard<std::mutex> guard(mutex_);
  PersistConfig();
  PersistRuntime();
}

bool Services::WasNotified(const std::string& key) const {
  std::lock_guard<std::mutex> guard(mutex_);
  return classtime::HasFired(runtime_, key);
}

void Services::MarkNotified(const std::string& key) {
  std::lock_guard<std::mutex> guard(mutex_);
  classtime::MarkFired(runtime_, key);
  PersistRuntime();
}

void Services::ForgetNotificationsForOtherDays(const std::string& dateKey) {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<std::string> kept;
  kept.reserve(runtime_.firedKeys.size());
  for (const auto& key : runtime_.firedKeys) {
    if (key.compare(0, dateKey.size(), dateKey) == 0) kept.push_back(key);
  }
  runtime_.firedKeys = std::move(kept);
  runtime_.lastActiveDate = dateKey;
  PersistRuntime();
}

void Services::NotifyChanged(Change change) {
  ChangeCallback callback;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    callback = changeCallback_;
  }
  if (callback) callback(change);
}

std::wstring DayStateLabel(classtime::DayState state) {
  switch (state) {
    case classtime::DayState::Idle: return L"空闲";
    case classtime::DayState::Focus: return L"上课中";
    case classtime::DayState::Break: return L"课间";
    case classtime::DayState::LongBreak: return L"长休息";
    case classtime::DayState::Lunch: return L"午休";
  }
  return L"空闲";
}

std::wstring SegmentKindLabel(classtime::SegmentKind kind) {
  switch (kind) {
    case classtime::SegmentKind::Focus: return L"上课";
    case classtime::SegmentKind::Break: return L"课间";
    case classtime::SegmentKind::LongBreak: return L"长休息";
    case classtime::SegmentKind::Lunch: return L"午休";
  }
  return L"时段";
}

std::wstring FormatClock(const classtime::TimeOfDay& time) {
  wchar_t buffer[8];
  swprintf_s(buffer, L"%02d:%02d", time.hour, time.minute);
  return buffer;
}

std::wstring FormatRange(const classtime::Segment& segment) {
  return FormatClock(segment.start) + L"–" + FormatClock(segment.end);
}

std::wstring FormatMinutes(int minutes) { return std::to_wstring(minutes) + L" 分钟"; }

std::wstring FormatPercent(double progress) {
  const int percent = static_cast<int>(std::lround(progress * 100.0));
  return std::to_wstring(percent) + L"%";
}

}  // namespace ClassTimeApp
