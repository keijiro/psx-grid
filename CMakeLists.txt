cmake_minimum_required(VERSION 3.21)
project(psx-grid LANGUAGES C VERSION 0.1.0 DESCRIPTION "Score Plane wavetable sequencer")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CARGO_EXECUTABLE cargo REQUIRED HINTS "$ENV{HOME}/.cargo/bin")
set(RUST_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml")
set(RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
set(AUDIO_RUST_TABLES "${CMAKE_CURRENT_SOURCE_DIR}/rust/src/audio_tables.rs")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(RUST_PROFILE release)
    set(RUST_RELEASE_FLAG --release)
else()
    set(RUST_PROFILE debug)
    set(RUST_RELEASE_FLAG)
endif()
set(RUST_LIBRARY "${RUST_TARGET_DIR}/mipsel-sony-psx/${RUST_PROFILE}/libpsx_grid_core.a")
file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rust/src/*.rs")
# PSn00bSDK's GPREL objects use a fixed GP; Rust's default abicalls code
# clobbers it and corrupts later C accesses even when the link succeeds.
add_custom_command(OUTPUT "${RUST_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${RUST_TARGET_DIR}"
        "RUSTFLAGS=-Ctarget-feature=+noabicalls"
        "${CARGO_EXECUTABLE}" +nightly-2026-09-26 build --manifest-path "${RUST_MANIFEST}"
        --target mipsel-sony-psx -Z build-std=core --locked ${RUST_RELEASE_FLAG}
    DEPENDS ${RUST_SOURCES} "${AUDIO_RUST_TABLES}" "${RUST_MANIFEST}"
        "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.lock"
        "${CMAKE_CURRENT_SOURCE_DIR}/rust/rust-toolchain.toml"
    VERBATIM)
add_custom_target(grid_rust DEPENDS "${RUST_LIBRARY}")

# Rust emits GNU_STACK after LOAD; the SDK converter assumes LOAD is last.
set(ELF2X "${CMAKE_CURRENT_SOURCE_DIR}/scripts/elf2x-rust.py")
function(link_grid_rust target)
    add_dependencies(${target} grid_rust)
    target_link_libraries(${target} PRIVATE "${RUST_LIBRARY}")
endfunction()
set(UI_ATLAS "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_atlas.h")
set(UI_METRICS "${CMAKE_CURRENT_SOURCE_DIR}/rust/src/ui_metrics.rs")
add_custom_command(OUTPUT "${UI_ATLAS}" "${UI_METRICS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-assets.py" "${UI_ATLAS}"
    DEPENDS scripts/generate-assets.py assets/ui/masks.py assets/ui/jacquard-logo.svg
    VERBATIM)
set(WAVE_SAMPLES "${CMAKE_CURRENT_BINARY_DIR}/generated/wave_samples.h")
set(AUDIO_TABLES "${CMAKE_CURRENT_BINARY_DIR}/generated/audio_tables.h")
add_custom_command(OUTPUT "${WAVE_SAMPLES}" "${AUDIO_TABLES}" "${AUDIO_RUST_TABLES}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/wave_samples.txt"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-audio.py" "${WAVE_SAMPLES}"
    DEPENDS scripts/generate-audio.py
    VERBATIM)
set(CARD_SOURCE src/storage/card_bios.c)
set(ABI_CHECKS_SOURCE src/abi_checks.c)
psn00bsdk_add_executable(psx-grid GPREL src/main.c ${ABI_CHECKS_SOURCE} ${CARD_SOURCE} src/input/input_queue.c src/ui/render.c src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
link_grid_rust(psx-grid)
target_include_directories(psx-grid PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(psx-grid PRIVATE -Wall -Wextra)

# Keep the C entry point and platform interrupt path optimized in Debug.
# The Rust profile controls traversal and synthesis; fixtures check both builds.
set_source_files_properties(src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c PROPERTIES COMPILE_OPTIONS -O2)

# PSn00bSDK GPU packet macros intentionally alias the DMA tag types.
set_source_files_properties(src/ui/render.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)

option(AUDIO_FIXTURE "Build the automated audio and input development fixtures" OFF)
if(AUDIO_FIXTURE)
    psn00bsdk_add_executable(audio-fixture GPREL tests/audio_fixture.c ${ABI_CHECKS_SOURCE} src/input/input_queue.c src/ui/render.c src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    link_grid_rust(audio-fixture)
    target_include_directories(audio-fixture PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(audio-fixture PRIVATE -Wall -Wextra)
    psn00bsdk_add_executable(input-fixture GPREL tests/input_fixture.c ${ABI_CHECKS_SOURCE} src/input/input_queue.c src/ui/render.c src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    link_grid_rust(input-fixture)
    target_include_directories(input-fixture PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(input-fixture PRIVATE -Wall -Wextra)
endif()

option(STORAGE_FIXTURE "Build the isolated card/audio development fixture" OFF)
if(STORAGE_FIXTURE)
    psn00bsdk_add_executable(storage-fixture GPREL tests/storage_fixture.c ${ABI_CHECKS_SOURCE} ${CARD_SOURCE} src/input/input_queue.c src/ui/render.c src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c "${UI_ATLAS}" "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    link_grid_rust(storage-fixture)
    target_include_directories(storage-fixture PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(storage-fixture PRIVATE -Wall -Wextra)
endif()

option(REPLACEMENT_FIXTURE "Build the isolated audio replacement fixture" OFF)
option(BIOS_CARD_FIXTURE "Build the BIOS file fixture without audio or pad" OFF)
if(BIOS_CARD_FIXTURE)
    psn00bsdk_add_executable(card-file-fixture GPREL tests/card_file_fixture.c
        ${ABI_CHECKS_SOURCE} ${CARD_SOURCE})
    link_grid_rust(card-file-fixture)
    target_include_directories(card-file-fixture PRIVATE include src)
    target_compile_options(card-file-fixture PRIVATE -Wall -Wextra)
endif()

if(REPLACEMENT_FIXTURE)
    psn00bsdk_add_executable(replacement-fixture GPREL tests/replacement_fixture.c src/abi_checks.c src/input/input_queue.c src/audio/sequencer.c src/audio/audio_psx.c src/input/pad.c "${WAVE_SAMPLES}" "${AUDIO_TABLES}")
    link_grid_rust(replacement-fixture)
    target_include_directories(replacement-fixture PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(replacement-fixture PRIVATE -Wall -Wextra -fstack-usage)
endif()
