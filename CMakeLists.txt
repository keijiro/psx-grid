cmake_minimum_required(VERSION 3.21)
project(psx-grid LANGUAGES C VERSION 0.1.0 DESCRIPTION "Score Plane sine sequencer")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(UI_ATLAS "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_atlas.h")
add_custom_command(OUTPUT "${UI_ATLAS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-assets.py" "${UI_ATLAS}"
    DEPENDS scripts/generate-assets.py assets/ui/masks.py
    VERBATIM)
set(SINE_SAMPLES "${CMAKE_CURRENT_BINARY_DIR}/generated/sine_samples.h")
add_custom_command(OUTPUT "${SINE_SAMPLES}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-audio.py" "${SINE_SAMPLES}"
    DEPENDS scripts/generate-audio.py
    VERBATIM)
psn00bsdk_add_executable(psx-grid GPREL src/main.c src/input.c src/score.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${SINE_SAMPLES}")
target_include_directories(psx-grid PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(psx-grid PRIVATE -Wall -Wextra)

# Keep the interrupt path within its deadline in Debug as well. Symbols remain
# available; the emulator fixture measures the same optimized audio code in both.
set_source_files_properties(src/sequencer.c src/audio.c src/audio_psx.c src/pad.c PROPERTIES COMPILE_OPTIONS -O2)

# PSn00bSDK GPU packet macros intentionally alias the DMA tag types.
set_source_files_properties(src/render.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)

option(AUDIO_FIXTURE "Build the automated audio and input development fixtures" OFF)
if(AUDIO_FIXTURE)
    psn00bsdk_add_executable(audio-fixture GPREL tests/audio_fixture.c src/score.c src/input.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${SINE_SAMPLES}")
    target_include_directories(audio-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(audio-fixture PRIVATE -Wall -Wextra)
    psn00bsdk_add_executable(input-fixture GPREL tests/input_fixture.c src/score.c src/input.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${SINE_SAMPLES}")
    target_include_directories(input-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(input-fixture PRIVATE -Wall -Wextra)
endif()
