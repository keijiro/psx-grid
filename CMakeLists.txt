cmake_minimum_required(VERSION 3.21)
project(psx-grid LANGUAGES C VERSION 0.1.0 DESCRIPTION "Score Plane gamepad prototype")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(UI_ATLAS "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_atlas.h")
add_custom_command(OUTPUT "${UI_ATLAS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-assets.py" "${UI_ATLAS}"
    DEPENDS scripts/generate-assets.py assets/ui/masks.py
    VERBATIM)
psn00bsdk_add_executable(psx-grid GPREL src/main.c src/input.c src/score.c src/editor.c src/render.c "${UI_ATLAS}")
target_include_directories(psx-grid PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(psx-grid PRIVATE -Wall -Wextra)

# PSn00bSDK GPU packet macros intentionally alias the DMA tag types.
set_source_files_properties(src/render.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)
