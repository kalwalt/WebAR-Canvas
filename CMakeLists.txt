cmake_minimum_required(VERSION 3.19)
project(WebAR-Canvas)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    add_compile_options(-Wno-error=c++20-extensions)
    add_compile_options(-Wno-c++20-extensions)
    add_compile_options(-fno-rtti) # Match Filament build config
    add_definitions(-DWEBGL -DEMSCRIPTEN -D__EMSCRIPTEN__ -DHAVE_NFT=1)
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
set(MATC_EXEC "/src/third_party/filament/out/host/tools/matc/matc" CACHE STRING "" FORCE)

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

# --- WebARKitLib Sources ---
file(GLOB AR_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/AR/*.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR/arLabelingSub/*.c
)

file(GLOB ARICP_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/ARICP/*.c
)

set(ARUTIL_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/ARUtil/log.c
    ${WEBARKITLIB_DIR}/lib/SRC/ARUtil/file_utils.c
)

set(AR2_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/coord.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/featureMap.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/featureSet.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/handle.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/imageSet.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/jpeg.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/marker.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/matching.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/matching2.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/searchPoint.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/selectTemplate.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/surface.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/template.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/tracking.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/tracking2d.c
    ${WEBARKITLIB_DIR}/lib/SRC/AR2/util.c
)

set(KPM_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmHandle.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmRefDataSet.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmMatching.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmResult.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmUtil.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/kpmFopen.c
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/detectors/DoG_scale_invariant_detector.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/detectors/gaussian_scale_space_pyramid.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/detectors/gradients.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/detectors/orientation_assignment.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/detectors/pyramid.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/facade/visual_database_facade.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/matchers/hough_similarity_voting.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/matchers/freak.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/framework/date_time.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/framework/image.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/framework/logger.cpp
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher/framework/timers.cpp
)

set(WEBARKIT_EXTRA_SOURCES
    ${WEBARKITLIB_DIR}/lib/SRC/WebARKit/WebARKitLog.cpp
    ${WEBARKITLIB_DIR}/WebARKit/WebARKitVideoLuma.cpp
)

set(NFT_CUSTOM_SOURCES
    src/nft/trackingMod.c
    src/nft/trackingMod2d.c
    src/nft/markerDecompress.c
    src/nft/ARNFTTracker.cpp
)

set(WEBARKIT_NFT_SOURCES
    ${AR_SOURCES}
    ${ARICP_SOURCES}
    ${ARUTIL_SOURCES}
    ${AR2_SOURCES}
    ${KPM_SOURCES}
    ${WEBARKIT_EXTRA_SOURCES}
    ${NFT_CUSTOM_SOURCES}
)

add_library(WebARKitNFT STATIC ${WEBARKIT_NFT_SOURCES})

target_include_directories(WebARKitNFT PUBLIC
    ${WEBARKITLIB_DIR}/include
    ${WEBARKITLIB_DIR}/WebARKit/include
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nft
)

target_compile_definitions(WebARKitNFT PUBLIC
    HAVE_NFT=1
)

target_compile_options(WebARKitNFT PUBLIC
    "-sUSE_ZLIB=1"
    "-sUSE_LIBJPEG=1"
    "-Wno-warn-absolute-paths"
    "-Wno-return-type-c-linkage"
)

set(SOURCES src/main.cpp src/ARBaseEngine.cpp ${MATERIAL_OUTPUT} ${CUBE_MATERIAL_OUTPUT})
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE 
    src/
    src/nft/
    ${FILAMENT_SOURCE_DIR}/include
    ${FILAMENT_SOURCE_DIR}/backend/include
    ${FILAMENT_SOURCE_DIR}/libs/math/include
    ${FILAMENT_SOURCE_DIR}/libs/utils/include
    ${WEBARKITLIB_DIR}/include
    ${WEBARKITLIB_DIR}/WebARKit/include
    ${WEBARKITLIB_DIR}/lib/SRC/KPM/FreakMatcher
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
    WebARKitNFT
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

add_dependencies(${PROJECT_NAME} filament WebARKitNFT)