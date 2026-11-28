#include "pch.h"
#include "boundary_monitor.h"
#include "boundary_wait.h"
#include "services.h"

namespace ClassTimeApp {
namespace {

struct Candidate {
  int seconds = 0;
  classtime::BoundaryEvent event;
};

std::vector<Candidate> CollectBoundaries(const std::vector<classtime::Segment>& segments) {
  std::vector<Candidate> candidates;
  int focusOrdinal = 0;
  auto sorted = segments;
  std::stable_sort(sorted.begin(), sorted.end(), [](const classtime::Segment& lhs,
                                                   const classtime::Segment& rhs) {
    return lhs.start.TotalMinutes() < rhs.start.TotalMinutes();
  });
  for (const auto& segment : sorted) {
    if (segment.kind == classtime::SegmentKind::Focus) ++focusOrdinal;
    classtime::SegmentRef ref;
    ref.id = segment.id;
    ref.label = segment.label;
    ref.kind = segment.kind;
    ref.start = segment.start;
    ref.end = segment.end;
    ref.focusOrdinal = segment.kind == classtime::SegmentKind::Focus ? focusOrdinal : 0;
    candidates.push_back({segment.start.TotalMinutes() * 60,
                          {classtime::StartBoundaryKind(segment.kind), ref, {}}});
    candidates.push_back(
        {segment.end.TotalMinutes() * 60, {classtime::EndBoundaryKind(segment.kind), ref, {}}});
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& lhs, const Candidate& rhs) {
                     return lhs.seconds < rhs.seconds;
                   });
  return candidates;
}

int SecondsOfDay(const classtime::WallClock& clock) {
  return clock.hour * 3600 + clock.minute * 60 + clock.second;
}

}  // namespace

void BoundaryMonitor::Start(FireCallback onFire) {
  Stop();
  {
    std::lock_guard<std::mutex> guard(mutex_);
    stop_ = false;
    refresh_ = false;
    onFire_ = std::move(onFire);
  }
  thread_ = std::thread([this] { Run(); });
}

void BoundaryMonitor::Stop() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!thread_.joinable()) return;
    stop_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> guard(mutex_);
  onFire_ = nullptr;
}

void BoundaryMonitor::RequestRefresh() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    refresh_ = true;
  }
  wake_.notify_all();
}

void BoundaryMonitor::Run() {
  // 启动时以“现在”为锚点，绝不补发启动之前已经过去的边界。
  auto anchor = NowClock();
  int lastCheckedSeconds = SecondsOfDay(anchor);
  std::string activeDate = anchor.DateKey();
  Services::Instance().ForgetNotificationsForOtherDays(activeDate);

  constexpr auto maxGap = std::chrono::seconds(BoundaryWait::kWatchdogSeconds + BoundaryWait::kToleranceSeconds);
  auto lastSample = std::chrono::steady_clock::now();

  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stop_) return;
      if (refresh_) {
        refresh_ = false;
        auto now = NowClock();
        lastCheckedSeconds = SecondsOfDay(now);
        activeDate = now.DateKey();
        lastSample = std::chrono::steady_clock::now();
      }
      lock.unlock();

      auto now = NowClock();
      auto plan = Services::Instance().PlanFor(now);
      const auto candidates = CollectBoundaries(plan.segments);

      const int nowSeconds = SecondsOfDay(now);
      const auto sampledAt = std::chrono::steady_clock::now();
      const bool newDate = now.DateKey() != activeDate;
      lastCheckedSeconds = BoundaryWait::LowerBound(lastCheckedSeconds, nowSeconds, newDate,
                                                     sampledAt - lastSample > maxGap);
      lastSample = sampledAt;
      if (newDate) {
        activeDate = now.DateKey();
        Services::Instance().ForgetNotificationsForOtherDays(activeDate);
        Services::Instance().Refresh();
      }

      std::vector<Candidate> due;
      for (const auto& candidate : candidates) {
        if (candidate.seconds <= lastCheckedSeconds || candidate.seconds > nowSeconds) continue;
        if (nowSeconds - candidate.seconds > BoundaryWait::kToleranceSeconds) continue;  // 睡眠/启动后的过期事件
        auto event = candidate.event;
        event.at = now;
        event.at.hour = candidate.seconds / 3600;
        event.at.minute = (candidate.seconds % 3600) / 60;
        event.at.second = candidate.seconds % 60;
        due.push_back({candidate.seconds, event});
      }

      if (!due.empty()) {
        FiredBatch batch;
        batch.revision = plan.revision;
        batch.dateKey = plan.dateKey;
        batch.plan = plan.segments;
        for (const auto& candidate : due) {
          const auto key = classtime::EventKey(plan.dateKey, candidate.event.segment.id,
                                               candidate.event.kind, plan.revision);
          if (Services::Instance().WasNotified(key)) continue;
          Services::Instance().MarkNotified(key);
          batch.events.push_back(candidate.event);
        }
        if (!batch.events.empty()) {
          FireCallback callback;
          {
            std::lock_guard<std::mutex> guard(mutex_);
            callback = onFire_;
          }
          if (callback) callback(batch);
        }
      }

      lastCheckedSeconds = nowSeconds;

      // 计算下一次唤醒：最近边界，最多 60 秒（用于发现系统时间调整）。
      // Midnight changes the effective plan even when today's last class has ended.
      auto nextBoundary = std::chrono::seconds(BoundaryWait::InitialWaitSeconds(nowSeconds));
      for (const auto& candidate : candidates) {
        if (candidate.seconds > nowSeconds) {
          const auto delta = std::chrono::seconds(candidate.seconds - nowSeconds);
          if (delta < nextBoundary) nextBoundary = delta;
          break;
        }
      }

      std::unique_lock<std::mutex> waitLock(mutex_);
      wake_.wait_for(waitLock, nextBoundary, [this] { return stop_ || refresh_; });
      if (stop_) return;
    }
  }
}

}  // namespace ClassTimeApp
