#include "ARNFTTracker.h"
#include "KpmWorkerLink.h"
#include "WorkerKpmDetector.h"
#include <AR/paramGL.h>
#include <chrono>

#if defined(WEBAR_KPM_WORKER) && defined(WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR)
#define WEBAR_USE_KPM_WORKER 1
#include "WorkerTransport.h"
#endif

NFTTrackingConfig ARNFTTracker::coreConfig() {
    NFTTrackingConfig config = singleThreadPreset();
#ifdef WEBAR_USE_KPM_WORKER
    config.detector = NFTTrackingConfig::Detector::External;
    config.makeDetector = &ARNFTTracker::makeKpmDetector;
    config.makeDetectorUser = this;
#endif
    return config;
}

// The core's factory: called in the first addNFTMarkers() after every setupAR2() (and so after
// every camera change), with the core's KPM handle and the resized camera. The worker starts
// over with this camera; the markers follow through markersAdded().
std::unique_ptr<NFTDetector> ARNFTTracker::makeKpmDetector(void *user, KpmHandle *kpm, const ARParam &camera) {
    ARNFTTracker *self = static_cast<ARNFTTracker *>(user);
    if (!self || !self->kpmLink) return nullptr;
    self->kpmLink->setCamera(kpmCameraFromParam(camera));
    return std::unique_ptr<NFTDetector>(new WorkerKpmDetector(*self->kpmLink, kpm, camera.xsize * camera.ysize));
}

ARNFTTracker::ARNFTTracker()
#ifdef WEBAR_USE_KPM_WORKER
    : ARNFTTracker(std::unique_ptr<KpmTransport>(new WorkerTransport()))
#endif
{
#ifdef WEBAR_USE_KPM_WORKER
    // The page can force the main-thread path (Module.webarConfig.kpmWorker = false, ?kpmWorker=0).
    if (!WorkerTransport::enabledByPage() && kpmLink) kpmLink->fallBack("disabled by the page");
#else
    initCore();
#endif
}

ARNFTTracker::ARNFTTracker(std::unique_ptr<KpmTransport> kpmTransport) {
#ifdef WEBAR_USE_KPM_WORKER
    kpmLink.reset(new KpmWorkerLink(std::move(kpmTransport), nullptr, true));
#else
    (void)kpmTransport;
#endif
    initCore();
}

void ARNFTTracker::initCore() {
    core.setContinuousDetection(continuousDetection);
    core.setDetectionInterval(detectionIntervalMs);
    core.setFiltering(withFiltering);
    // Before setup(): setCamera() computes the camera lens with these planes.
    core.setProjectionNearPlane(10.0);
    core.setProjectionFarPlane(5000.0);
    ARLOGi("ARNFTTracker initialized.\n");
}

ARNFTTracker::~ARNFTTracker() {
    teardown();
}

int ARNFTTracker::loadCamera(const std::string &cparamName) {
    return ARToolKitNFTCore::loadCamera(cparamName);
}

int ARNFTTracker::setup(int w, int h, int cameraID) {
    // The core returns -1 for an invalid frame size (changing nothing), and frees paramLT when
    // the camera cannot be applied, so cameraParamLT() tells on every setup() (WebARKitLib 0.11.0).
    this->coreId = core.setup(w, h, cameraID);
    if (this->coreId < 0 || core.cameraParamLT() == nullptr) {
        ARLOGe("setup(): frame %dx%d with camera %d could not be set up\n", w, h, cameraID);
        teardown();
        return -1;
    }

    this->width = w;
    this->height = h;
    this->videoFrameSize = w * h * 4 * sizeof(ARUint8);
    this->videoFrame = std::make_unique<ARUint8[]>(this->videoFrameSize);

    if (lumaInfo) {
        webarkit::webarkitVideoLumaFinal(&lumaInfo);
    }
    lumaInfo = webarkit::webarkitVideoLumaInit(w, h, true);
    if (!lumaInfo) {
        ARLOGe("setup(): luma conversion could not be initialized\n");
        teardown();
        return -1;
    }

    ARLOGi("setup(): Allocated videoFrameSize %d (%dx%d RGBA)\n", this->videoFrameSize, w, h);
    return 0;
}

int ARNFTTracker::setCamera(int cameraID) {
    return core.setCamera(this->coreId, cameraID);
}

int ARNFTTracker::setupAR2() {
    return core.setupAR2();
}

int ARNFTTracker::loadMarker(const std::string &datasetPathname) {
    const std::vector<int> ids = loadMarkers({datasetPathname});
    return ids.empty() ? -1 : ids[0];
}

