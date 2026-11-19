#ifndef AR_NFT_TRACKER_H
#define AR_NFT_TRACKER_H

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <WebARKitTrackers/WebARKitNFT/ARToolKitNFTCore.h>
#include <WebARKitVideoLuma.h>

#include "KpmTransport.h"

constexpr int NFT_PAGES_MAX = PAGES_MAX;

struct NFTMarkerData {
    int id = -1;
    int width = 0;
    int height = 0;
    int dpi = 0;
};

/** Where the last update() spent its time, in milliseconds (0 before any measured update). */
struct NFTUpdateTimings {
    double lumaMs = 0.0;  // RGBA to luma conversion
    double trackMs = 0.0; // the core: AR2 tracking, and KPM detection (with the KPM worker, only handing the frame to it)
};

/**
 * The NFT tracker of WebAR-Canvas: an adapter over WebARKitLib's ARToolKitNFTCore.
 *
 * It owns the RGBA frame that JavaScript fills (getVideoFrame()) and its luma conversion;
 * camera, markers, KPM detection, AR2 tracking and pose filtering are the core's.
 * Single-thread core (singleThreadPreset()), with pose filtering on, a 200 ms detection
 * interval and projection planes 10 / 5000.
 *
 * Typical use: loadCamera(), setup(), setupAR2(), loadMarker(); then, per frame, fill
 * getVideoFrame() and call update().
 */
class ARNFTTracker {
public:
    ARNFTTracker();
    /** With the KPM worker reached through this transport (tests); the default is the browser's Web Worker. */
    explicit ARNFTTracker(std::unique_ptr<KpmTransport> kpmTransport);
    ~ARNFTTracker();

    // Prevent copying
    ARNFTTracker(const ARNFTTracker&) = delete;
    ARNFTTracker& operator=(const ARNFTTracker&) = delete;

    // Camera parameter loading & initialization
    int loadCamera(const std::string &cparamName);
    /**
     * Allocates the frame for width x height and applies the camera. A later setup() re-applies
     * the camera: call setupAR2() after it, as after setCamera(); the markers already loaded stay
     * loaded and are detected again (WebARKitLib 0.13.0).
     * @return 0, or -1 when the frame size is not positive or the camera cannot be applied
     *         (the tracker is then torn down)
     */
    int setup(int width, int height, int cameraID);
    /** Applies another camera. As with the core, call setupAR2() after it; loaded markers are kept. */
    int setCamera(int cameraID);
    int setupAR2();
    void teardown();

    // Marker loading
    int loadMarker(const std::string &datasetPathname);
    std::vector<int> loadMarkers(const std::vector<std::string> &datasetPathnames);

    // Frame ingestion & Tracking
    ARUint8* getVideoFrame() { return videoFrame.get(); }
    const ARUint8* getVideoFrame() const { return videoFrame.get(); }
    int getVideoFrameSize() const { return videoFrameSize; }
    ARUint8* getVideoLuma() { return lumaInfo ? lumaInfo->buff.get() : nullptr; }
    const ARUint8* getVideoLuma() const { return lumaInfo ? lumaInfo->buff.get() : nullptr; }

    int update(); // Main tracking cycle: converts luma, detects, tracks
    NFTUpdateTimings getLastUpdateTimings() const { return lastTimings; }

    // Tracking state queries
    bool isMarkerTracked(int markerIndex) const;
    bool anyMarkerTracked() const;
    bool allMarkersTracked() const;
    float getTrackingError(int markerIndex) const;
    void getTransformMatrix(int markerIndex, float m[16]) const;
    void getPose(int markerIndex, ARdouble pose[3][4]) const;

    // Tracking configuration
    void setContinuousDetection(bool enable);
    bool getContinuousDetection() const { return continuousDetection; }
    void setDetectionInterval(double intervalMs);
    double getDetectionInterval() const { return detectionIntervalMs; }
    void setFiltering(bool enable);
    bool getFiltering() const { return withFiltering; }

    // Marker accessors
    int getMarkerCount() const { return core.markerCount(); }
    NFTMarkerData getMarkerData(int index) const;
    const NFTMarkerState& getMarkerState(int index) const;

    // Camera frustum projection matrix (OpenGL column-major 4x4)
    const ARdouble* getCameraLens() const { return core.cameraLens(); }
    void setProjectionPlanes(ARdouble nearP, ARdouble farP);
    ARdouble getNearPlane() const { return core.getProjectionNearPlane(); }
    ARdouble getFarPlane() const { return core.getProjectionFarPlane(); }
    void recalculateCameraLens() { core.recalculateCameraLens(); }

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    const ARParam* getCameraParam() const { return &core.cameraParam(); }
    const ARParamLT* getCameraParamLT() const { return core.cameraParamLT(); }

    // Logging
    void setLogLevel(int level);
    int getLogLevel() const;

private:
    // The core's settings: singleThreadPreset(), with the KPM detector in a Web Worker
    // (Detector::External and makeKpmDetector) when built with WEBAR_KPM_WORKER.
    NFTTrackingConfig coreConfig();
    void initCore();
    static std::unique_ptr<NFTDetector> makeKpmDetector(void *user, KpmHandle *kpm, const ARParam &camera);

    // The KPM worker's state, shared by the detectors the core creates; declared before core so
    // it outlives the core's last detector. Null without WEBAR_KPM_WORKER.
    std::unique_ptr<class KpmWorkerLink> kpmLink;
    ARToolKitNFTCore core{coreConfig(), true};
    int coreId = -1;

    int width = 0;
    int height = 0;

    // Frame Buffers
    std::unique_ptr<ARUint8[]> videoFrame;
    int videoFrameSize = 0;
    webarkit::WebARKitLumaInfo *lumaInfo = nullptr;
    NFTUpdateTimings lastTimings;

    // Settings pushed to the core, kept for the getters
    bool continuousDetection = true;
    double detectionIntervalMs = 200.0;
    bool withFiltering = true;
};

#endif // AR_NFT_TRACKER_H
