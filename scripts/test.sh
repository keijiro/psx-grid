#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
python3 scripts/generate-assets.py build/generated/ui_atlas.h
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/score_test.c src/score.c src/input.c src/editor.c -o build/tests/score_test
build/tests/score_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Itests/gpu_stub -Isrc -Ibuild/generated tests/render_test.c src/render.c src/editor.c src/score.c \
  -o build/tests/render_test
build/tests/render_test
