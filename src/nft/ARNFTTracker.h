#ifndef AR_NFT_TRACKER_H
#define AR_NFT_TRACKER_H

#include <stdio.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <array>

#include <AR/ar.h>
#include <AR/config.h>
#include <AR2/tracking.h>
#include <AR/arFilterTransMat.h>
#include <AR/paramGL.h>
#include <KPM/kpm.h>
#include <WebARKit/WebARKitLog.h>

#include "trackingMod.h"
#include "markerDecompress.h"
#include "NFTMarkerState.h"

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
    ARdouble nearPlane = 0.1;
    ARdouble farPlane = 1000.0;
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

    std::unique_ptr<ARUint8[]> videoFrame;
    int videoFrameSize = 0;
    std::unique_ptr<ARUint8[]> videoLuma;
};

#endif // AR_NFT_TRACKER_H
