// Browser check of the KPM worker path (issue #16): WorkerKpmDetector -> KpmWorkerLink ->
// WorkerTransport -> a real Web Worker running videoTest.kpm.js, on pinball-demo.jpg.
// Built with -DWEBAR_CANVAS_BUILD_TESTS=ON (target kpm_worker_browser, only when asked for) and
// driven by tests/kpm_worker_browser.html, served from the build directory next to
// videoTest.kpm.js/.wasm. Not run in CI: it needs a browser.

#include "KpmWorkerLink.h"
#include "WorkerKpmDetector.h"
#include "WorkerTransport.h"

#include <AR/ar.h>
#include <AR2/imageFormat.h>
#include <KPM/kpm.h>
#include <emscripten.h>

#include <cstdio>
#include <memory>
#include <vector>

namespace {

const int kWidth = 2000;
const int kHeight = 1500;

std::unique_ptr<KpmWorkerLink> gLink;
std::unique_ptr<WorkerKpmDetector> gDetector;
std::vector<unsigned char> gLuma;
ARParamLT *gParamLT = nullptr;
KpmHandle *gCoreKpm = nullptr;  // the "core's" handle, for the fallback
int gPasses = 0;
int gFound = 0;
int gPage = -1;
float gZ = 0.0f;

bool loadLuma() {
  FILE *fp = std::fopen("/data/pinball-demo.jpg", "rb");
  AR2JpegImageT *jpeg = fp ? ar2ReadJpegImage2(fp) : nullptr;
  if (fp) std::fclose(fp);
  if (!jpeg) return false;
  const bool fits = jpeg->xsize == kWidth && jpeg->ysize == kHeight;
  if (fits) {
    gLuma.resize(kWidth * kHeight);
    for (size_t i = 0; i < gLuma.size(); i++) {
      const ARUint8 *p = jpeg->image + i * jpeg->nc;
      gLuma[i] = jpeg->nc >= 3 ? static_cast<unsigned char>((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8) : p[0];
    }
  }
  ar2FreeJpegImage(&jpeg);
  return fits;
}

}  // namespace

extern "C" {

/** Starts the worker path. url null: the default (videoTest.kpm.js next to this page's script). */
EMSCRIPTEN_KEEPALIVE int kwb_start(const char *url, int enabled) {
  ARParam param;
  if (arParamLoad("/data/camera_para.dat", 1, &param) < 0 || !loadLuma()) return -1;
  arParamChangeSize(&param, kWidth, kHeight, &param);
  gParamLT = arParamLTCreate(&param, AR_PARAM_LT_DEFAULT_OFFSET);
  gCoreKpm = gParamLT ? kpmCreateHandle(gParamLT) : nullptr;
  KpmRefDataSet *ref = nullptr;
  if (!gCoreKpm || kpmLoadRefDataSet("/data/pinball", "fset3", &ref) < 0) return -2;
  kpmChangePageNoOfRefDataSet(ref, KpmChangePageNoAllPages, 0);
  kpmSetRefDataSet(gCoreKpm, ref);
  kpmDeleteRefDataSet(&ref);

  WorkerTransport *transport = new WorkerTransport(url);
  gLink.reset(new KpmWorkerLink(std::unique_ptr<KpmTransport>(transport), nullptr, enabled != 0));
  transport->setLink(gLink.get());
  gDetector.reset(new WorkerKpmDetector(*gLink, gCoreKpm, kWidth * kHeight));

  gLink->setCamera(kpmCameraFromParam(param));
  std::vector<KpmMarkerData> markers;
  if (!WorkerKpmDetector::readMarkerData({0}, {"/data/pinball"}, markers)) return -3;
  gLink->addMarkers(std::move(markers));
  return 0;
}

/** One "frame": collect a finished pass, start one when idle. @return passes collected so far */
EMSCRIPTEN_KEEPALIVE int kwb_step() {
  if (!gDetector) return -1;
  std::vector<NFTDetection> out;
  int resultNum = -1;
  bool collected = gDetector->collect(out, resultNum);
  if (!collected && gDetector->idle()) {
    if (gDetector->start(gLuma.data(), nullptr, 0)) collected = gDetector->collect(out, resultNum);
  }
  if (collected) {
    ++gPasses;
    if (!out.empty()) {
      ++gFound;
      gPage = out[0].page;
      gZ = out[0].trans[2][3];
    }
  }
  return gPasses;
}

/** 0 starting, 1 worker, 2 fallback. */
EMSCRIPTEN_KEEPALIVE int kwb_state() { return gLink ? static_cast<int>(gLink->state()) : -1; }
EMSCRIPTEN_KEEPALIVE int kwb_found() { return gFound; }
EMSCRIPTEN_KEEPALIVE int kwb_page() { return gPage; }
EMSCRIPTEN_KEEPALIVE float kwb_z() { return gZ; }
EMSCRIPTEN_KEEPALIVE int kwb_running() { return gLink && gLink->passRunning() ? 1 : 0; }

}  // extern "C"

int main() { return 0; }
