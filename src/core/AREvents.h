#ifndef AR_EVENTS_H
#define AR_EVENTS_H

#include <cstddef>
#include <vector>

/**
 * Turns the per-frame tracked state of each marker into found / lost events (#2).
 *
 * Call update() once per processed camera frame with one flag per loaded marker. It reports, in
 * index order, the markers that became tracked (onFound) and those that stopped being tracked
 * (onLost) since the previous call. A marker never tracked before counts as lost, so nothing is
 * reported until it is found. The events follow the raw tracking state (no debounce).
 */
class MarkerEventTracker {
public:
  template <class Found, class Lost>
  void update(const std::vector<bool> &tracked, Found onFound, Lost onLost) {
    if (previous.size() < tracked.size()) {
      previous.resize(tracked.size(), false);
    }
    for (std::size_t i = 0; i < tracked.size(); ++i) {
      if (tracked[i] && !previous[i]) {
        onFound(static_cast<int>(i));
      } else if (!tracked[i] && previous[i]) {
        onLost(static_cast<int>(i));
      }
      previous[i] = tracked[i];
    }
  }

  // Forgets every marker's state, e.g. after the markers are reloaded.
  void reset() { previous.clear(); }

private:
  std::vector<bool> previous;
};

/**
 * Seconds between two ticks for update(deltaTime): 0 on the first tick (previousMs < 0) or if
 * the clock went back, at most maxSeconds (a background tab or a debugger pause would otherwise
 * make animations jump).
 */
inline double computeDeltaSeconds(double previousMs, double nowMs, double maxSeconds = 0.1) {
  if (previousMs < 0.0 || nowMs <= previousMs) return 0.0;
  const double seconds = (nowMs - previousMs) / 1000.0;
  return seconds < maxSeconds ? seconds : maxSeconds;
}

#endif // AR_EVENTS_H
