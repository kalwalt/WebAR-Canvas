#ifndef WEBAR_WORKER_KPM_DETECTOR_H
#define WEBAR_WORKER_KPM_DETECTOR_H

// An NFTDetector whose KPM passes run in a Web Worker (issue #16), through a KpmWorkerLink it
// borrows. When the link has fallen back, the passes run synchronously on the core's own KPM
// handle, exactly as SyncKpmDetector does: today's behaviour, with today's stall.

#include "KpmWorkerLink.h"

#include <KPM/kpm.h>
#include <WebARKitTrackers/WebARKitNFT/NFTDetector.h>
#include <WebARKitTrackers/WebARKitNFT/SyncKpmDetector.h>

#include <string>
#include <vector>

class WorkerKpmDetector final : public NFTDetector {
 public:
  /**
   * @param link the worker's state; not owned, must outlive the detector
   * @param coreKpmHandle the core's KPM handle, used only in fallback; not owned, may be null
   *        (the fallback then detects nothing)
   * @param frameSize bytes of a luma frame (width * height)
   */
  WorkerKpmDetector(KpmWorkerLink &link, KpmHandle *coreKpmHandle, int frameSize);
  /** Abandons a running pass, as waitIdle() does: the core destroys detectors instead of waiting. */
  ~WorkerKpmDetector() override;

  bool collect(std::vector<NFTDetection> &out, int &resultNum) override;
  /** False while a pass runs in the worker, and while its result waits to be collected. */
  bool idle() const override;
  bool start(ARUint8 *luma, const int *skipPages, int skipNum) override;
  /** Does not block: abandons the running pass (its late result is dropped by generation). */
  void waitIdle() override;

#ifdef WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR
  void markersAdded(const std::vector<int> &pages, const std::vector<std::string> &datasetPathnames) override;
#endif

  /**
   * Reads each marker's <pathname>.fset3 from the main instance's file system (where the build
   * embeds it), for the worker. @return false when a file cannot be read (nothing is returned)
   */
  static bool readMarkerData(const std::vector<int> &pages, const std::vector<std::string> &datasetPathnames,
                             std::vector<KpmMarkerData> &out);

 private:
  KpmWorkerLink *m_link;
  SyncKpmDetector m_sync;
  bool m_hasCoreHandle;
  bool m_warnedNoHandle = false;
  int m_frameSize;
};

#endif  // WEBAR_WORKER_KPM_DETECTOR_H
