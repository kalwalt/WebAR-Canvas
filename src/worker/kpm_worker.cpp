// The KPM-only worker module (see kpm_worker.h): one camera, one KPM handle, the markers the main
// instance sent, and the result of the last pass. Single-threaded; the worker's JavaScript calls
// these functions from its message handler, one message at a time.
#include "kpm_worker.h"

#include <AR/ar.h>
#include <KPM/kpm.h>
#include <WebARKitTrackers/WebARKitNFT/KpmRefDataSetCopy.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

ARParamLT *gParamLT = nullptr;
KpmHandle *gKpm = nullptr;
KpmRefDataSet *gRefData = nullptr;  // every marker added since kw_init()
std::vector<NFTDetection> gDetections;
int gResultNum = -1;

// kpmLoadRefDataSet() reads a file: the bytes go into the module's in-memory file system first.
const char kMarkerBase[] = "/kpm_worker_marker";
const char kMarkerFile[] = "/kpm_worker_marker.fset3";

void reset() {
  if (gKpm) kpmDeleteHandle(&gKpm);
  if (gRefData) kpmDeleteRefDataSet(&gRefData);
  if (gParamLT) arParamLTFree(&gParamLT);
  gKpm = nullptr;
  gRefData = nullptr;
  gParamLT = nullptr;
  gDetections.clear();
  gResultNum = -1;
}

KpmRefDataSet *loadMarker(const unsigned char *fset3, int size) {
  FILE *fp = std::fopen(kMarkerFile, "wb");
  if (!fp) return nullptr;
  const bool written = std::fwrite(fset3, 1, static_cast<size_t>(size), fp) == static_cast<size_t>(size);
  std::fclose(fp);
  KpmRefDataSet *refDataSet = nullptr;
  if (written && kpmLoadRefDataSet(kMarkerBase, "fset3", &refDataSet) < 0) refDataSet = nullptr;
  std::remove(kMarkerFile);
  return refDataSet;
}

}  // namespace

extern "C" {

int kw_init(int xsize, int ysize, const double *mat, const double *dist, int distFunctionVersion) {
  reset();
  if (xsize <= 0 || ysize <= 0 || !mat || !dist) return -1;
  if (distFunctionVersion < 1 || distFunctionVersion > AR_DIST_FUNCTION_VERSION_MAX) return -1;

  ARParam param;
  std::memset(&param, 0, sizeof(param));
  param.xsize = xsize;
  param.ysize = ysize;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++) param.mat[r][c] = static_cast<ARdouble>(mat[r * 4 + c]);
  for (int i = 0; i < AR_DIST_FACTOR_NUM_MAX; i++) param.dist_factor[i] = static_cast<ARdouble>(dist[i]);
  param.dist_function_version = distFunctionVersion;

  gParamLT = arParamLTCreate(&param, AR_PARAM_LT_DEFAULT_OFFSET);
  if (!gParamLT) return -2;
  gKpm = kpmCreateHandle(gParamLT);
  if (!gKpm) {
    reset();
    return -3;
  }
  return 0;
}

int kw_add_marker(const unsigned char *fset3, int size, int page) {
  if (!gKpm || !fset3 || size <= 0 || page < 0) return -1;

  KpmRefDataSet *marker = loadMarker(fset3, size);
  if (!marker) return -2;
  if (kpmChangePageNoOfRefDataSet(marker, KpmChangePageNoAllPages, page) < 0) {
    kpmDeleteRefDataSet(&marker);
    return -3;
  }

  // As the main instance's core does: merge into a copy, which replaces the set only once KPM
  // accepts it, so a rejected marker leaves the earlier ones loaded.
  KpmRefDataSet *combined = nullptr;
  if (gRefData && (combined = kpmCopyRefDataSet(gRefData)) == nullptr) {
    kpmDeleteRefDataSet(&marker);
    return -4;
  }
  if (kpmMergeRefDataSet(&combined, &marker) < 0) {
    if (marker) kpmDeleteRefDataSet(&marker);
    if (combined) kpmDeleteRefDataSet(&combined);
    return -4;
  }
  if (kpmSetRefDataSet(gKpm, combined) < 0) {
    kpmDeleteRefDataSet(&combined);
    return -5;
  }
  if (gRefData) kpmDeleteRefDataSet(&gRefData);
  gRefData = combined;
  return 0;
}

int kw_match(const unsigned char *luma, const int *skipPages, int skipNum) {
  if (!gKpm || !luma) return -1;

  // Pages already tracked need no pose this pass; kpmMatching() clears the flags when it ends.
  if (skipPages && skipNum > 0) {
    std::vector<int> pages(skipPages, skipPages + skipNum);  // kpmSetMatchingSkipPage() is not const
    kpmSetMatchingSkipPage(gKpm, pages.data(), skipNum);
  }
  kpmMatching(gKpm, const_cast<ARUint8 *>(luma));

  KpmResult *result = nullptr;
  int resultNum = -1;
  kpmGetResult(gKpm, &result, &resultNum);

  gDetections.clear();
  for (int i = 0; i < resultNum; i++) {
    if (result[i].camPoseF != 0) continue;
    NFTDetection detection;
    detection.page = result[i].pageNo;
    std::memcpy(detection.trans, result[i].camPose, sizeof(detection.trans));
    gDetections.push_back(detection);
  }
  gResultNum = resultNum;
  return static_cast<int>(gDetections.size());
}

int kw_result_num(void) { return gResultNum; }

const NFTDetection *kw_detections(void) { return gDetections.empty() ? nullptr : gDetections.data(); }

int kw_detection_size(void) { return static_cast<int>(sizeof(NFTDetection)); }

}  // extern "C"
