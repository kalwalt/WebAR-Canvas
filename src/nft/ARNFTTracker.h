#ifndef AR_NFT_TRACKER_H
#define AR_NFT_TRACKER_H

#include <stdio.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <array>
#include <algorithm>

#include <AR/ar.h>
#include <AR/config.h>
#include <AR2/tracking.h>
#include <AR/arFilterTransMat.h>
#include <AR/paramGL.h>
#include <KPM/kpm.h>
#include <WebARKit/WebARKitLog.h>
#include <WebARKitVideoLuma.h>

#include <WebARKitTrackers/WebARKitNFT/trackingMod.h>
#include <WebARKitTrackers/WebARKitNFT/markerDecompress.h>
#include <WebARKitTrackers/WebARKitNFT/NFTMarkerState.h>

constexpr int NFT_PAGES_MAX = 20;

struct NFTMarkerData {
    int id = -1;
    int width = 0;
    int height = 0;
    int dpi = 0;
};

class ARNFTTracker {
public:
    ARNFTTracker();
    ~ARNFTTracker();

    // Prevent copying
    ARNFTTracker(const ARNFTTracker&) = delete;
    ARNFTTracker& operator=(const ARNFTTracker&) = delete;

    // Camera parameter loading & initialization
    int loadCamera(const std::string &cparamName);
    int setup(int width, int height, int cameraID);
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
    ARUint8* getVideoLuma() { return videoLuma.get(); }
    const ARUint8* getVideoLuma() const { return videoLuma.get(); }

    void updateVideoLuma();
    int update(); // Main tracking cycle: converts luma, detects, tracks
    int detectNFTMarker();
    void trackMarkers();

    // Tracking state queries
    bool isMarkerTracked(int markerIndex) const;
    bool anyMarkerTracked() const;
    bool allMarkersTracked() const;
    float getTrackingError(int markerIndex) const;
    void getTransformMatrix(int markerIndex, float m[16]) const;
    void getPose(int markerIndex, ARdouble pose[3][4]) const;

    // Tracking configuration
    void setContinuousDetection(bool enable) { continuousDetection = enable; }
    bool getContinuousDetection() const { return continuousDetection; }
    void setDetectionInterval(double intervalMs) { detectionIntervalMs = intervalMs; }
    double getDetectionInterval() const { return detectionIntervalMs; }
    void setFiltering(bool enable) { withFiltering = enable; }
    bool getFiltering() const { return withFiltering; }
    void setFilterParameters(double sampleRate, double cutoffFreq);

    // Marker accessors
    int getMarkerCount() const { return surfaceSetCount; }
    const NFTMarkerData& getMarkerData(int index) const;
    const NFTMarkerState& getMarkerState(int index) const;
    NFTMarkerState& getMarkerState(int index);
    AR2SurfaceSetT* getSurfaceSet(int index) {
        return (index >= 0 && index < surfaceSetCount) ? surfaceSet[index] : nullptr;
    }

    // Camera frustum projection matrix (OpenGL column-major 4x4)
    const ARdouble* getCameraLens() const { return cameraLens; }
    void setProjectionPlanes(ARdouble nearP, ARdouble farP);
    ARdouble getNearPlane() const { return nearPlane; }
    ARdouble getFarPlane() const { return farPlane; }
    void recalculateCameraLens();

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    const ARParam* getCameraParam() const { return &param; }
    const ARParamLT* getCameraParamLT() const { return paramLT; }

    // Logging
    void setLogLevel(int level);
    int getLogLevel() const;

private:
    void deleteHandles();

    int width = 0;
    int height = 0;
    ARdouble nearPlane = 10.0;
    ARdouble farPlane = 5000.0;
    ARdouble cameraLens[16] = {0};

    AR_PIXEL_FORMAT pixFormat = AR_PIXEL_FORMAT_RGBA;

    ARParam param = {};
    ARParamLT *paramLT = nullptr;
    ARHandle *arhandle = nullptr;
    AR3DHandle *ar3DHandle = nullptr;
    AR2HandleT *ar2Handle = nullptr;
    KpmHandle *kpmHandle = nullptr;

    std::unordered_map<int, ARParam> cameraParams;
    int nextCameraID = 0;

    // NFT Marker Data & State
    int surfaceSetCount = 0;
    AR2SurfaceSetT *surfaceSet[NFT_PAGES_MAX] = {nullptr};
    std::array<NFTMarkerState, NFT_PAGES_MAX> markerStates;
    std::vector<NFTMarkerData> markersData;

    // Frame Buffers
    std::unique_ptr<ARUint8[]> videoFrame;
    int videoFrameSize = 0;
    std::unique_ptr<ARUint8[]> videoLuma;
    webarkit::WebARKitLumaInfo *lumaInfo = nullptr;

    // Detection & Tracking Parameters
    bool continuousDetection = true;
    double detectionIntervalMs = 200.0;
    double lastKpmEndMs = 0.0;
    bool withFiltering = true;
    double filterCutoffFrequency = AR_FILTER_TRANS_MAT_CUTOFF_FREQ_DEFAULT;
    double filterSampleRate = AR_FILTER_TRANS_MAT_SAMPLE_RATE_DEFAULT;
};

#endif // AR_NFT_TRACKER_H
