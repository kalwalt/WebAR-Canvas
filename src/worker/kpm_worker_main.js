// The KPM worker's message handler (issue #16), linked into videoTest.kpm.js with --pre-js.
// Protocol (main <-> worker): runtime (the module has loaded), init -> ready, markers -> loaded
// (tagged with the number of inits handled), match -> result, error. Every binary payload
// is an ArrayBuffer in the transfer list; the luma buffer of a match goes back with its result.
// Messages that arrive before the WASM runtime is ready wait in a queue, in order.

var kwQueue = [];
var kwRuntimeReady = false;
var kwLumaPtr = 0;
var kwLumaSize = 0;
var kwEpoch = 0;  // inits handled: tags the loaded answers, so the main side can drop stale ones

function kwCopyIn(bytes) {
  var ptr = _malloc(bytes.length);
  HEAPU8.set(bytes, ptr);
  return ptr;
}

function kwHandle(m) {
  if (m.type === 'init') {
    var mat = _malloc(12 * 8);
    var dist = _malloc(9 * 8);
    HEAPF64.set(m.mat, mat >> 3);
    HEAPF64.set(m.dist, dist >> 3);
    var r = _kw_init(m.xsize, m.ysize, mat, dist, m.distFunctionVersion);
    kwEpoch++;
    _free(mat);
    _free(dist);
    if (r < 0) self.postMessage({ type: 'error', message: 'kw_init failed (' + r + ')' });
    else self.postMessage({ type: 'ready' });
  } else if (m.type === 'markers') {
    for (var i = 0; i < m.pages.length; i++) {
      var ptr = kwCopyIn(new Uint8Array(m.datasets[i]));
      var added = _kw_add_marker(ptr, m.datasets[i].byteLength, m.pages[i]);
      _free(ptr);
      if (added < 0) self.postMessage({ type: 'error', message: 'kw_add_marker failed for page ' + m.pages[i] + ' (' + added + ')' });
    }
    self.postMessage({ type: 'loaded', epoch: kwEpoch });
  } else if (m.type === 'match') {
    var size = m.luma.byteLength;
    if (size !== kwLumaSize) {
      if (kwLumaPtr) _free(kwLumaPtr);
      kwLumaPtr = _malloc(size);
      kwLumaSize = size;
    }
    HEAPU8.set(new Uint8Array(m.luma), kwLumaPtr);
    var skip = m.skipPages || [];
    var skipPtr = 0;
    if (skip.length) {
      skipPtr = _malloc(skip.length * 4);
      HEAP32.set(skip, skipPtr >> 2);
    }
    var count = _kw_match(kwLumaPtr, skipPtr, skip.length);
    if (skipPtr) _free(skipPtr);
    var detections = new ArrayBuffer(0);
    if (count > 0) {
      var at = _kw_detections();
      detections = HEAPU8.slice(at, at + count * _kw_detection_size()).buffer;
    }
    self.postMessage({ type: 'result', gen: m.gen, resultNum: _kw_result_num(), count: Math.max(count, 0),
                       detections: detections, luma: m.luma }, [detections, m.luma]);
  }
}

function kwDispatch(m) {
  try {
    kwHandle(m);
  } catch (e) {
    self.postMessage({ type: 'error', message: String(e && e.message || e) });
  }
}

Module['onRuntimeInitialized'] = function() {
  kwRuntimeReady = true;
  self.postMessage({ type: 'runtime' });
  var queued = kwQueue;
  kwQueue = [];
  queued.forEach(kwDispatch);
};

self.onmessage = function(ev) {
  if (kwRuntimeReady) kwDispatch(ev.data || {});
  else kwQueue.push(ev.data || {});
};
