// Node test runner for the KPM worker module's entry points (src/worker/kpm_worker.cpp), the
// part of the worker that does the matching. Built with -DWEBAR_CANVAS_BUILD_TESTS=ON and run by
// scripts/test-wasm.sh. WebARKitLib's tests/data is embedded at /data. The exit code is the
// number of failed checks.

#include "kpm_worker.h"

#include <AR/ar.h>
#include <AR2/imageFormat.h>

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

static const int kWidth = 2000;
static const int kHeight = 1500;

static std::vector<unsigned char> readFile(const char *path) {
  std::vector<unsigned char> bytes;
  FILE *fp = std::fopen(path, "rb");
  if (!fp) return bytes;
  // On the heap: Emscripten's default stack is 64 KB.
  std::fseek(fp, 0, SEEK_END);
  const long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size > 0) {
    bytes.resize(static_cast<size_t>(size));
    if (std::fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) bytes.clear();
  }
  std::fclose(fp);
  return bytes;
}

/** Initialises the module the way the worker does: camera_para.dat resized to 2000x1500. */
static int initCamera() {
  ARParam param;
  if (arParamLoad("/data/camera_para.dat", 1, &param) < 0) return -100;
  arParamChangeSize(&param, kWidth, kHeight, &param);
  double mat[12];
  double dist[9] = {0};
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++) mat[r * 4 + c] = param.mat[r][c];
  for (int i = 0; i < 9 && i < AR_DIST_FACTOR_NUM_MAX; i++) dist[i] = param.dist_factor[i];
  return kw_init(param.xsize, param.ysize, mat, dist, param.dist_function_version);
}

static int addPinball(int page) {
  const std::vector<unsigned char> fset3 = readFile("/data/pinball.fset3");
  if (fset3.empty()) return -100;
  return kw_add_marker(fset3.data(), static_cast<int>(fset3.size()), page);
}

/** pinball-demo.jpg (2000x1500) as 8-bit luma. */
static std::vector<unsigned char> demoLuma() {
  std::vector<unsigned char> luma;
  FILE *fp = std::fopen("/data/pinball-demo.jpg", "rb");
  if (!fp) return luma;
  AR2JpegImageT *jpeg = ar2ReadJpegImage2(fp);
  std::fclose(fp);
  if (!jpeg) return luma;
  if (jpeg->xsize == kWidth && jpeg->ysize == kHeight) {
    const int pixels = kWidth * kHeight;
    luma.resize(pixels);
    for (int i = 0; i < pixels; i++) {
      const unsigned char *p = jpeg->image + i * jpeg->nc;
      luma[i] = jpeg->nc >= 3 ? static_cast<unsigned char>((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8)
                              : p[0];
    }
  }
  ar2FreeJpegImage(&jpeg);
  return luma;
}

static void match_before_init_fails() {
  // Runs first: nothing has called kw_init() yet.
  const std::vector<unsigned char> luma(kWidth * kHeight, 0);
  CHECK(kw_match(luma.data(), nullptr, 0) == -1);
  CHECK(kw_add_marker(luma.data(), 16, 0) < 0);
  CHECK(kw_detections() == nullptr || kw_result_num() <= 0);
}

static void init_rejects_a_bad_camera() {
  double mat[12] = {0};
  double dist[9] = {0};
  CHECK(kw_init(0, 0, mat, dist, 4) < 0);
  CHECK(kw_init(kWidth, kHeight, nullptr, dist, 4) < 0);
}

static void finds_pinball_in_the_demo() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(0) == 0);
  const std::vector<unsigned char> luma = demoLuma();
  CHECK(!luma.empty());
  if (luma.empty()) return;
  const int found = kw_match(luma.data(), nullptr, 0);
  CHECK(found >= 1);
  CHECK(kw_result_num() >= found);
  const NFTDetection *d = kw_detections();
  CHECK(d != nullptr);
  if (found >= 1 && d) {
    CHECK(d[0].page == 0);
    CHECK(d[0].trans[2][3] > 0.0f);  // ARToolKit camera coordinates: the marker in front
  }
  CHECK(kw_detection_size() == static_cast<int>(sizeof(NFTDetection)));
}

static void keeps_the_main_instances_page_number() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(3) == 0);
  const std::vector<unsigned char> luma = demoLuma();
  if (luma.empty()) return;
  CHECK(kw_match(luma.data(), nullptr, 0) >= 1);
  CHECK(kw_detections() && kw_detections()[0].page == 3);
}

static void blank_frame_finds_nothing() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(0) == 0);
  const std::vector<unsigned char> blank(kWidth * kHeight, 0);
  CHECK(kw_match(blank.data(), nullptr, 0) == 0);
}

static void skipped_page_is_not_reported() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(0) == 0);
  const std::vector<unsigned char> luma = demoLuma();
  if (luma.empty()) return;
  const int skip[1] = {0};
  CHECK(kw_match(luma.data(), skip, 1) == 0);
  // kpmMatching() clears the skip flags after the pass: the next one finds it again.
  CHECK(kw_match(luma.data(), nullptr, 0) >= 1);
}

static void bad_marker_data_changes_nothing() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(0) == 0);
  const unsigned char garbage[32] = {1, 2, 3};
  CHECK(kw_add_marker(garbage, sizeof(garbage), 1) < 0);
  CHECK(kw_add_marker(nullptr, 0, 1) < 0);
  const std::vector<unsigned char> luma = demoLuma();
  if (luma.empty()) return;
  CHECK(kw_match(luma.data(), nullptr, 0) >= 1);
}

static void reinit_forgets_the_markers() {
  CHECK(initCamera() == 0);
  CHECK(addPinball(0) == 0);
  CHECK(initCamera() == 0);
  const std::vector<unsigned char> luma = demoLuma();
  if (luma.empty()) return;
  CHECK(kw_match(luma.data(), nullptr, 0) == 0);
}

struct TestCase {
  const char *name;
  void (*run)();
};

static const TestCase kTests[] = {
    {"match_before_init_fails", match_before_init_fails},
    {"init_rejects_a_bad_camera", init_rejects_a_bad_camera},
    {"finds_pinball_in_the_demo", finds_pinball_in_the_demo},
    {"keeps_the_main_instances_page_number", keeps_the_main_instances_page_number},
    {"blank_frame_finds_nothing", blank_frame_finds_nothing},
    {"skipped_page_is_not_reported", skipped_page_is_not_reported},
    {"bad_marker_data_changes_nothing", bad_marker_data_changes_nothing},
    {"reinit_forgets_the_markers", reinit_forgets_the_markers},
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
