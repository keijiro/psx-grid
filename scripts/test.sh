#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
python3 scripts/generate-assets.py build/generated/ui_atlas.h
python3 scripts/generate-audio.py build/generated/wave_samples.h
RUST_HOST_TARGET=$("$HOME/.cargo/bin/rustc" +nightly-2026-09-26 -vV | sed -n 's/^host: //p')
CARGO_TARGET_DIR=build/rust-host-target "$HOME/.cargo/bin/cargo" +nightly-2026-09-26 build \
  --manifest-path rust/Cargo.toml --target "$RUST_HOST_TARGET" --locked
RUST_LIBRARY="build/rust-host-target/$RUST_HOST_TARGET/debug/libpsx_grid_core.a"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/score_format_test.c src/score_format.c "$RUST_LIBRARY" -o build/tests/score_format_test
build/tests/score_format_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/storage_test.c src/score_format.c "$RUST_LIBRARY" -o build/tests/storage_test
build/tests/storage_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/storage_editor_test.c src/score_format.c src/input.c src/editor.c "$RUST_LIBRARY" -o build/tests/storage_editor_test
build/tests/storage_editor_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/replacement_test.c src/score_format.c src/sequencer.c "$RUST_LIBRARY" -o build/tests/replacement_test
build/tests/replacement_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/score_test.c src/score_format.c src/input.c src/editor.c "$RUST_LIBRARY" -o build/tests/score_test
build/tests/score_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Itests/gpu_stub -Isrc -Ibuild/generated tests/render_test.c src/render.c src/editor.c src/score_format.c "$RUST_LIBRARY" \
  -o build/tests/render_test
build/tests/render_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Ibuild/generated tests/audio_test.c src/score_format.c src/input.c src/editor.c src/sequencer.c src/audio.c "$RUST_LIBRARY" \
  -o build/tests/audio_test
build/tests/audio_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Ibuild/generated tests/synthesis_test.c src/score_format.c src/sequencer.c src/audio.c "$RUST_LIBRARY" \
  -lm -o build/tests/synthesis_test
build/tests/synthesis_test
python3 tests/audio_assets_test.py
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/input_test.c src/score_format.c src/input.c src/editor.c "$RUST_LIBRARY" -o build/tests/input_test
build/tests/input_test
