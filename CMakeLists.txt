cmake_minimum_required(VERSION 3.21)
project(psx-grid LANGUAGES C VERSION 0.1.0 DESCRIPTION "Score Plane wavetable sequencer")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(UI_ATLAS "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_atlas.h")
add_custom_command(OUTPUT "${UI_ATLAS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-assets.py" "${UI_ATLAS}"
    DEPENDS scripts/generate-assets.py assets/ui/masks.py assets/ui/jacquard-logo.svg
    VERBATIM)
set(WAVE_SAMPLES "${CMAKE_CURRENT_BINARY_DIR}/generated/wave_samples.h")
set(AUDIO_TABLES "${CMAKE_CURRENT_BINARY_DIR}/generated/audio_tables.h")
add_custom_command(OUTPUT "${WAVE_SAMPLES}" "${AUDIO_TABLES}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/wave_samples.txt"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-audio.py" "${WAVE_SAMPLES}"
    DEPENDS scripts/generate-audio.py
    VERBATIM)
# Neither handoff candidate is platform-verified yet. Start with the planned
# BIOS prototype; the direct sector backend is available for comparison.
option(STORAGE_BIOS_BACKEND "Use the provisional BIOS card handoff" ON)
if(STORAGE_BIOS_BACKEND)
    set(CARD_SOURCE src/card_bios.c)
else()
    set(CARD_SOURCE src/card_psx.c)
endif()
set(STORAGE_SOURCES src/score_format.c src/storage.c)
psn00bsdk_add_executable(psx-grid GPREL src/main.c ${STORAGE_SOURCES} ${CARD_SOURCE} src/input.c src/score.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
target_include_directories(psx-grid PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(psx-grid PRIVATE -Wall -Wextra)

# Keep the interrupt path within its deadline in Debug as well. Symbols remain
# available; the emulator fixture measures the same optimized audio code in both.
set_source_files_properties(src/sequencer.c src/audio.c src/audio_psx.c src/pad.c PROPERTIES COMPILE_OPTIONS -O2)

# PSn00bSDK GPU packet macros intentionally alias the DMA tag types.
set_source_files_properties(src/render.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)

option(AUDIO_FIXTURE "Build the automated audio and input development fixtures" OFF)
if(AUDIO_FIXTURE)
    psn00bsdk_add_executable(audio-fixture GPREL tests/audio_fixture.c ${STORAGE_SOURCES} src/score.c src/input.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    target_include_directories(audio-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(audio-fixture PRIVATE -Wall -Wextra)
    psn00bsdk_add_executable(input-fixture GPREL tests/input_fixture.c ${STORAGE_SOURCES} src/score.c src/input.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    target_include_directories(input-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(input-fixture PRIVATE -Wall -Wextra)
endif()

option(STORAGE_FIXTURE "Build the isolated card/audio development fixture" OFF)
if(STORAGE_FIXTURE)
    psn00bsdk_add_executable(storage-fixture GPREL tests/storage_fixture.c ${STORAGE_SOURCES} ${CARD_SOURCE} src/score.c src/input.c src/editor.c src/render.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    target_include_directories(storage-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(storage-fixture PRIVATE -Wall -Wextra)
endif()

option(REPLACEMENT_FIXTURE "Build the isolated audio replacement fixture" OFF)
if(REPLACEMENT_FIXTURE)
    psn00bsdk_add_executable(replacement-fixture GPREL tests/replacement_fixture.c src/score_format.c src/score.c src/input.c src/sequencer.c src/audio.c src/audio_psx.c src/pad.c "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    target_include_directories(replacement-fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(replacement-fixture PRIVATE -Wall -Wextra -fstack-usage)
endif()
