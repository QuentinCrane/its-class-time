#pragma once

#include <algorithm>

namespace ClassTimeApp::BoundaryWait {

constexpr int kDaySeconds = 24 * 60 * 60;
constexpr int kWatchdogSeconds = 60;
constexpr int kToleranceSeconds = 30;

inline int InitialWaitSeconds(int now) {
  return std::min(kWatchdogSeconds, kDaySeconds - now);
}

// The lower bound is exclusive. A normal midnight must include the event at second zero.
inline int LowerBound(int previous, int now, bool newDate, bool interrupted) {
  if (interrupted) return now;
  if (!newDate) return previous;
  constexpr int window = kWatchdogSeconds + kToleranceSeconds;
  return previous >= kDaySeconds - window && now < window ? -1 : now;
}

}  // namespace ClassTimeApp::BoundaryWait
