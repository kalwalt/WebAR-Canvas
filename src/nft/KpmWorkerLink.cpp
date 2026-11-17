#include "KpmWorkerLink.h"

#include <WebARKitTrackers/WebARKitNFT/NFTTrackingConfig.h>  // nftDefaultClockMs

#include <utility>

KpmCamera kpmCameraFromParam(const ARParam &param) {
  KpmCamera camera;
  camera.xsize = param.xsize;
  camera.ysize = param.ysize;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++) camera.mat[r * 4 + c] = param.mat[r][c];
  for (int i = 0; i < 9 && i < AR_DIST_FACTOR_NUM_MAX; i++) camera.dist[i] = param.dist_factor[i];
  camera.distFunctionVersion = param.dist_function_version;
  return camera;
}

KpmWorkerLink::KpmWorkerLink(std::unique_ptr<KpmTransport> transport, double (*clock)(), bool enabled)
    : m_transport(std::move(transport)), m_clock(clock ? clock : &nftDefaultClockMs) {
  if (m_transport) m_transport->attach(this);
  if (!enabled) {
    fallBack("disabled by the page");
  } else if (!m_transport) {
    fallBack("no worker transport in this build");
  }
}

KpmWorkerLink::~KpmWorkerLink() {
  if (m_transport && m_started && m_state != State::Fallback) m_transport->terminate();
}

double KpmWorkerLink::now() const { return m_clock(); }

void KpmWorkerLink::setCamera(const KpmCamera &camera) {
  if (m_state == State::Fallback) return;
  if (!m_started) {
    m_started = true;
    if (!m_transport->start()) {
      fallBack("the worker could not be created");
      return;
    }
  }
  // The worker starts over: a running pass belongs to the old camera and markers.
  abandonPass();
  m_transport->sendInit(camera);
  m_state = State::Starting;
  m_hasCamera = true;
  m_initSentMs = now();
  m_lastPollMs = m_initSentMs;
}

void KpmWorkerLink::addMarkers(std::vector<KpmMarkerData> markers) {
  if (m_state == State::Fallback || !m_hasCamera || markers.empty()) return;
  m_transport->sendMarkers(markers);
}

void KpmWorkerLink::poll() {
  // No poll for longer than a timeout means no frames ran: a hidden tab or a sleeping device.
  // The worker's answer may be waiting in the event queue behind this frame, so the freeze does
  // not count against the worker: its deadlines move by the gap.
  const double t = now();
  const double gap = t - m_lastPollMs;
  if (gap > kPassTimeoutMs) {
    m_initSentMs += gap;
    m_matchSentMs += gap;
  }
  m_lastPollMs = t;

  if (m_state == State::Starting && m_hasCamera && now() - m_initSentMs > kReadyTimeoutMs) {
    fallBack("no answer within 5 s of starting");
  } else if (m_state == State::Worker && m_passRunning && now() - m_matchSentMs > kPassTimeoutMs) {
    fallBack("a pass got no result within 2 s");
  }
}

bool KpmWorkerLink::startPass(const unsigned char *luma, int size, const int *skipPages, int skipNum) {
  if (m_state != State::Worker || m_passRunning || !luma || size <= 0) return false;
  ++m_gen;
  m_hasResult = false;
  m_transport->sendMatch(m_gen, luma, size, skipPages, skipNum);
  m_passRunning = true;
  m_matchSentMs = now();
  m_lastPollMs = m_matchSentMs;
  return true;
}

bool KpmWorkerLink::takeResult(std::vector<NFTDetection> &out, int &resultNum) {
  if (!m_hasResult) return false;
  out.swap(m_detections);
  m_detections.clear();
  resultNum = m_resultNum;
  m_hasResult = false;
  return true;
}

void KpmWorkerLink::abandonPass() {
  // A result tagged with the old generation is dropped when it arrives.
  ++m_gen;
  m_passRunning = false;
  m_hasResult = false;
  m_detections.clear();
}

void KpmWorkerLink::fallBack(const std::string &reason) {
  if (m_state == State::Fallback) return;
  m_state = State::Fallback;
  m_fallbackReason = reason;
  m_passRunning = false;
  m_hasResult = false;
  m_detections.clear();
  if (m_transport && m_started) m_transport->terminate();
  ARLOGw("KPM worker unavailable (%s): running KPM on the main thread\n", reason.c_str());
}

void KpmWorkerLink::onReady() {
  if (m_state != State::Starting) return;
  m_state = State::Worker;
  ARLOGi("KPM worker ready\n");
}

void KpmWorkerLink::onResult(int gen, int resultNum, std::vector<NFTDetection> detections) {
  // Only the running pass's own answer counts: an abandoned pass's result may name pages of
  // an old marker set.
  if (m_state != State::Worker || !m_passRunning || gen != m_gen) return;
  m_detections = std::move(detections);
  m_resultNum = resultNum;
  m_hasResult = true;
  m_passRunning = false;
}

void KpmWorkerLink::onError(const std::string &message) { fallBack("worker error: " + message); }