std::vector<int> ARNFTTracker::loadMarkers(const std::vector<std::string> &datasetPathnames) {
    return core.addNFTMarkers(datasetPathnames);
}

int ARNFTTracker::update() {
    if (!videoFrame || !lumaInfo) {
        return 0;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const ARUint8 *luma = webarkit::webarkitVideoLuma(lumaInfo, videoFrame.get());
    const auto lumaDone = Clock::now();
    core.setVideoFrame(videoFrame.get(), luma);
    // ARToolKitNFTCore logs every tracked frame at INFO ("Tracked page ..."): keep the
    // ARToolKit log at WARN or above while it runs, so errors still show.
    const int logLevel = arLogLevel;
    arLogLevel = std::max(logLevel, static_cast<int>(AR_LOG_LEVEL_WARN));
    core.detectNFTMarker();
    arLogLevel = logLevel;
    const auto trackDone = Clock::now();
    using Ms = std::chrono::duration<double, std::milli>;
    lastTimings = {Ms(lumaDone - start).count(), Ms(trackDone - lumaDone).count()};
    return anyMarkerTracked() ? 1 : 0;
}

bool ARNFTTracker::isMarkerTracked(int markerIndex) const {
    const NFTMarkerState *state = core.markerState(markerIndex);
    return state != nullptr && state->tracking;
}

bool ARNFTTracker::anyMarkerTracked() const {
    for (int i = 0; i < core.markerCount(); ++i) {
        if (isMarkerTracked(i)) return true;
    }
    return false;
}

bool ARNFTTracker::allMarkersTracked() const {
    if (core.markerCount() == 0) return false;
    for (int i = 0; i < core.markerCount(); ++i) {
        if (!isMarkerTracked(i)) return false;
    }
    return true;
}

float ARNFTTracker::getTrackingError(int markerIndex) const {
    const NFTMarkerState *state = core.markerState(markerIndex);
    return state != nullptr ? state->err : -1.0f;
}

void ARNFTTracker::getPose(int markerIndex, ARdouble pose[3][4]) const {
    const NFTMarkerState *state = core.markerState(markerIndex);
    const bool tracked = state != nullptr && state->tracking;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            pose[r][c] = tracked ? state->pose[r][c] : 0.0;
        }
    }
}

void ARNFTTracker::getTransformMatrix(int markerIndex, float m[16]) const {
    const NFTMarkerState *state = core.markerState(markerIndex);
    if (state == nullptr || !state->tracking) {
        std::fill(m, m + 16, 0.0f);
        return;
    }
    ARdouble glPose[16];
    arglCameraViewRH(state->pose, glPose, 1.0);
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<float>(glPose[i]);
    }
}

void ARNFTTracker::setContinuousDetection(bool enable) {
    continuousDetection = enable;
    core.setContinuousDetection(enable);
}

void ARNFTTracker::setDetectionInterval(double intervalMs) {
    // The core treats negative values and NaN as 0 (every frame).
    detectionIntervalMs = intervalMs > 0.0 ? intervalMs : 0.0;
    core.setDetectionInterval(detectionIntervalMs);
}

void ARNFTTracker::setFiltering(bool enable) {
    withFiltering = enable;
    core.setFiltering(enable);
}

NFTMarkerData ARNFTTracker::getMarkerData(int index) const {
    // ARToolKitNFTCore::getNFTData() aborts on an out-of-range index.
    if (index < 0 || index >= core.markerCount()) {
        return NFTMarkerData{};
    }
    const nftMarker marker = core.getNFTData(index);
    return NFTMarkerData{marker.id_NFT, marker.width_NFT, marker.height_NFT, marker.dpi_NFT};
}

const NFTMarkerState& ARNFTTracker::getMarkerState(int index) const {
    static const NFTMarkerState emptyState;
    const NFTMarkerState *state = core.markerState(index);
    return state != nullptr ? *state : emptyState;
}

void ARNFTTracker::setProjectionPlanes(ARdouble nearP, ARdouble farP) {
    core.setProjectionNearPlane(nearP);
    core.setProjectionFarPlane(farP);
    core.recalculateCameraLens();
}

void ARNFTTracker::teardown() {
    core.teardown();

    videoFrame.reset();
    videoFrameSize = 0;
    lastTimings = {};
    width = 0;
    height = 0;

    if (lumaInfo) {
        webarkit::webarkitVideoLumaFinal(&lumaInfo);
        lumaInfo = nullptr;
    }
}

void ARNFTTracker::setLogLevel(int level) {
    if (level >= 0) {
        arLogLevel = level;
    }
}

int ARNFTTracker::getLogLevel() const {
    return arLogLevel;
}
