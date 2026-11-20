#ifndef WEBAR_KPM_WORKER_H
#define WEBAR_KPM_WORKER_H

// The C entry points of the KPM-only worker module (target kpm_worker, videoTest.kpm.js/.wasm).
// The worker's JavaScript (src/worker/kpm_worker_main.js) calls them; kpm_worker_tests calls
// them directly. Only KPM matching runs here: no AR2 tracking, no file loading from URLs.
// Every input comes from the main instance: the resized camera as numbers and each marker's
// .fset3 bytes, with the page number the main instance's core assigned to it.

#include <WebARKitTrackers/WebARKitNFT/NFTDetector.h>  // NFTDetection

#ifdef __cplusplus
extern "C" {
#endif

/**
 * (Re)creates the camera lookup table and the KPM handle from a camera already resized to the
 * frame, and forgets every marker. mat holds 12 values (row-major 3x4), dist 9.
 * @return 0, or a negative value when the camera is invalid (the module is then uninitialised)
 */
int kw_init(int xsize, int ysize, const double *mat, const double *dist, int distFunctionVersion);

/**
 * Adds one marker's .fset3 data, as the bytes of the file, under the given page number, and
 * makes KPM match against every marker added since kw_init().
 * @return 0, or a negative value (not initialised, bad data); a failed marker changes nothing
 */
int kw_add_marker(const unsigned char *fset3, int size, int page);

/**
 * Runs one KPM pass on an 8-bit luma frame of the camera's size.
 * @param skipPages pages not to report (already tracked), may be null
 * @return the number of detections (results with a pose), or -1 when not initialised
 */
int kw_match(const unsigned char *luma, const int *skipPages, int skipNum);

/** The number of results KPM reported in the last pass, including those without a pose. */
int kw_result_num(void);

/** The detections of the last pass (kw_match()'s return value entries), or null. */
const NFTDetection *kw_detections(void);

/** sizeof(NFTDetection), so the worker's JavaScript reads the array without guessing. */
int kw_detection_size(void);

#ifdef __cplusplus
}
#endif

#endif  // WEBAR_KPM_WORKER_H
