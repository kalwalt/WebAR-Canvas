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

# --- WebARKitLib NFT (AR, ARICP, AR2, KPM, ARUtil + trackingMod/markerDecompress) ---
# Only the NFT subdirectory is added, so no OpenCV is fetched.
set(WEBARKIT_NFT_THREADS OFF CACHE BOOL "" FORCE)
add_subdirectory(${WEBARKITLIB_DIR}/WebARKit/WebARKitTrackers/WebARKitNFT ${CMAKE_CURRENT_BINARY_DIR}/webarkitnft_build)

set(WEBARKIT_EXTRA_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/WebARKit/WebARKitLog.cpp
    ${WEBARKITLIB_DIR}/WebARKit/WebARKitVideoLuma.cpp
)

add_library(ARNFTTracker STATIC src/nft/ARNFTTracker.cpp ${WEBARKIT_EXTRA_SOURCES})

target_include_directories(ARNFTTracker PUBLIC
    ${WEBARKITLIB_DIR}/WebARKit/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nft
)

target_link_libraries(ARNFTTracker PUBLIC WebARKitNFT)

target_compile_options(ARNFTTracker PUBLIC
    "-Wno-warn-absolute-paths"
    "-Wno-return-type-c-linkage"
)

# --- Version header (WEBAR_CANVAS_VERSION_*), generated from project(VERSION) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebARCanvasVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/WebARCanvasVersion.h
    @ONLY
)

set(SOURCES src/main.cpp src/ARBaseEngine.cpp ${MATERIAL_OUTPUT} ${CUBE_MATERIAL_OUTPUT})
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE 
    src/
    src/nft/
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${FILAMENT_SOURCE_DIR}/include
    ${FILAMENT_SOURCE_DIR}/backend/include
    ${FILAMENT_SOURCE_DIR}/libs/math/include
    ${FILAMENT_SOURCE_DIR}/libs/utils/include
)

# --- LINKING ---
# Here is the magic to make the backend work without BlueGL
target_link_libraries(${PROJECT_NAME} PUBLIC
    filament
    # We use --whole-archive on backend to prevent the linker from removing the GLES driver
    "-Wl,--whole-archive" backend "-Wl,--no-whole-archive"
    utils
    ibl
    geometry
    ARNFTTracker
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "videoTest")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

target_link_options(${PROJECT_NAME} PRIVATE
    "-O3"
    "-sUSE_WEBGL2=1"
    "-sFULL_ES3=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=268435456"
    "-sSTACK_SIZE=5242880"
    "-sUSE_LIBJPEG=1"
    "-sUSE_ZLIB=1"
    "-lhtml5"
    "-lGL" # This links the native WebGL of Emscripten
    "-sEXPORTED_FUNCTIONS=['_main']"
    "-sEXPORTED_RUNTIME_METHODS=['GL','UTF8ToString']"
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/src/shell.html"
    "--embed-file=${MATERIAL_OUTPUT}@/assets/video_background.fil"
    "--embed-file=${CUBE_MATERIAL_OUTPUT}@/assets/ar_cube.fil"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/camera_para.dat@/assets/camera_para.dat"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.fset@/assets/markers/pinball.fset"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.fset3@/assets/markers/pinball.fset3"
    "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/pinball.iset@/assets/markers/pinball.iset"
    "-g"
)

add_dependencies(${PROJECT_NAME} filament ARNFTTracker)