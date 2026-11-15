// Node test runner for the pure C++ parts of the framework core (target core_tests, built with
// -DWEBAR_CANVAS_BUILD_TESTS=ON). Run it with scripts/test-wasm.sh or `node build-docker.js --test`.
// No Filament, no tracker, no browser. The exit code is the number of failed checks.

#include "core/AREvents.h"

#include <cstdio>
#include <vector>

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      ++gFailures;                                                             \
      std::printf("  check failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                          \
  } while (0)

/** Runs one MarkerEventTracker::update() and records the events it reports. */
struct Events {
  std::vector<int> found;
  std::vector<int> lost;
};

static Events step(MarkerEventTracker &tracker, const std::vector<bool> &tracked) {
  Events e;
  tracker.update(tracked, [&](int i) { e.found.push_back(i); },
                 [&](int i) { e.lost.push_back(i); });
  return e;
}

static void no_events_while_never_tracked() {
  MarkerEventTracker t;
  for (int i = 0; i < 3; i++) {
    const Events e = step(t, {false});
    CHECK(e.found.empty() && e.lost.empty());
  }
}

static void found_once_then_lost_once() {
  MarkerEventTracker t;
  CHECK(step(t, {false}).found.empty());
  const Events found = step(t, {true});
  CHECK(found.found == std::vector<int>{0} && found.lost.empty());
  const Events still = step(t, {true});
  CHECK(still.found.empty() && still.lost.empty());
  const Events lost = step(t, {false});
  CHECK(lost.lost == std::vector<int>{0} && lost.found.empty());
  const Events after = step(t, {false});
  CHECK(after.found.empty() && after.lost.empty());
}

static void two_markers_same_frame() {
  MarkerEventTracker t;
  step(t, {false, false});
  const Events found = step(t, {true, true});
  CHECK((found.found == std::vector<int>{0, 1}) && found.lost.empty());
  const Events lost = step(t, {false, false});
  CHECK((lost.lost == std::vector<int>{0, 1}) && lost.found.empty());
}

static void reset_forgets_state() {
  MarkerEventTracker t;
  CHECK(step(t, {true}).found == std::vector<int>{0});
  t.reset();
  CHECK(step(t, {true}).found == std::vector<int>{0});
}

struct TestCase {
  const char *name;
  void (*run)();
};

static const TestCase kTests[] = {
    {"no_events_while_never_tracked", no_events_while_never_tracked},
    {"found_once_then_lost_once", found_once_then_lost_once},
    {"two_markers_same_frame", two_markers_same_frame},
    {"reset_forgets_state", reset_forgets_state},
};

int main() {
  for (const TestCase &test : kTests) {
    const int failuresBefore = gFailures;
    test.run();
    std::printf("%s %s\n", gFailures == failuresBefore ? "ok  " : "FAIL", test.name);
  }
  std::printf("%d checks, %d failed\n", gChecks, gFailures);
  return gFailures;
}
