#ifndef WEBAR_KPM_WORKER_LINK_H
#define WEBAR_KPM_WORKER_LINK_H

// The main side's state of the KPM worker (issue #16): the transport, whether the worker is
// starting, running or given up (fallback), the generation of the running pass and its result.
//
// It outlives detectors: the core destroys its detector on every setCamera() / setupAR2() and
// creates a new one in the next addNFTMarkers(), so ARNFTTracker owns the link and each
// WorkerKpmDetector borrows it. A recreation re-sends the camera and the markers, but does not
// respawn the worker, and a fallback stays final for the session.

#include "KpmTransport.h"

#include <WebARKitTrackers/WebARKitNFT/NFTDetector.h>

#include <memory>
#include <string>
#include <vector>

class KpmWorkerLink {
 public:
  enum class State { Starting, Worker, Fallback };

  static constexpr double kReadyTimeoutMs = 5000.0;  // from init to ready
  static constexpr double kPassTimeoutMs = 2000.0;   // from match to result (a pass is ~45 ms on a desktop)

  /**
   * @param transport the channel to the worker; owned. Null means no worker: fallback at once.
   * @param clock current time in milliseconds (NFTTrackingConfig::clock); null means nftDefaultClockMs
   * @param enabled false (Module.webarConfig.kpmWorker = false) forces the fallback at once
   */
  KpmWorkerLink(std::unique_ptr<KpmTransport> transport, double (*clock)(), bool enabled);
  ~KpmWorkerLink();

  KpmWorkerLink(const KpmWorkerLink &) = delete;
  KpmWorkerLink &operator=(const KpmWorkerLink &) = delete;

  State state() const { return m_state; }
  /** Why the link fell back (empty while it has not). */
  const std::string &fallbackReason() const { return m_fallbackReason; }

  // --- From the detector (main thread) ---

  /** A (new) camera: the worker starts over with it and no markers. Starts the worker the first time. */
  void setCamera(const KpmCamera &camera);
  /** Markers the core accepted, after those already sent for this camera. */
  void addMarkers(std::vector<KpmMarkerData> markers);
  /** Checks the timeouts; called before every decision (idle, start, collect). */
  void poll();
  /** True while a pass sent to the worker has not answered. */
  bool passRunning() const { return m_passRunning; }
  /** Sends a pass when the worker is ready and idle. @return true when it was sent */
  bool startPass(const unsigned char *luma, int size, const int *skipPages, int skipNum);
  /** Takes the current pass's result, once. */
  bool takeResult(std::vector<NFTDetection> &out, int &resultNum);
  /** Ends the running pass without waiting: its result, if it ever comes, is dropped (generation). */
  void abandonPass();
  /** Gives up the worker for the session, logging why once. */
  void fallBack(const std::string &reason);

  // --- From the transport (incoming messages, main thread) ---

  void onReady();
  void onResult(int gen, int resultNum, std::vector<NFTDetection> detections);
  void onError(const std::string &message);

 private:
  double now() const;

  std::unique_ptr<KpmTransport> m_transport;
  double (*m_clock)();
  State m_state = State::Starting;
  std::string m_fallbackReason;
  bool m_started = false;
  bool m_hasCamera = false;
  double m_initSentMs = 0.0;
  int m_gen = 0;
  bool m_passRunning = false;
  double m_matchSentMs = 0.0;
  double m_lastPollMs = 0.0;  // to tell a frozen page (no frames) from a silent worker
  bool m_hasResult = false;
  int m_resultNum = -1;
  std::vector<NFTDetection> m_detections;
};

#endif  // WEBAR_KPM_WORKER_LINK_H
