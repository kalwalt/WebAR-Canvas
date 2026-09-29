#include "ARNFTTracker.h"
#include <emscripten.h>
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

    if (lumaInfo) {
        webarkit::webarkitVideoLumaFinal(&lumaInfo);
        lumaInfo = nullptr;
    }
    lumaInfo = webarkit::webarkitVideoLumaInit(w, h, true);

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

int ARNFTTracker::loadMarker(const std::string &datasetPathname) {
    std::vector<std::string> names = { datasetPathname };
    auto ids = loadMarkers(names);
    if (ids.empty()) {
        return -1;
    }
    return ids[0];
}

std::vector<int> ARNFTTracker::loadMarkers(const std::vector<std::string> &datasetPathnames) {
    if (!this->kpmHandle) {
        ARLOGe("loadMarkers(): kpmHandle is null! Call setupAR2() before loading markers.\n");
        return {};
    }

    if (datasetPathnames.size() > static_cast<size_t>(NFT_PAGES_MAX - this->surfaceSetCount)) {
        ARLOGe("loadMarkers(): Exceeded maximum allowed markers (%d).\n", NFT_PAGES_MAX);
        return {};
    }

    KpmRefDataSet *refDataSet = nullptr;
    std::vector<int> markerIds;

    for (size_t i = 0; i < datasetPathnames.size(); ++i) {
        const std::string &path = datasetPathnames[i];
        int pageNo = this->surfaceSetCount + i;
        ARLOGi("Loading NFT marker [%d] from '%s'...\n", pageNo, path.c_str());

        // 1. Load KPM dataset (.fset3)
        KpmRefDataSet *refDataSet2 = nullptr;
        ARLOGi("Reading KPM dataset: %s.fset3\n", path.c_str());
        if (kpmLoadRefDataSet(path.c_str(), "fset3", &refDataSet2) < 0) {
            ARLOGe("Error reading KPM dataset from %s.fset3\n", path.c_str());
            if (refDataSet) kpmDeleteRefDataSet(&refDataSet);
            return {};
        }

        if (kpmChangePageNoOfRefDataSet(refDataSet2, KpmChangePageNoAllPages, pageNo) < 0) {
            ARLOGe("Error in kpmChangePageNoOfRefDataSet for page %d\n", pageNo);
            kpmDeleteRefDataSet(&refDataSet2);
            if (refDataSet) kpmDeleteRefDataSet(&refDataSet);
            return {};
        }

        if (kpmMergeRefDataSet(&refDataSet, &refDataSet2) < 0) {
            ARLOGe("Error in kpmMergeRefDataSet for page %d\n", pageNo);
            if (refDataSet) kpmDeleteRefDataSet(&refDataSet);
            return {};
        }

        // 2. Load AR2 surface set (.fset and .iset)
        ARLOGi("Reading AR2 surface set: %s.fset\n", path.c_str());
        int slot = this->surfaceSetCount + i;
        this->surfaceSet[slot] = ar2ReadSurfaceSet(path.c_str(), "fset", nullptr);
        if (this->surfaceSet[slot] == nullptr) {
            ARLOGe("Error reading AR2 surface set from %s.fset\n", path.c_str());
            if (refDataSet) kpmDeleteRefDataSet(&refDataSet);
            return {};
        }

        // 3. Extract marker metadata
        NFTMarkerData data;
        data.id = pageNo;
        if (this->surfaceSet[slot]->surface && this->surfaceSet[slot]->surface[0].imageSet) {
            auto *imgSet = this->surfaceSet[slot]->surface[0].imageSet;
            if (imgSet->scale && imgSet->scale[0]) {
                data.width = imgSet->scale[0]->xsize;
                data.height = imgSet->scale[0]->ysize;
                data.dpi = imgSet->scale[0]->dpi;
            }
        }
        this->markersData.push_back(data);
        this->markerStates[slot] = NFTMarkerState{};
        markerIds.push_back(pageNo);

        ARLOGi("NFT marker [%d] loaded successfully: resolution=%dx%d, dpi=%d\n",
               data.id, data.width, data.height, data.dpi);
    }

    // 4. Bind merged reference dataset to KPM handle
    if (kpmSetRefDataSet(this->kpmHandle, refDataSet) < 0) {
        ARLOGe("Error binding refDataSet to KPM handle via kpmSetRefDataSet\n");
        kpmDeleteRefDataSet(&refDataSet);
        return {};
    }
    kpmDeleteRefDataSet(&refDataSet);

    this->surfaceSetCount += markerIds.size();
    ARLOGi("All NFT markers loaded. Total active markers: %d\n", this->surfaceSetCount);
    return markerIds;
}

