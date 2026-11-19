cmake_minimum_required(VERSION 3.19)
# The release version: bump it here (and in CHANGELOG.md) for every release.
project(WebAR-Canvas VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    add_compile_options(-Wno-error=c++20-extensions)
    add_compile_options(-Wno-c++20-extensions)
    add_compile_options(-fno-rtti) # Match Filament build config
    add_definitions(-DWEBGL -DEMSCRIPTEN -D__EMSCRIPTEN__)
endif()

set(FILAMENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/filament)
set(WEBARKITLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/WebARKitLib)
set(SOKOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol)

# --- Rendering backend (#13) ---
# filament (default): Google Filament, with its host tools (matc, ...) and glTF through gltfio.
# sokol: sokol_gfx (vendored headers in third_party/sokol), WebGL 2; Filament is not configured
# at all, so neither its submodule nor its host tools are needed.
set(WEBAR_RENDERER "filament" CACHE STRING "Rendering backend: filament or sokol")
set_property(CACHE WEBAR_RENDERER PROPERTY STRINGS filament sokol)
if(NOT WEBAR_RENDERER STREQUAL "filament" AND NOT WEBAR_RENDERER STREQUAL "sokol")
    message(FATAL_ERROR "WEBAR_RENDERER must be filament or sokol, not '${WEBAR_RENDERER}'")
endif()
message(STATUS "WebAR-Canvas renderer: ${WEBAR_RENDERER}")

if(WEBAR_RENDERER STREQUAL "filament")
# --- KEY CONFIGURATION ---
# 1. DISABLE Desktop OpenGL (this removes the dependency on BlueGL which caused the error)
set(FILAMENT_SUPPORTS_OPENGL ON CACHE BOOL "" FORCE)

# 2. ENABLE GLES (This is what Filament uses for WebGL)
set(FILAMENT_SUPPORTS_GLES ON CACHE BOOL "" FORCE)

# 3. Other Web settings
set(FILAMENT_BUILD_FOR_WEB ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SAMPLES ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SDL2 ON CACHE BOOL "" FORCE)
set(WEBGL ON CACHE BOOL "Force WebGL" FORCE)

# Setup MATC
# Default: the host tool built by scripts/build-host-tools.sh; -DMATC_EXEC=... overrides it.
set(MATC_EXEC "${FILAMENT_SOURCE_DIR}/out/host/tools/matc/matc" CACHE FILEPATH "Filament matc host tool")

# --- Include Filament ---
add_subdirectory(${FILAMENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/filament_build EXCLUDE_FROM_ALL)

# --- Material Compilation ---
set(MATERIAL_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/materials/video_background.mat)
set(MATERIAL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/video_background.fil)

add_custom_command(
    OUTPUT ${MATERIAL_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMAND ${MATC_EXEC} -p mobile -o ${MATERIAL_OUTPUT} ${MATERIAL_INPUT}
    DEPENDS ${MATERIAL_INPUT}
    COMMENT "Compiling Filament material: video_background.mat"
)

set(CUBE_MATERIAL_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/materials/ar_cube.mat)
set(CUBE_MATERIAL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/ar_cube.fil)

add_custom_command(
    OUTPUT ${CUBE_MATERIAL_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMAND ${MATC_EXEC} -p mobile -o ${CUBE_MATERIAL_OUTPUT} ${CUBE_MATERIAL_INPUT}
    DEPENDS ${CUBE_MATERIAL_INPUT}
    COMMENT "Compiling Filament material: ar_cube.mat"
)
endif() # WEBAR_RENDERER filament

# --- WebARKitLib NFT (AR, ARICP, AR2, KPM, ARUtil + trackingMod/markerDecompress) ---
# Only the NFT subdirectory is added, so no OpenCV is fetched.
set(WEBARKIT_NFT_THREADS OFF CACHE BOOL "" FORCE)
add_subdirectory(${WEBARKITLIB_DIR}/WebARKit/WebARKitTrackers/WebARKitNFT ${CMAKE_CURRENT_BINARY_DIR}/webarkitnft_build)

set(WEBARKIT_EXTRA_SOURCES
    # The std::string logger used by WebARKitVideoLuma (<WebARKit/WebARKitLog.h>). WebARKitNFT
    # compiles a different one, WebARKit/WebARKitLog.cpp (webarkitLog), for the tracking core.
    ${WEBARKITLIB_DIR}/lib/SRC/WebARKit/WebARKitLog.cpp
    ${WEBARKITLIB_DIR}/WebARKit/WebARKitVideoLuma.cpp
)

# The KPM worker (issue #16): KPM detection in a Web Worker, with videoTest.kpm.js/.wasm built
# with the app and deployed next to it (four files). OFF builds the app alone, with the
# in-process detector, exactly as before issue #16.
option(WEBAR_KPM_WORKER "Run KPM detection in a Web Worker (videoTest.kpm.js/.wasm)" ON)
set(WEBAR_KPM_WORKER_EXPORTS "")
if(WEBAR_KPM_WORKER)
    set(WEBAR_KPM_WORKER_EXPORTS ",'_webar_kpm_deliver','_malloc','_free'")
endif()

add_library(ARNFTTracker STATIC
    src/nft/ARNFTTracker.cpp
    # The worker-backed KPM detector and its main-thread fallback (issue #16)
    src/nft/KpmWorkerLink.cpp
    src/nft/WorkerKpmDetector.cpp
    src/nft/WorkerTransport.cpp
    ${WEBARKIT_EXTRA_SOURCES}
)

target_include_directories(ARNFTTracker PUBLIC
    ${WEBARKITLIB_DIR}/WebARKit/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nft
)

target_link_libraries(ARNFTTracker PUBLIC WebARKitNFT)
if(WEBAR_KPM_WORKER)
    target_compile_definitions(ARNFTTracker PUBLIC WEBAR_KPM_WORKER=1)
endif()

target_compile_options(ARNFTTracker PUBLIC
    "-Wno-warn-absolute-paths"
    "-Wno-return-type-c-linkage"
)

# --- The KPM-only worker module (issue #16): videoTest.kpm.js/.wasm ---
# Runs the KPM detection pass in a Web Worker, with its own small WASM instance. No -g (it would
# grow the module from 86 KB to 637 KB gzipped), no embedded files: the main instance sends the
# camera and each marker's .fset3 bytes.
set(KPM_WORKER_SOURCES src/worker/kpm_worker.cpp)
set(KPM_WORKER_MAIN_JS ${CMAKE_CURRENT_SOURCE_DIR}/src/worker/kpm_worker_main.js)
# EXCLUDE_FROM_ALL: built as a dependency of the app with WEBAR_KPM_WORKER, or when asked for.
add_executable(kpm_worker EXCLUDE_FROM_ALL ${KPM_WORKER_SOURCES})
set_property(TARGET kpm_worker APPEND PROPERTY LINK_DEPENDS ${KPM_WORKER_MAIN_JS})
target_include_directories(kpm_worker PRIVATE
    ${WEBARKITLIB_DIR}/WebARKit/WebARKitTrackers/WebARKitNFT/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/worker
)
target_link_libraries(kpm_worker PRIVATE WebARKitNFT)
set_target_properties(kpm_worker PROPERTIES OUTPUT_NAME "videoTest.kpm" SUFFIX ".js")
target_link_options(kpm_worker PRIVATE
    "-O3"
    "-sENVIRONMENT=worker"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=33554432"
    "-sEXPORTED_FUNCTIONS=['_kw_init','_kw_add_marker','_kw_match','_kw_result_num','_kw_detections','_kw_detection_size','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAP32','HEAPF32','HEAPF64']"
    # The worker's onmessage handler (the protocol's worker side)
    "--pre-js=${KPM_WORKER_MAIN_JS}"
)

# --- Version header (WEBAR_CANVAS_VERSION_*), generated from project(VERSION) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebARCanvasVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/WebARCanvasVersion.h
    @ONLY
)

set(SOURCES
    src/main.cpp
    src/app/App.cpp
    src/core/ARBaseEngine.cpp
    src/core/ARBaseApp.cpp
    src/core/ARInput.cpp
    src/core/ARVideoTexture.cpp
)
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src/
    src/core/
    src/nft/
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(${PROJECT_NAME} PUBLIC ARNFTTracker)

if(WEBAR_RENDERER STREQUAL "filament")
    target_sources(${PROJECT_NAME} PRIVATE
        src/core/FilamentRenderer.cpp
        ${MATERIAL_OUTPUT}
        ${CUBE_MATERIAL_OUTPUT}
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${FILAMENT_SOURCE_DIR}/include
        ${FILAMENT_SOURCE_DIR}/backend/include
        ${FILAMENT_SOURCE_DIR}/libs/math/include
        ${FILAMENT_SOURCE_DIR}/libs/utils/include
    )
    # Here is the magic to make the backend work without BlueGL
    target_link_libraries(${PROJECT_NAME} PUBLIC
        filament
        # We use --whole-archive on backend to prevent the linker from removing the GLES driver
        "-Wl,--whole-archive" backend "-Wl,--no-whole-archive"
        utils
        ibl
        geometry
    )
    # The Filament backend: enables the Filament API of ARBaseApp (getEngine, createBox, ...).
    target_compile_definitions(${PROJECT_NAME} PRIVATE WEBAR_RENDERER_FILAMENT=1)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-sFULL_ES3=1"
        "--embed-file=${MATERIAL_OUTPUT}@/assets/video_background.fil"
        "--embed-file=${CUBE_MATERIAL_OUTPUT}@/assets/ar_cube.fil"
    )
    add_dependencies(${PROJECT_NAME} filament)
else()
    # sokol_gfx, GLES3 backend (WebGL 2); the implementation is compiled in sokol_impl.cpp.
    target_sources(${PROJECT_NAME} PRIVATE
        src/core/SokolRenderer.cpp
        src/core/sokol_impl.cpp
    )
    target_include_directories(${PROJECT_NAME} PRIVATE ${SOKOL_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE WEBAR_RENDERER_SOKOL=1 SOKOL_GLES3=1)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "videoTest")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

target_link_options(${PROJECT_NAME} PRIVATE
    "-O3"
    "-sUSE_WEBGL2=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=268435456"
    "-sSTACK_SIZE=5242880"
    "-sUSE_LIBJPEG=1"
    "-sUSE_ZLIB=1"
    "-lhtml5"
    "-lGL" # This links the native WebGL of Emscripten
    # webar_load_model / webar_place_model are EMSCRIPTEN_KEEPALIVE; pages call them with ccall.
    # With the KPM worker, WorkerTransport's JavaScript calls webar_kpm_deliver, malloc and free.
    "-sEXPORTED_FUNCTIONS=['_main','_webar_load_model','_webar_place_model'${WEBAR_KPM_WORKER_EXPORTS}]"
    "-sEXPORTED_RUNTIME_METHODS=['GL','UTF8ToString','ccall','FS_createDataFile']"
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/src/shell.html"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/camera_para.dat@/assets/camera_para.dat"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.fset@/assets/markers/pinball.fset"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.fset3@/assets/markers/pinball.fset3"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.iset@/assets/markers/pinball.iset"
)

# --- Debug information (#23) ---
# OFF is the published build: what CI tests and Pages deploys. ON links the app with -g, which keeps
# function names for readable stack traces but makes the download several times larger. -g is a
# link option only, so there are no source lines for this repository's code (only for libc/libc++).
# CI and Pages check the result with scripts/check-wasm-debug.js.
# scripts/build-wasm.sh passes it on every configure (WEBAR_DEBUG_INFO, node build-docker.js --debug).
# The KPM worker module never gets -g (see kpm_worker above).
option(WEBAR_DEBUG_INFO "Link the app with -g (debug information, for development only)" OFF)
if(WEBAR_DEBUG_INFO)
    target_link_options(${PROJECT_NAME} PRIVATE "-g")
endif()

add_dependencies(${PROJECT_NAME} ARNFTTracker)

if(WEBAR_KPM_WORKER)
    add_dependencies(${PROJECT_NAME} kpm_worker)
endif()

# --- glTF models through Filament's gltfio (needs the uberz host tool for uberarchive) ---
option(WEBAR_CANVAS_GLTF "Load glTF models with Filament's gltfio (ARBaseApp::loadModel)" ON)
if(WEBAR_CANVAS_GLTF AND NOT WEBAR_RENDERER STREQUAL "filament")
    message(STATUS "WEBAR_CANVAS_GLTF is ignored with WEBAR_RENDERER=${WEBAR_RENDERER}: glTF needs Filament's gltfio")
    set(WEBAR_CANVAS_GLTF OFF) # this configuration only: the cached option keeps its value
endif()
if(WEBAR_CANVAS_GLTF)
    target_sources(${PROJECT_NAME} PRIVATE src/core/ARModel.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WEBAR_CANVAS_GLTF=1)
    target_link_libraries(${PROJECT_NAME} PUBLIC gltfio_core uberarchive)
    # The demo's model: AnimatedMorphCube (CC0, Microsoft) from the Filament submodule.
    target_link_options(${PROJECT_NAME} PRIVATE
        "--embed-file=${FILAMENT_SOURCE_DIR}/third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb@/assets/models/AnimatedMorphCube.glb"
    )
endif()

# --- Node test runners (scripts/test-wasm.sh, node build-docker.js --test) ---
option(WEBAR_CANVAS_BUILD_TESTS "Build the Node test runners (nft_tests, core_tests)" OFF)
if(WEBAR_CANVAS_BUILD_TESTS)
    # kpm_worker.cpp too: a loopback transport runs the worker's entry points in-process.
    add_executable(nft_tests tests/nft_tests.cpp ${KPM_WORKER_SOURCES})
    target_include_directories(nft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/worker)
    target_link_libraries(nft_tests PRIVATE ARNFTTracker)
    set_target_properties(nft_tests PROPERTIES SUFFIX ".js")
    target_link_options(nft_tests PRIVATE
        # WorkerTransport's JavaScript (linked through ARNFTTracker) calls malloc and free.
        "-sEXPORTED_FUNCTIONS=['_main'${WEBAR_KPM_WORKER_EXPORTS}]"
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sASSERTIONS=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=268435456"
        "-sUSE_LIBJPEG=1"
        "-sUSE_ZLIB=1"
        # WebARKitLib's NFT fixtures: camera_para.dat, pinball.{iset,fset,fset3}, pinball-demo.jpg
        "--embed-file=${WEBARKITLIB_DIR}/tests/data@/data"
    )

    # The KPM worker module's entry points, compiled into a Node runner on the same fixtures.
    add_executable(kpm_worker_tests tests/kpm_worker_tests.cpp ${KPM_WORKER_SOURCES})
    target_include_directories(kpm_worker_tests PRIVATE
        ${WEBARKITLIB_DIR}/WebARKit/WebARKitTrackers/WebARKitNFT/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/worker
    )
    target_link_libraries(kpm_worker_tests PRIVATE WebARKitNFT)
    set_target_properties(kpm_worker_tests PROPERTIES SUFFIX ".js")
    target_link_options(kpm_worker_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sASSERTIONS=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sUSE_LIBJPEG=1"
        "-sUSE_ZLIB=1"
        "--embed-file=${WEBARKITLIB_DIR}/tests/data@/data"
    )

    # A browser check of the worker path, built only when asked for (cmake --build ... --target
    # kpm_worker_browser) and opened with tests/kpm_worker_browser.html; not run in CI.
    if(WEBAR_KPM_WORKER)
        add_executable(kpm_worker_browser EXCLUDE_FROM_ALL tests/kpm_worker_browser.cpp)
        target_link_libraries(kpm_worker_browser PRIVATE ARNFTTracker)
        set_target_properties(kpm_worker_browser PROPERTIES SUFFIX ".js")
        target_link_options(kpm_worker_browser PRIVATE
            "-sENVIRONMENT=web"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=134217728"
            "-sUSE_LIBJPEG=1"
            "-sUSE_ZLIB=1"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_webar_kpm_deliver','_kwb_start','_kwb_step','_kwb_state','_kwb_found','_kwb_page','_kwb_z','_kwb_running']"
            "-sEXPORTED_RUNTIME_METHODS=['stringToNewUTF8']"
            "--embed-file=${WEBARKITLIB_DIR}/tests/data@/data"
        )
    endif()

    # Pure C++ parts of the framework core (src/core/AREvents.h, ARAppConfig.h, ARAnchor.h, ARInput.h,
    # ARMeshAnchors.h, ARPrimitives.h): no renderer, no tracker.
    add_executable(core_tests tests/core_tests.cpp)
    target_include_directories(core_tests PRIVATE src/)
    set_target_properties(core_tests PROPERTIES SUFFIX ".js")
    target_link_options(core_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sASSERTIONS=1"
    )
endif()