cmake_minimum_required(VERSION 3.19)
project(WebAR-Canvas)

# Use C++17 as required by Filament
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    add_compile_options(-Wno-error=c++20-extensions)
    add_compile_options(-Wno-c++20-extensions)
endif()

# --- Filament Configuration ---
set(FILAMENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/filament)

# Force Filament to treat the target as Web (Emscripten) and NOT Linux
set(CMAKE_SYSTEM_NAME Emscripten CACHE STRING "" FORCE)

# Additional flags to bypass Linux-specific code in utils
# 2. Indicates to Filament that we are specifically compiling for the Web
set(WEBGL ON CACHE BOOL "" FORCE)
add_definitions(-DWEBGL -DEMSCRIPTEN -D__EMSCRIPTEN__)

# 1. Forces CMake not to identify itself as Linux/Unix
set(LINUX OFF CACHE BOOL "" FORCE)
set(UNIX OFF CACHE BOOL "" FORCE)

# Options to optimize Filament build for WebAssembly
set(FILAMENT_SUPPORTS_OPENGL OFF CACHE BOOL "" FORCE)  # Disable desktop OpenGL
set(FILAMENT_SUPPORTS_GLES ON CACHE BOOL "" FORCE)    # Enable GLES (used by WebGL)
set(FILAMENT_SKIP_SAMPLES ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SDL2 ON CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_VULKAN OFF CACHE BOOL "" FORCE)
set(FILAMENT_SUPPORTS_METAL OFF CACHE BOOL "" FORCE)
set(FILAMENT_BUILD_FOR_WEB ON CACHE BOOL "" FORCE)

# Forces WebGL backend as the only available one
set(FILAMENT_USE_EXTERNAL_GLES3 ON CACHE BOOL "" FORCE)

set(MATC_EXEC "/src/third_party/filament/out/host/tools/matc/matc" CACHE STRING "" FORCE)
message(STATUS "FORCING MATC_EXEC TO: ${MATC_EXEC}")

# Include Filament subdirectory
# Using a separate binary directory to keep things clean
add_subdirectory(${FILAMENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/filament_build EXCLUDE_FROM_ALL)

# --- Asset Compilation (Materials) ---
# Ensure MATC_EXEC is passed via command line: -DMATC_EXEC=/path/to/host/matc
if(NOT MATC_EXEC)
    message(FATAL_ERROR "MATC_EXEC not defined. Please provide the path to the host-compiled matc binary.")
endif()

set(MATERIAL_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/materials/video_background.mat)
set(MATERIAL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/video_background.fil)

add_custom_command(
    OUTPUT ${MATERIAL_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMAND ${MATC_EXEC} -p mobile -o ${MATERIAL_OUTPUT} ${MATERIAL_INPUT}
    DEPENDS ${MATERIAL_INPUT}
    COMMENT "Compiling Filament material: video_background.mat"
)

# --- Source Files ---
set(SOURCES
    src/main.cpp
    src/ARBaseEngine.cpp
    ${MATERIAL_OUTPUT} # Include the generated material so CMake knows to build it
)

# --- Executable Target ---
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE 
    src/
    ${FILAMENT_SOURCE_DIR}/include
    ${FILAMENT_SOURCE_DIR}/libs/math/include
    ${FILAMENT_SOURCE_DIR}/libs/utils/include
)

# --- Linking ---
# When using add_subdirectory, targets are referenced by their plain names
target_link_libraries(${PROJECT_NAME}
    filament
    backend
    bluegl
    utils
    ibl
    # Replace with your actual static library path for the NFT core
    # ${CMAKE_CURRENT_SOURCE_DIR}/libs/libjsartoolkitNFT.a 
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "videoTest")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

# --- Emscripten Linker Options ---
target_link_options(${PROJECT_NAME} PRIVATE
    "-O3"
    "-sUSE_WEBGL2=1"
    "-s FULL_ES3=1"                # Supports extensions needed for the video background
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sALLOW_MEMORY_GROWTH=1"
    "-s INITIAL_MEMORY=268435456"  # 256MB expressed in bytes for greater precision
    "-s STACK_SIZE=5242880"        # Increases stack for AR math operations
    "-lhtml5"                      # Links Emscripten system libraries for the DOM
    "-sEXPORTED_FUNCTIONS=['_main']"
    "-s EXPORTED_RUNTIME_METHODS=['GL']"
    "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "--embed-file ${MATERIAL_OUTPUT}@/assets/video_background.fil"
    "--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/markers@/assets/markers"
    "-lGL"
    "-g"
)

# Ensure the material is built before the executable
add_dependencies(${PROJECT_NAME} filament)