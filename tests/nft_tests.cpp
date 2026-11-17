// Node test runner for ARNFTTracker (target nft_tests, built with -DWEBAR_CANVAS_BUILD_TESTS=ON).
// Run it with scripts/test-wasm.sh or `node build-docker.js --test`.
// WebARKitLib's tests/data is embedded at /data. The exit code is the number of failed checks.

#include "ARNFTTracker.h"
#include "KpmTransport.h"
#include "KpmWorkerLink.h"
#include "WorkerKpmDetector.h"

#include <AR2/imageFormat.h>
#include <KPM/kpm.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
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

/** Decodes a JPEG of the tracker's frame size into its RGBA frame buffer. */
static bool fillFromJpeg(ARNFTTracker &t, const char *path) {
  FILE *fp = std::fopen(path, "rb");
  if (!fp) return false;
  AR2JpegImageT *jpeg = ar2ReadJpegImage2(fp);
  std::fclose(fp);
  if (!jpeg) return false;
  ARUint8 *rgba = t.getVideoFrame();
  const bool fits = rgba && jpeg->xsize == t.getWidth() && jpeg->ysize == t.getHeight();
  if (fits) {
    const int pixels = jpeg->xsize * jpeg->ysize;
    for (int i = 0; i < pixels; i++) {
      const ARUint8 *src = jpeg->image + i * jpeg->nc;
      ARUint8 *dst = rgba + i * 4;
      dst[0] = src[0];
      dst[1] = jpeg->nc >= 3 ? src[1] : src[0];
      dst[2] = jpeg->nc >= 3 ? src[2] : src[0];
      dst[3] = 255;
    }
  }
  ar2FreeJpegImage(&jpeg);
  return fits;
}

/** Sets up a 2000x1500 tracker with pinball and feeds pinball-demo.jpg until it is tracked. */
static bool trackPinball(ARNFTTracker &t) {
  const int cam = t.loadCamera("/data/camera_para.dat");
  if (cam < 0 || t.setup(2000, 1500, cam) != 0 || t.setupAR2() != 0) return false;
  if (t.loadMarker("/data/pinball") != 0) return false;
  if (!fillFromJpeg(t, "/data/pinball-demo.jpg")) return false;
  for (int i = 0; i < 60 && !t.isMarkerTracked(0); i++) {
    t.update();
  }
  return t.isMarkerTracked(0);
}

static void loads_camera_param() {
  ARNFTTracker t;
  CHECK(t.loadCamera("/data/camera_para.dat") >= 0);
  CHECK(t.loadCamera("/data/does-not-exist.dat") == -1);
}

static void missing_marker_fails() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0 && t.setupAR2() == 0);
  CHECK(t.loadMarker("/data/does-not-exist") == -1);
  CHECK(t.getMarkerCount() == 0);
}

static void update_before_setup_is_safe() {
  ARNFTTracker t;
  CHECK(t.update() == 0);
  CHECK(!t.isMarkerTracked(0));
}

static void update_without_markers_is_safe() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0 && t.setupAR2() == 0);
  CHECK(t.update() == 0);
}

static void keeps_dev_tracking_settings() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0);
  CHECK(t.getNearPlane() == 10.0 && t.getFarPlane() == 5000.0);
  CHECK(t.getCameraLens()[15] == 0.0 && t.getCameraLens()[11] < 0.0);
  // The frustum dev prints for camera_para.dat at 640x480 with planes 10 / 5000.
  CHECK(std::fabs(t.getCameraLens()[10] - (-1.0040)) < 1e-3);
  CHECK(std::fabs(t.getCameraLens()[14] - (-20.0401)) < 1e-3);
  CHECK(t.getDetectionInterval() == 200.0);
  CHECK(t.getContinuousDetection() && t.getFiltering());
}

static void out_of_range_index_is_empty() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  CHECK(t.getMarkerData(5).id == -1 && t.getMarkerData(-1).width == 0);
  CHECK(!t.isMarkerTracked(5));
  float m[16];
  std::fill(m, m + 16, 1.0f);
  t.getTransformMatrix(5, m);
  CHECK(std::all_of(m, m + 16, [](float v) { return v == 0.0f; }));
}

