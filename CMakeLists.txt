cmake_minimum_required(VERSION 3.21)
project(psx-grid LANGUAGES C VERSION 0.1.0 DESCRIPTION "Score Plane gamepad prototype")
psn00bsdk_add_executable(psx-grid GPREL src/main.c src/input.c src/score.c src/editor.c src/render.c)
target_compile_options(psx-grid PRIVATE -Wall -Wextra)

# PSn00bSDK GPU packet macros intentionally alias the DMA tag types.
set_source_files_properties(src/render.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)
