#include "WorkerKpmDetector.h"

#include <cstdio>
#include <utility>

WorkerKpmDetector::WorkerKpmDetector(KpmWorkerLink &link, KpmHandle *coreKpmHandle, int frameSize)
    : m_link(&link), m_sync(coreKpmHandle), m_hasCoreHandle(coreKpmHandle != nullptr), m_frameSize(frameSize) {}

bool WorkerKpmDetector::collect(std::vector<NFTDetection> &out, int &resultNum) {
  m_link->poll();
  if (m_link->state() == KpmWorkerLink::State::Fallback) return m_sync.collect(out, resultNum);
  return m_link->takeResult(out, resultNum);
}

bool WorkerKpmDetector::idle() const {
  m_link->poll();
  if (m_link->state() == KpmWorkerLink::State::Fallback) return true;
  return !m_link->passRunning();
}

bool WorkerKpmDetector::start(ARUint8 *luma, const int *skipPages, int skipNum) {
  m_link->poll();
  if (m_link->state() == KpmWorkerLink::State::Fallback) {
    if (!m_hasCoreHandle) {
      if (!m_warnedNoHandle) {
        ARLOGe("KPM fallback has no KPM handle: nothing is detected\n");
        m_warnedNoHandle = true;
      }
      return false;
    }
    return m_sync.start(luma, skipPages, skipNum);  // finished inside start(), like SyncKpmDetector
  }
  // Starting: refused (idle() stays true, so the core tries again next frame). Worker: sent,
  // and it finishes asynchronously.
  m_link->startPass(luma, m_frameSize, skipPages, skipNum);
  return false;
}

void WorkerKpmDetector::waitIdle() { m_link->abandonPass(); }

#ifdef WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR
void WorkerKpmDetector::markersAdded(const std::vector<int> &pages,
                                     const std::vector<std::string> &datasetPathnames) {
  std::vector<KpmMarkerData> data;
  if (!readMarkerData(pages, datasetPathnames, data)) {
    m_link->fallBack("the marker data could not be read for the worker");
    return;
  }
  m_link->addMarkers(std::move(data));
}
#endif

bool WorkerKpmDetector::readMarkerData(const std::vector<int> &pages, const std::vector<std::string> &datasetPathnames,
                                       std::vector<KpmMarkerData> &out) {
  out.clear();
  if (pages.empty() || pages.size() != datasetPathnames.size()) return false;
  for (size_t i = 0; i < pages.size(); i++) {
    const std::string path = datasetPathnames[i] + ".fset3";
    FILE *fp = std::fopen(path.c_str(), "rb");
    KpmMarkerData marker;
    marker.page = pages[i];
    bool ok = fp != nullptr;
    if (ok) {
      std::fseek(fp, 0, SEEK_END);
      const long size = std::ftell(fp);
      std::fseek(fp, 0, SEEK_SET);
      ok = size > 0;
      if (ok) {
        marker.fset3.resize(static_cast<size_t>(size));
        ok = std::fread(marker.fset3.data(), 1, marker.fset3.size(), fp) == marker.fset3.size();
      }
      std::fclose(fp);
    }
    if (!ok) {
      ARLOGe("Could not read %s for the KPM worker\n", path.c_str());
      out.clear();
      return false;
    }
    out.push_back(std::move(marker));
  }
  return true;
}