static void tracks_pinball_demo() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  const NFTMarkerData &d = t.getMarkerData(0);
  CHECK(d.id == 0 && d.width == 893 && d.height == 1117 && d.dpi == 120);
  float m[16];
  t.getTransformMatrix(0, m);
  CHECK(m[14] < 0.0f); // marker in front of the camera (GL, right-handed)
  CHECK(m[15] == 1.0f);
}

static void loses_tracking_on_blank_frame() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  std::memset(t.getVideoFrame(), 0, t.getVideoFrameSize());
  t.update();
  CHECK(!t.isMarkerTracked(0));
}

static void re_setup_checks_the_camera() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0);
  CHECK(t.setup(320, 240, cam) == 0 && t.getWidth() == 320);
  // An unknown camera on a later setup(): it fails and leaves nothing half set up.
  CHECK(t.setup(640, 480, -1) == -1);
  CHECK(t.getVideoFrame() == nullptr);
  CHECK(t.update() == 0);
}

static void setup_rejects_an_empty_frame() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0);
  // WebARKitLib 0.11.0: setup() returns -1 for a non-positive frame size and changes nothing.
  CHECK(t.setup(0, 480, cam) == -1);
  CHECK(t.getVideoFrame() == nullptr);
  CHECK(t.update() == 0);
}

static int gTrackedLogLines = 0;
static void countTrackedLogLines(const char *message) {
  if (std::strstr(message, "Tracked page") != nullptr) ++gTrackedLogLines;
}

static void no_log_line_per_tracked_frame() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  const int logLevel = t.getLogLevel();
  gTrackedLogLines = 0;
  arLogSetLogger(countTrackedLogLines, 0);
  for (int i = 0; i < 5; i++) t.update();
  arLogSetLogger(nullptr, 0);
  CHECK(t.isMarkerTracked(0));
  CHECK(gTrackedLogLines == 0);
  CHECK(t.getLogLevel() == logLevel);
}

static void reports_update_timings() {
  ARNFTTracker t;
  CHECK(t.getLastUpdateTimings().lumaMs == 0.0 && t.getLastUpdateTimings().trackMs == 0.0);
  // update() before setup() does no work, so it measures nothing.
  CHECK(t.update() == 0);
  CHECK(t.getLastUpdateTimings().lumaMs == 0.0 && t.getLastUpdateTimings().trackMs == 0.0);
  CHECK(trackPinball(t));
  t.update();
  const NFTUpdateTimings u = t.getLastUpdateTimings();
  // A 2000x1500 frame: both the luma conversion and the core take measurable time.
  CHECK(u.lumaMs > 0.0 && u.trackMs > 0.0);
  CHECK(std::isfinite(u.lumaMs) && std::isfinite(u.trackMs));
  t.teardown();
  CHECK(t.getLastUpdateTimings().lumaMs == 0.0 && t.getLastUpdateTimings().trackMs == 0.0);
}

// --- WorkerKpmDetector and KpmWorkerLink, against a fake transport (issue #16) ---

/** Records what the link sends; the tests deliver the worker's answers by hand. */
struct FakeTransport : KpmTransport {
  struct Match {
    int gen;
    std::vector<int> skip;
    int size;
  };
  bool available = true;
  int starts = 0;
  int terminated = 0;
  std::vector<std::string> sent;  // "init", "markers", "match", in order
  std::vector<KpmCamera> cameras;
  std::vector<std::vector<int>> markerPages;
  std::vector<size_t> markerBytes;
  std::vector<Match> matches;

  bool start() override {
    ++starts;
    return available;
  }
  void sendInit(const KpmCamera &camera) override {
    sent.push_back("init");
    cameras.push_back(camera);
  }
  void sendMarkers(std::vector<KpmMarkerData> &markers) override {
    sent.push_back("markers");
    std::vector<int> pages;
    for (const KpmMarkerData &m : markers) {
      pages.push_back(m.page);
      markerBytes.push_back(m.fset3.size());
    }
    markerPages.push_back(pages);
  }
  void sendMatch(int gen, const unsigned char *, int size, const int *skipPages, int skipNum) override {
    sent.push_back("match");
    matches.push_back({gen, std::vector<int>(skipPages, skipPages + skipNum), size});
  }
  void terminate() override { ++terminated; }
};

