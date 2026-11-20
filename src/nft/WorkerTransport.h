#ifndef WEBAR_WORKER_TRANSPORT_H
#define WEBAR_WORKER_TRANSPORT_H

// The browser's KpmTransport (issue #16): a Web Worker running videoTest.kpm.js, driven with
// postMessage and transferable ArrayBuffers (no SharedArrayBuffer, no isolation needed).
//
// Messages from the worker reach the KpmWorkerLink that attached it, on the main thread, in the
// browser's event loop, between frames. The luma copies travel in a pool of two ArrayBuffers that
// go to the worker with match and come back with result.

#include "KpmTransport.h"

class KpmWorkerLink;

class WorkerTransport final : public KpmTransport {
 public:
  /** @param url the worker script; empty means Module.webarConfig.kpmWorkerUrl, else videoTest.kpm.js next to the app */
  explicit WorkerTransport(const char *url = nullptr);
  ~WorkerTransport() override;

  void attach(KpmWorkerLink *link) override { m_link = link; }

  bool start() override;
  void sendInit(const KpmCamera &camera) override;
  void sendMarkers(std::vector<KpmMarkerData> &markers) override;
  void sendMatch(int gen, const unsigned char *luma, int size, const int *skipPages, int skipNum) override;
  void terminate() override;

  /** False when the page set Module.webarConfig.kpmWorker = false (or ?kpmWorker=0 in the shell). */
  static bool enabledByPage();

 private:
  KpmWorkerLink *m_link = nullptr;
  int m_id = 0;  // the worker's entry on the JavaScript side; 0 while not started
  const char *m_url;
};

#endif  // WEBAR_WORKER_TRANSPORT_H
