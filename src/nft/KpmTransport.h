#ifndef WEBAR_KPM_TRANSPORT_H
#define WEBAR_KPM_TRANSPORT_H

// The channel between the main instance and the KPM worker (issue #16). The browser
// implementation (WorkerTransport.cpp) posts messages to a Web Worker; the tests use a fake.
// Incoming messages (ready, result, error) go to KpmWorkerLink, which owns the transport.

#include <AR/ar.h>

#include <vector>

/** The camera the worker matches with: an ARParam already resized to the frame, as plain numbers. */
struct KpmCamera {
  int xsize = 0;
  int ysize = 0;
  double mat[12] = {};   // row-major 3x4
  double dist[9] = {};   // AR_DIST_FACTOR_NUM_MAX values
  int distFunctionVersion = 0;
};

/** Copies an ARParam into a KpmCamera, so no pointer into the main heap crosses to the worker. */
KpmCamera kpmCameraFromParam(const ARParam &param);

/** One marker for the worker: its .fset3 file's bytes and the page the core gave it. */
struct KpmMarkerData {
  int page = -1;
  std::vector<unsigned char> fset3;
};

class KpmWorkerLink;

class KpmTransport {
 public:
  virtual ~KpmTransport() = default;

  /** The link that receives ready / result / error; KpmWorkerLink's constructor calls it. */
  virtual void attach(KpmWorkerLink * /*link*/) {}

  /** Starts the worker. @return false when no worker can be created (no Worker, bad URL known at once) */
  virtual bool start() = 0;

  /** init: the worker (re)creates its KPM handle for this camera and forgets its markers; it answers ready. */
  virtual void sendInit(const KpmCamera &camera) = 0;

  /** markers: the worker loads them, after the ones it has, under their pages. The bytes may be moved from. */
  virtual void sendMarkers(std::vector<KpmMarkerData> &markers) = 0;

  /**
   * match: one KPM pass on a copy of the luma frame (the transport copies it into a buffer of its
   * own pool before this returns), tagged with gen; the worker answers result with the same gen.
   */
  virtual void sendMatch(int gen, const unsigned char *luma, int size, const int *skipPages, int skipNum) = 0;

  /** Stops the worker; nothing arrives after this. */
  virtual void terminate() = 0;
};

#endif  // WEBAR_KPM_TRANSPORT_H