static double gFakeNowMs = 0.0;
static double fakeClockMs() { return gFakeNowMs; }

static KpmCamera testCamera() {
  KpmCamera c;
  c.xsize = 640;
  c.ysize = 480;
  c.distFunctionVersion = 4;
  return c;
}

static std::vector<KpmMarkerData> oneMarker(int page) {
  KpmMarkerData m;
  m.page = page;
  m.fset3.assign(16, 1);
  return {m};
}

static NFTDetection detectionAt(int page) {
  NFTDetection d;
  d.page = page;
  std::memset(d.trans, 0, sizeof(d.trans));
  d.trans[2][3] = 500.0f;
  return d;
}

/** A link over a fake transport (kept for inspection), already given a camera and one marker. */
struct LinkFixture {
  FakeTransport *transport;
  KpmWorkerLink link;
  explicit LinkFixture(bool enabled = true, bool available = true)
      : transport(new FakeTransport), link(std::unique_ptr<KpmTransport>(transport), fakeClockMs, enabled) {
    transport->available = available;
    gFakeNowMs = 1000.0;
    link.setCamera(testCamera());
    link.addMarkers(oneMarker(0));
  }
};

static int gWorkerLogLines = 0;
static void countWorkerLogLines(const char *message) {
  if (std::strstr(message, "KPM worker unavailable") != nullptr) ++gWorkerLogLines;
}

static const int kFrame = 640 * 480;

/** Lets time pass as frames do: the detector is polled every 16 ms, as the core polls it each frame. */
static void runFrames(const NFTDetector &d, double ms) {
  for (double t = 0.0; t < ms; t += 16.0) {
    gFakeNowMs += 16.0;
    d.idle();
  }
}

static void worker_pass_start_result_collect() {
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  std::vector<unsigned char> luma(kFrame, 0);
  CHECK(f.transport->starts == 1);
  CHECK((f.transport->sent == std::vector<std::string>{"init", "markers"}));
  CHECK(f.transport->markerPages.size() == 1 && f.transport->markerPages[0] == std::vector<int>{0});
  // Before ready: passes are refused and the detector looks idle.
  CHECK(!d.start(luma.data(), nullptr, 0));
  CHECK(d.idle() && f.transport->matches.empty());
  f.link.onReady();
  CHECK(f.link.state() == KpmWorkerLink::State::Worker);
  const int skip[1] = {2};
  CHECK(!d.start(luma.data(), skip, 1));  // asynchronous: never finished inside start()
  CHECK(!d.idle());
  CHECK(f.transport->matches.size() == 1);
  CHECK(f.transport->matches[0].skip == std::vector<int>{2} && f.transport->matches[0].size == kFrame);
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(!d.collect(out, resultNum));
  f.link.onResult(f.transport->matches[0].gen, 3, {detectionAt(0)});
  CHECK(d.idle());
  CHECK(d.collect(out, resultNum));
  CHECK(out.size() == 1 && out[0].page == 0 && resultNum == 3);
  // A result is collected once.
  CHECK(!d.collect(out, resultNum));
}

static void late_result_after_abandon_is_dropped() {
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  std::vector<unsigned char> luma(kFrame, 0);
  f.link.onReady();
  d.start(luma.data(), nullptr, 0);
  const int oldGen = f.transport->matches.back().gen;
  d.waitIdle();  // the core's dropRunningSearch(): does not block
  CHECK(d.idle());
  f.link.onResult(oldGen, 1, {detectionAt(0)});
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(!d.collect(out, resultNum));
  // The next pass works and only its own result counts.
  d.start(luma.data(), nullptr, 0);
  const int newGen = f.transport->matches.back().gen;
  CHECK(newGen != oldGen);
  f.link.onResult(oldGen, 1, {detectionAt(0)});
  CHECK(!d.idle() && !d.collect(out, resultNum));
  f.link.onResult(newGen, 2, {detectionAt(0)});
  CHECK(d.collect(out, resultNum) && resultNum == 2);
}

