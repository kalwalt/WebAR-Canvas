cmake_minimum_required(VERSION 3.19)
project(WebAR-Canvas)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    add_compile_options(-Wno-error=c++20-extensions)
    add_compile_options(-Wno-c++20-extensions)
    add_definitions(-DWEBGL -DEMSCRIPTEN -D__EMSCRIPTEN__)
endif()

set(FILAMENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/filament)

# --- CONFIGURAZIONE CHIAVE ---
# 1. DISABILITA OpenGL Desktop (questo rimuove la dipendenza da BlueGL che causava l'errore)
set(FILAMENT_SUPPORTS_OPENGL ON CACHE BOOL "" FORCE)

# 2. ABILITA GLES (Questo è ciò che Filament usa per WebGL)
set(FILAMENT_SUPPORTS_GLES ON CACHE BOOL "" FORCE)

# 3. Altre impostazioni Web
set(FILAMENT_BUILD_FOR_WEB ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SAMPLES ON CACHE BOOL "" FORCE)
set(FILAMENT_SKIP_SDL2 ON CACHE BOOL "" FORCE)
set(WEBGL ON CACHE BOOL "Force WebGL" FORCE)

# Setup MATC
set(MATC_EXEC "/src/third_party/filament/out/host/tools/matc/matc" CACHE STRING "" FORCE)

# --- Includi Filament ---
add_subdirectory(${FILAMENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/filament_build EXCLUDE_FROM_ALL)

# --- Compilazione Materiali ---
set(MATERIAL_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/materials/video_background.mat)
set(MATERIAL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/video_background.fil)

add_custom_command(
    OUTPUT ${MATERIAL_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMAND ${MATC_EXEC} -p mobile -o ${MATERIAL_OUTPUT} ${MATERIAL_INPUT}
    DEPENDS ${MATERIAL_INPUT}
    COMMENT "Compiling Filament material: video_background.mat"
)

set(SOURCES src/main.cpp src/ARBaseEngine.cpp ${MATERIAL_OUTPUT})
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE 
    src/
    ${FILAMENT_SOURCE_DIR}/include
    ${FILAMENT_SOURCE_DIR}/libs/math/include
    ${FILAMENT_SOURCE_DIR}/libs/utils/include
    # NOTA: Niente include di bluegl qui!
)

# --- LINKING ---
# Qui sta la magia per far funzionare il backend senza BlueGL
target_link_libraries(${PROJECT_NAME} PUBLIC
    filament
    # Usiamo --whole-archive su backend per evitare che il linker rimuova il driver GLES
    "-Wl,--whole-archive" backend "-Wl,--no-whole-archive"
    utils
    ibl
    geometry
    # Rimosso: bluegl (non serve su WebGL puro)
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
    "-lhtml5"
    "-lGL" # Questo linka il WebGL nativo di Emscripten
    "-sEXPORTED_FUNCTIONS=['_main']"
    "-sEXPORTED_RUNTIME_METHODS=['GL']"
    "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/src/shell.html"
    "--embed-file" "${MATERIAL_OUTPUT}@/assets/video_background.fil"
    #"--embed-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets/markers@/assets/markers"
)

add_dependencies(${PROJECT_NAME} filament)