void ARNFTTracker::updateVideoLuma() {
    if (lumaInfo && videoFrame && videoLuma) {
        uint8_t *out = webarkit::webarkitVideoLuma(lumaInfo, videoFrame.get());
        if (out) {
            std::copy(out, out + (width * height), videoLuma.get());
        }
    }
}

int ARNFTTracker::update() {
    updateVideoLuma();
    detectNFTMarker();
    return anyMarkerTracked() ? 1 : 0;
}

int ARNFTTracker::detectNFTMarker() {
    if (surfaceSetCount <= 0 || !kpmHandle) {
        return -1;
    }

    KpmResult *kpmResult = nullptr;
    int kpmResultNum = -1;

    const double now = emscripten_get_now();
    const bool detectionDue = !anyMarkerTracked() ||
        (this->continuousDetection && (now - this->lastKpmEndMs >= this->detectionIntervalMs));

    if (!allMarkersTracked() && detectionDue) {
        int skipPages[NFT_PAGES_MAX];
        int skipNum = 0;
        for (int i = 0; i < this->surfaceSetCount; ++i) {
            if (markerStates[i].tracking) {
                skipPages[skipNum++] = i;
            }
        }
        if (skipNum > 0) {
            kpmSetMatchingSkipPage(this->kpmHandle, skipPages, skipNum);
        }

        kpmMatching(this->kpmHandle, this->videoLuma.get());
        kpmGetResult(this->kpmHandle, &kpmResult, &kpmResultNum);
        this->lastKpmEndMs = emscripten_get_now();

        for (int i = 0; i < kpmResultNum; ++i) {
            if (kpmResult[i].camPoseF != 0) continue;
            const int page = kpmResult[i].pageNo;
            if (page < 0 || page >= this->surfaceSetCount) continue;

            NFTMarkerState &state = markerStates[page];
            if (state.tracking) continue;

            ar2SetInitTrans(this->surfaceSet[page], kpmResult[i].camPose);
            state.tracking = true;
            state.filterNeedsReset = true;
            ARLOGi("KPM recognized marker page %d!\n", page);
        }
    }

    trackMarkers();
    return kpmResultNum;
}

void ARNFTTracker::trackMarkers() {
    if (!this->ar2Handle) return;

    for (int page = 0; page < this->surfaceSetCount; ++page) {
        NFTMarkerState &state = markerStates[page];
        if (!state.tracking) continue;

        float trans[3][4];
        float err = -1.0f;
        const int trackResult = ar2TrackingMod(this->ar2Handle, this->surfaceSet[page],
                                               this->videoFrame.get(), trans, &err);
        if (trackResult < 0) {
            ARLOGi("Tracking lost on marker page %d (code %d)\n", page, trackResult);
            state.tracking = false;
            state.err = -1.0f;
            continue;
        }

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                state.pose[r][c] = static_cast<ARdouble>(trans[r][c]);
            }
        }

        if (withFiltering) {
            if (!state.ftmi) {
                state.ftmi = arFilterTransMatInit(this->filterSampleRate, this->filterCutoffFrequency);
                state.filterNeedsReset = true;
            }
            if (arFilterTransMat(state.ftmi, state.pose, state.filterNeedsReset ? 1 : 0) < 0) {
                ARLOGe("arFilterTransMat error on marker %d\n", page);
            }
            state.filterNeedsReset = false;
        }
        state.err = err;
    }
}

bool ARNFTTracker::isMarkerTracked(int markerIndex) const {
    if (markerIndex >= 0 && markerIndex < surfaceSetCount) {
        return markerStates[markerIndex].tracking;
    }
    return false;
}

