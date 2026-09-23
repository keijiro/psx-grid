#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
python3 scripts/generate-assets.py build/generated/ui_atlas.h
python3 scripts/generate-audio.py build/generated/wave_samples.h
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/score_test.c src/score.c src/input.c src/editor.c -o build/tests/score_test
build/tests/score_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Itests/gpu_stub -Isrc -Ibuild/generated tests/render_test.c src/render.c src/editor.c src/score.c \
  -o build/tests/render_test
build/tests/render_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Ibuild/generated tests/audio_test.c src/score.c src/input.c src/editor.c src/sequencer.c src/audio.c \
  -o build/tests/audio_test
build/tests/audio_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Ibuild/generated tests/synthesis_test.c src/score.c src/sequencer.c src/audio.c \
  -lm -o build/tests/synthesis_test
build/tests/synthesis_test
python3 tests/audio_assets_test.py
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/input_test.c src/score.c src/input.c src/editor.c -o build/tests/input_test
build/tests/input_test
