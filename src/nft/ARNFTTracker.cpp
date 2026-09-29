#include "ARNFTTracker.h"
#include <iostream>

ARNFTTracker::ARNFTTracker() {
    ARLOGi("ARNFTTracker initialized.\n");
}

ARNFTTracker::~ARNFTTracker() {
    teardown();
}

int ARNFTTracker::loadCamera(const std::string &cparamName) {
    ARParam p;
    if (arParamLoad(cparamName.c_str(), 1, &p) < 0) {
        ARLOGe("loadCamera(): Error loading parameter file %s\n", cparamName.c_str());
        return -1;
    }
    int camId = nextCameraID++;
    cameraParams[camId] = p;
    ARLOGi("loadCamera(): Loaded camera parameters from %s (cameraID: %d, default size: %dx%d)\n",
           cparamName.c_str(), camId, p.xsize, p.ysize);
    return camId;
}

int ARNFTTracker::setup(int w, int h, int cameraID) {
    this->width = w;
    this->height = h;
    this->videoFrameSize = w * h * 4 * sizeof(ARUint8);
    this->videoFrame = std::make_unique<ARUint8[]>(this->videoFrameSize);
    this->videoLuma = std::make_unique<ARUint8[]>(w * h);

    if (setCamera(cameraID) < 0) {
        ARLOGe("setup(): setCamera failed for cameraID %d\n", cameraID);
        return -1;
    }

    ARLOGi("setup(): Allocated videoFrameSize %d (%dx%d RGBA)\n", this->videoFrameSize, w, h);
    return 0;
}

int ARNFTTracker::setCamera(int cameraID) {
    auto it = cameraParams.find(cameraID);
    if (it == cameraParams.end()) {
        ARLOGe("setCamera(): cameraID %d not found in cameraParams\n", cameraID);
        return -1;
    }
    this->param = it->second;

    if (this->param.xsize != this->width || this->param.ysize != this->height) {
        ARLOGw("*** Camera Parameter resized from %d, %d to %d, %d ***\n",
               this->param.xsize, this->param.ysize, this->width, this->height);
        arParamChangeSize(&(this->param), this->width, this->height, &(this->param));
    }

    ARLOGi("*** Camera Parameter ***\n");
    arParamDisp(&(this->param));

    deleteHandles();

    this->paramLT = arParamLTCreate(&(this->param), AR_PARAM_LT_DEFAULT_OFFSET);
    if (!this->paramLT) {
        ARLOGe("setCamera(): Error: arParamLTCreate for cameraID %d\n", cameraID);
        return -1;
    }

    ARLOGi("setCamera(): arParamLTCreated: %d, %d\n",
           (this->paramLT->param).xsize, (this->paramLT->param).ysize);

    this->arhandle = arCreateHandle(this->paramLT);
    if (this->arhandle == nullptr) {
        ARLOGe("setCamera(): Error: arCreateHandle\n");
        return -1;
    }
    arSetPixelFormat(this->arhandle, this->pixFormat);

    this->ar3DHandle = ar3DCreateHandle(&(this->param));
    if (this->ar3DHandle == nullptr) {
        ARLOGe("setCamera(): Error: ar3DCreateHandle\n");
        return -1;
    }

    recalculateCameraLens();
    return 0;
}

int ARNFTTracker::setupAR2() {
    if (!this->paramLT) {
        ARLOGe("setupAR2(): paramLT is null, call setup() first!\n");
        return -1;
    }

    AR2HandleT* tempHandle = ar2CreateHandleMod(this->paramLT, this->pixFormat);
    if (tempHandle == nullptr) {
        ARLOGe("setupAR2(): Error: ar2CreateHandleMod failed\n");
        return -1;
    }
    this->ar2Handle = tempHandle;

    ar2SetTrackingThresh(this->ar2Handle, 5.0);
    ar2SetSimThresh(this->ar2Handle, 0.50);
    ar2SetSearchFeatureNum(this->ar2Handle, 16);
    ar2SetSearchSize(this->ar2Handle, 6);
    ar2SetTemplateSize1(this->ar2Handle, 6);
    ar2SetTemplateSize2(this->ar2Handle, 6);

    this->kpmHandle = kpmCreateHandle(this->paramLT);
    if (!this->kpmHandle) {
        ARLOGe("setupAR2(): Error creating KPM handle\n");
        return -1;
    }

    ARLOGi("setupAR2(): AR2 and KPM handles initialized successfully.\n");
    return 0;
}

void ARNFTTracker::setProjectionPlanes(ARdouble nearP, ARdouble farP) {
    this->nearPlane = nearP;
    this->farPlane = farP;
    recalculateCameraLens();
}

void ARNFTTracker::recalculateCameraLens() {
    if (this->paramLT) {
        arglCameraFrustumRH(&((this->paramLT)->param), this->nearPlane, this->farPlane, this->cameraLens);
        ARLOGi("Camera frustum recalculated (near: %f, far: %f)\n", this->nearPlane, this->farPlane);
    }
}

void ARNFTTracker::deleteHandles() {
    if (this->arhandle != nullptr) {
        arPattDetach(this->arhandle);
        arDeleteHandle(this->arhandle);
        this->arhandle = nullptr;
    }
    if (this->ar3DHandle != nullptr) {
        ar3DDeleteHandle(&(this->ar3DHandle));
        this->ar3DHandle = nullptr;
    }
    if (this->paramLT != nullptr) {
        arParamLTFree(&(this->paramLT));
        this->paramLT = nullptr;
    }
    if (this->ar2Handle != nullptr) {
        ar2DeleteHandleMod(&(this->ar2Handle));
        this->ar2Handle = nullptr;
    }
    if (this->kpmHandle != nullptr) {
        kpmDeleteHandle(&(this->kpmHandle));
        this->kpmHandle = nullptr;
    }
}

void ARNFTTracker::teardown() {
    videoFrame.reset();
    videoLuma.reset();
    videoFrameSize = 0;
    deleteHandles();
}

void ARNFTTracker::setLogLevel(int level) {
    if (level >= 0) {
        arLogLevel = level;
    }
}

int ARNFTTracker::getLogLevel() const {
    return arLogLevel;
}