static void recreated_detector_resends_without_respawning() {
  LinkFixture f;
  f.link.onReady();
  {
    WorkerKpmDetector first(f.link, nullptr, kFrame);
  }
  // The core's next setupAR2() + addNFTMarkers(): a new detector, the camera and all markers again.
  KpmCamera camera = testCamera();
  camera.xsize = 320;
  f.link.setCamera(camera);
  f.link.addMarkers(oneMarker(0));
  WorkerKpmDetector second(f.link, nullptr, 320 * 480);
  CHECK(f.transport->starts == 1);
  CHECK((f.transport->sent == std::vector<std::string>{"init", "markers", "init", "markers"}));
  CHECK(f.transport->cameras.size() == 2 && f.transport->cameras[1].xsize == 320);
  // The worker starts over with the new camera: no pass until it is ready again.
  std::vector<unsigned char> luma(320 * 480, 0);
  CHECK(!second.start(luma.data(), nullptr, 0) && f.transport->matches.empty());
  f.link.onReady();
  second.start(luma.data(), nullptr, 0);
  CHECK(f.transport->matches.size() == 1);
}

static void falls_back_without_ready() {
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  gWorkerLogLines = 0;
  arLogSetLogger(countWorkerLogLines, 0);
  runFrames(d, KpmWorkerLink::kReadyTimeoutMs - 32.0);
  CHECK(d.idle() && f.link.state() == KpmWorkerLink::State::Starting);
  runFrames(d, 64.0);
  CHECK(d.idle());
  CHECK(f.link.state() == KpmWorkerLink::State::Fallback);
  CHECK(f.transport->terminated == 1);
  // A late ready changes nothing; the switch is logged once.
  f.link.onReady();
  d.idle();
  arLogSetLogger(nullptr, 0);
  CHECK(f.link.state() == KpmWorkerLink::State::Fallback);
  CHECK(gWorkerLogLines == 1);
}

static void falls_back_on_error() {
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  f.link.onReady();
  std::vector<unsigned char> luma(kFrame, 0);
  d.start(luma.data(), nullptr, 0);
  const int gen = f.transport->matches.back().gen;
  f.link.onError("worker script failed");
  CHECK(f.link.state() == KpmWorkerLink::State::Fallback);
  CHECK(f.link.fallbackReason().find("worker script failed") != std::string::npos);
  CHECK(d.idle());
  // Nothing from the worker counts after the switch.
  f.link.onResult(gen, 1, {detectionAt(0)});
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(!d.collect(out, resultNum));
}

static void falls_back_when_a_pass_never_answers() {
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  f.link.onReady();
  std::vector<unsigned char> luma(kFrame, 0);
  d.start(luma.data(), nullptr, 0);
  runFrames(d, KpmWorkerLink::kPassTimeoutMs - 32.0);
  CHECK(!d.idle());
  runFrames(d, 64.0);
  CHECK(d.idle());
  CHECK(f.link.state() == KpmWorkerLink::State::Fallback && f.transport->terminated == 1);
}

static void a_frozen_page_does_not_fall_back() {
  // A hidden tab or a sleeping device stops the frames; the worker's answer then waits in the
  // event queue behind the first frame after the resume. That frame must not count the freeze
  // against the worker.
  LinkFixture f;
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  gFakeNowMs += 60000.0;  // frozen during start-up
  CHECK(d.idle() && f.link.state() == KpmWorkerLink::State::Starting);
  f.link.onReady();
  std::vector<unsigned char> luma(kFrame, 0);
  d.start(luma.data(), nullptr, 0);
  gFakeNowMs += 60000.0;  // frozen during a pass
  CHECK(!d.idle() && f.link.state() == KpmWorkerLink::State::Worker);
  f.link.onResult(f.transport->matches.back().gen, 1, {detectionAt(0)});
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(d.collect(out, resultNum) && out.size() == 1);
  // A worker that really stops answering is still caught while frames run.
  d.start(luma.data(), nullptr, 0);
  runFrames(d, KpmWorkerLink::kPassTimeoutMs + 64.0);
  CHECK(f.link.state() == KpmWorkerLink::State::Fallback);
}