bool ARNFTTracker::anyMarkerTracked() const {
    for (int i = 0; i < surfaceSetCount; ++i) {
        if (markerStates[i].tracking) return true;
    }
    return false;
}

bool ARNFTTracker::allMarkersTracked() const {
    if (surfaceSetCount == 0) return false;
    for (int i = 0; i < surfaceSetCount; ++i) {
        if (!markerStates[i].tracking) return false;
    }
    return true;
}

float ARNFTTracker::getTrackingError(int markerIndex) const {
    if (markerIndex >= 0 && markerIndex < surfaceSetCount) {
        return markerStates[markerIndex].err;
    }
    return -1.0f;
}

void ARNFTTracker::getPose(int markerIndex, ARdouble pose[3][4]) const {
    if (markerIndex >= 0 && markerIndex < surfaceSetCount && markerStates[markerIndex].tracking) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                pose[r][c] = markerStates[markerIndex].pose[r][c];
            }
        }
    } else {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                pose[r][c] = 0.0;
            }
        }
    }
}

void ARNFTTracker::getTransformMatrix(int markerIndex, float m[16]) const {
    if (markerIndex < 0 || markerIndex >= surfaceSetCount || !markerStates[markerIndex].tracking) {
        std::fill(m, m + 16, 0.0f);
        return;
    }
    const auto &pose = markerStates[markerIndex].pose;
    // Column 0
    m[0]  = static_cast<float>(pose[0][0]);
    m[1]  = static_cast<float>(pose[1][0]);
    m[2]  = static_cast<float>(pose[2][0]);
    m[3]  = 0.0f;
    // Column 1
    m[4]  = static_cast<float>(pose[0][1]);
    m[5]  = static_cast<float>(pose[1][1]);
    m[6]  = static_cast<float>(pose[2][1]);
    m[7]  = 0.0f;
    // Column 2
    m[8]  = static_cast<float>(pose[0][2]);
    m[9]  = static_cast<float>(pose[1][2]);
    m[10] = static_cast<float>(pose[2][2]);
    m[11] = 0.0f;
    // Column 3
    m[12] = static_cast<float>(pose[0][3]);
    m[13] = static_cast<float>(pose[1][3]);
    m[14] = static_cast<float>(pose[2][3]);
    m[15] = 1.0f;
}

void ARNFTTracker::setFilterParameters(double sampleRate, double cutoffFreq) {
    this->filterSampleRate = sampleRate;
    this->filterCutoffFrequency = cutoffFreq;
    for (int i = 0; i < surfaceSetCount; ++i) {
        if (markerStates[i].ftmi) {
            arFilterTransMatSetParams(markerStates[i].ftmi, sampleRate, cutoffFreq);
        }
    }
}

const NFTMarkerData& ARNFTTracker::getMarkerData(int index) const {
    static const NFTMarkerData emptyData;
    if (index >= 0 && index < static_cast<int>(markersData.size())) {
        return markersData[index];
    }
    return emptyData;
}

const NFTMarkerState& ARNFTTracker::getMarkerState(int index) const {
    static const NFTMarkerState emptyState;
    if (index >= 0 && index < surfaceSetCount) {
        return markerStates[index];
    }
    return emptyState;
}

NFTMarkerState& ARNFTTracker::getMarkerState(int index) {
    if (index >= 0 && index < surfaceSetCount) {
        return markerStates[index];
    }
    return markerStates[0];
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

    if (lumaInfo) {
        webarkit::webarkitVideoLumaFinal(&lumaInfo);
        lumaInfo = nullptr;
    }

    for (int i = 0; i < surfaceSetCount; ++i) {
        if (surfaceSet[i]) {
            ar2FreeSurfaceSet(&surfaceSet[i]);
            surfaceSet[i] = nullptr;
        }
        if (markerStates[i].ftmi) {
            arFilterTransMatFinal(markerStates[i].ftmi);
            markerStates[i].ftmi = nullptr;
        }
    }
    surfaceSetCount = 0;
    markersData.clear();

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
