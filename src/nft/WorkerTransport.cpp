#include "WorkerTransport.h"

#include "KpmWorkerLink.h"

#include <emscripten.h>

#include <cstdlib>
#include <string>
#include <utility>

// The JavaScript side keeps one entry per worker in Module.__webarKpm: the Worker, its luma pool
// and the KpmWorkerLink pointer that receives its messages.

EM_JS_DEPS(webar_kpm_worker, "$UTF8ToString,$stringToNewUTF8,malloc,free");

EM_JS(int, webar_kpm_create, (const char *url, KpmWorkerLink *link), {
  if (typeof Worker === 'undefined') return 0;
  var config = (typeof Module.webarConfig === 'object' && Module.webarConfig) || {};
  var src = url ? UTF8ToString(url)
                : (typeof config.kpmWorkerUrl === 'string' ? config.kpmWorkerUrl
                                                           : scriptDirectory + 'videoTest.kpm.js');
  var reg = Module.__webarKpm || (Module.__webarKpm = { next: 1, workers: {} });
  var worker;
  try {
    worker = new Worker(src);
  } catch (e) {
    return 0;
  }
  var id = reg.next++;
  var entry = { worker: worker, pool: [], link: link };
  reg.workers[id] = entry;
  var deliver = function(type, gen, resultNum, bytes, count, text) {
    if (!reg.workers[id]) return;  // terminated: nothing arrives after terminate()
    var detPtr = 0, textPtr = 0;
    if (bytes && bytes.byteLength) {
      detPtr = _malloc(bytes.byteLength);
      HEAPU8.set(new Uint8Array(bytes), detPtr);
    }
    if (text) textPtr = stringToNewUTF8(text);
    _webar_kpm_deliver(entry.link, type, gen, resultNum, detPtr, count, textPtr);
    if (detPtr) _free(detPtr);
    if (textPtr) _free(textPtr);
  };
  worker.onmessage = function(ev) {
    var m = ev.data || {};
    if (m.type === 'ready') {
      deliver(1, 0, 0, null, 0, null);
    } else if (m.type === 'result') {
      // The luma buffer comes back to the pool, whether or not the result is still current.
      if (m.luma instanceof ArrayBuffer && entry.pool.length < 2) entry.pool.push(m.luma);
      deliver(2, m.gen | 0, m.resultNum | 0, m.detections, m.count | 0, null);
    } else if (m.type === 'error') {
      deliver(3, 0, 0, null, 0, String(m.message || 'error'));
    }
  };
  worker.onerror = function(ev) {
    if (ev && ev.preventDefault) ev.preventDefault();
    deliver(3, 0, 0, null, 0, 'the worker failed to load or run' + (ev && ev.message ? ': ' + ev.message : ''));
  };
  worker.onmessageerror = function() { deliver(3, 0, 0, null, 0, 'a message could not be read'); };
  return id;
});

EM_JS(void, webar_kpm_post_init, (int id, int xsize, int ysize, const double *mat, const double *dist, int version), {
  var entry = Module.__webarKpm && Module.__webarKpm.workers[id];
  if (!entry) return;
  entry.worker.postMessage({
    type: 'init', xsize: xsize, ysize: ysize,
    mat: Array.from(HEAPF64.subarray(mat >> 3, (mat >> 3) + 12)),
    dist: Array.from(HEAPF64.subarray(dist >> 3, (dist >> 3) + 9)),
    distFunctionVersion: version
  });
});

EM_JS(void, webar_kpm_post_marker, (int id, int page, const unsigned char *bytes, int size), {
  var entry = Module.__webarKpm && Module.__webarKpm.workers[id];
  if (!entry) return;
  var buffer = HEAPU8.slice(bytes, bytes + size).buffer;  // a copy, handed over to the worker
  entry.worker.postMessage({ type: 'markers', pages: [page], datasets: [buffer] }, [buffer]);
});

EM_JS(void, webar_kpm_post_match, (int id, int gen, const unsigned char *luma, int size, const int *skip, int skipNum), {
  var entry = Module.__webarKpm && Module.__webarKpm.workers[id];
  if (!entry) return;
  var buffer = null;
  while (entry.pool.length && !buffer) {
    var b = entry.pool.pop();
    if (b.byteLength === size) buffer = b;  // a buffer of another frame size is dropped
  }
  if (!buffer) buffer = new ArrayBuffer(size);
  new Uint8Array(buffer).set(HEAPU8.subarray(luma, luma + size));
  var pages = skipNum > 0 ? Array.from(HEAP32.subarray(skip >> 2, (skip >> 2) + skipNum)) : [];
  entry.worker.postMessage({ type: 'match', gen: gen, skipPages: pages, luma: buffer }, [buffer]);
});

EM_JS(void, webar_kpm_terminate, (int id), {
  var reg = Module.__webarKpm;
  var entry = reg && reg.workers[id];
  if (!entry) return;
  delete reg.workers[id];
  entry.worker.onmessage = entry.worker.onerror = entry.worker.onmessageerror = null;
  entry.worker.terminate();
});

EM_JS(int, webar_kpm_enabled_by_page, (), {
  var config = Module.webarConfig;
  return (config && typeof config === 'object' && config.kpmWorker === false) ? 0 : 1;
});

// Called by the JavaScript above for every message of a worker. type: 1 ready, 2 result, 3 error.
extern "C" EMSCRIPTEN_KEEPALIVE void webar_kpm_deliver(KpmWorkerLink *link, int type, int gen, int resultNum,
                                                       const NFTDetection *detections, int count,
                                                       const char *text) {
  if (!link) return;
  if (type == 1) {
    link->onReady();
  } else if (type == 2) {
    std::vector<NFTDetection> list;
    if (detections && count > 0) list.assign(detections, detections + count);
    link->onResult(gen, resultNum, std::move(list));
  } else if (type == 3) {
    link->onError(text ? text : "error");
  }
}

WorkerTransport::WorkerTransport(const char *url) : m_url(url) {}

WorkerTransport::~WorkerTransport() { terminate(); }

bool WorkerTransport::start() {
  if (m_id != 0) return true;
  m_id = webar_kpm_create(m_url, m_link);
  return m_id != 0;
}

void WorkerTransport::sendInit(const KpmCamera &camera) {
  if (m_id) webar_kpm_post_init(m_id, camera.xsize, camera.ysize, camera.mat, camera.dist, camera.distFunctionVersion);
}

void WorkerTransport::sendMarkers(std::vector<KpmMarkerData> &markers) {
  if (!m_id) return;
  for (KpmMarkerData &m : markers) {
    webar_kpm_post_marker(m_id, m.page, m.fset3.data(), static_cast<int>(m.fset3.size()));
    std::vector<unsigned char>().swap(m.fset3);  // released here once copied for the worker
  }
}

void WorkerTransport::sendMatch(int gen, const unsigned char *luma, int size, const int *skipPages, int skipNum) {
  if (m_id) webar_kpm_post_match(m_id, gen, luma, size, skipPages, skipNum);
}

void WorkerTransport::terminate() {
  if (m_id) webar_kpm_terminate(m_id);
  m_id = 0;
}

bool WorkerTransport::enabledByPage() { return webar_kpm_enabled_by_page() != 0; }