static void falls_back_when_disabled_or_unavailable() {
  LinkFixture disabled(false);
  CHECK(disabled.link.state() == KpmWorkerLink::State::Fallback);
  CHECK(disabled.transport->starts == 0 && disabled.transport->sent.empty());
  LinkFixture unavailable(true, false);
  CHECK(unavailable.link.state() == KpmWorkerLink::State::Fallback);
  CHECK(unavailable.transport->starts == 1 && unavailable.transport->sent.empty());
  KpmWorkerLink none(nullptr, fakeClockMs, true);
  none.setCamera(testCamera());
  CHECK(none.state() == KpmWorkerLink::State::Fallback);
}

/** pinball-demo.jpg as 8-bit luma, and a KPM handle for 2000x1500 with pinball at page 0, as the core builds. */
struct CoreKpmFixture {
  ARParamLT *paramLT = nullptr;
  KpmHandle *kpm = nullptr;
  std::vector<unsigned char> luma;
  CoreKpmFixture() {
    ARParam param;
    if (arParamLoad("/data/camera_para.dat", 1, &param) < 0) return;
    arParamChangeSize(&param, 2000, 1500, &param);
    paramLT = arParamLTCreate(&param, AR_PARAM_LT_DEFAULT_OFFSET);
    kpm = paramLT ? kpmCreateHandle(paramLT) : nullptr;
    KpmRefDataSet *ref = nullptr;
    if (kpm && kpmLoadRefDataSet("/data/pinball", "fset3", &ref) == 0) {
      // pinball.fset3 stores page 1; the core renumbers each marker to its id.
      kpmChangePageNoOfRefDataSet(ref, KpmChangePageNoAllPages, 0);
      kpmSetRefDataSet(kpm, ref);
      kpmDeleteRefDataSet(&ref);
    }
    FILE *fp = std::fopen("/data/pinball-demo.jpg", "rb");
    AR2JpegImageT *jpeg = fp ? ar2ReadJpegImage2(fp) : nullptr;
    if (fp) std::fclose(fp);
    if (jpeg && jpeg->xsize == 2000 && jpeg->ysize == 1500) {
      luma.resize(2000 * 1500);
      for (size_t i = 0; i < luma.size(); i++) {
        const ARUint8 *p = jpeg->image + i * jpeg->nc;
        luma[i] = jpeg->nc >= 3 ? static_cast<unsigned char>((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8) : p[0];
      }
    }
    if (jpeg) ar2FreeJpegImage(&jpeg);
  }
  ~CoreKpmFixture() {
    if (kpm) kpmDeleteHandle(&kpm);
    if (paramLT) arParamLTFree(&paramLT);
  }
};

static void fallback_detects_on_the_core_handle() {
  CoreKpmFixture core;
  CHECK(core.kpm != nullptr && !core.luma.empty());
  if (!core.kpm || core.luma.empty()) return;
  LinkFixture f(false);
  WorkerKpmDetector d(f.link, core.kpm, 2000 * 1500);
  CHECK(d.idle());
  CHECK(d.start(core.luma.data(), nullptr, 0));  // synchronous, like SyncKpmDetector
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(d.collect(out, resultNum));
  CHECK(!out.empty() && out[0].page == 0 && out[0].trans[2][3] > 0.0f);
  CHECK(f.transport->matches.empty());
}

static void fallback_after_a_failed_pass_detects_next() {
  CoreKpmFixture core;
  if (!core.kpm || core.luma.empty()) return;
  LinkFixture f;
  WorkerKpmDetector d(f.link, core.kpm, 2000 * 1500);
  f.link.onReady();
  CHECK(!d.start(core.luma.data(), nullptr, 0));
  f.link.onError("lost");
  CHECK(d.idle());
  CHECK(d.start(core.luma.data(), nullptr, 0));
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(d.collect(out, resultNum) && !out.empty());
}

static void fallback_without_a_handle_detects_nothing() {
  LinkFixture f(false);
  WorkerKpmDetector d(f.link, nullptr, kFrame);
  std::vector<unsigned char> luma(kFrame, 0);
  CHECK(!d.start(luma.data(), nullptr, 0));
  CHECK(d.idle());
  std::vector<NFTDetection> out;
  int resultNum = -1;
  CHECK(!d.collect(out, resultNum));
}

static void reads_marker_data_from_the_file_system() {
  std::vector<KpmMarkerData> data;
  CHECK(WorkerKpmDetector::readMarkerData({4}, {"/data/pinball"}, data));
  CHECK(data.size() == 1 && data[0].page == 4);
  FILE *fp = std::fopen("/data/pinball.fset3", "rb");
  long size = -1;
  if (fp) {
    std::fseek(fp, 0, SEEK_END);
    size = std::ftell(fp);
    std::fclose(fp);
  }
  CHECK(!data.empty() && size > 0 && static_cast<long>(data[0].fset3.size()) == size);
  std::vector<KpmMarkerData> none;
  CHECK(!WorkerKpmDetector::readMarkerData({0, 1}, {"/data/pinball", "/data/does-not-exist"}, none));
  CHECK(none.empty());
  CHECK(!WorkerKpmDetector::readMarkerData({0}, {}, none));
}

static void camera_copies_the_param() {
  ARParam param;
  CHECK(arParamLoad("/data/camera_para.dat", 1, &param) == 0);
  arParamChangeSize(&param, 2000, 1500, &param);
  const KpmCamera c = kpmCameraFromParam(param);
  CHECK(c.xsize == 2000 && c.ysize == 1500 && c.distFunctionVersion == param.dist_function_version);
  CHECK(c.mat[0] == param.mat[0][0] && c.mat[11] == param.mat[2][3]);
  CHECK(c.dist[0] == param.dist_factor[0] && c.dist[8] == param.dist_factor[8]);
}

struct TestCase {
  const char *name;
  void (*run)();
};

static const TestCase kTests[] = {
    {"loads_camera_param", loads_camera_param},
    {"missing_marker_fails", missing_marker_fails},
    {"update_before_setup_is_safe", update_before_setup_is_safe},
    {"update_without_markers_is_safe", update_without_markers_is_safe},
    {"keeps_dev_tracking_settings", keeps_dev_tracking_settings},
    {"out_of_range_index_is_empty", out_of_range_index_is_empty},
    {"tracks_pinball_demo", tracks_pinball_demo},
    {"loses_tracking_on_blank_frame", loses_tracking_on_blank_frame},
    {"re_setup_checks_the_camera", re_setup_checks_the_camera},
    {"setup_rejects_an_empty_frame", setup_rejects_an_empty_frame},
    {"no_log_line_per_tracked_frame", no_log_line_per_tracked_frame},
    {"reports_update_timings", reports_update_timings},
    {"worker_pass_start_result_collect", worker_pass_start_result_collect},
    {"late_result_after_abandon_is_dropped", late_result_after_abandon_is_dropped},
    {"recreated_detector_resends_without_respawning", recreated_detector_resends_without_respawning},
    {"falls_back_without_ready", falls_back_without_ready},
    {"falls_back_on_error", falls_back_on_error},
    {"falls_back_when_a_pass_never_answers", falls_back_when_a_pass_never_answers},
    {"falls_back_when_disabled_or_unavailable", falls_back_when_disabled_or_unavailable},
    {"a_frozen_page_does_not_fall_back", a_frozen_page_does_not_fall_back},
    {"fallback_detects_on_the_core_handle", fallback_detects_on_the_core_handle},
    {"fallback_after_a_failed_pass_detects_next", fallback_after_a_failed_pass_detects_next},
    {"fallback_without_a_handle_detects_nothing", fallback_without_a_handle_detects_nothing},
    {"reads_marker_data_from_the_file_system", reads_marker_data_from_the_file_system},
    {"camera_copies_the_param", camera_copies_the_param},
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
