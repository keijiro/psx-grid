---
name: c-coding-style
description: C coding style. Use when writing or editing C (.c, .h) files.
---

# C Coding Style

The style is defined by example. Before writing C code, read both files and
match them in layout, naming, braces, comments, and idiom:

- [examples/crc32.h](examples/crc32.h) — public header
- [examples/crc32.c](examples/crc32.c) — implementation

Comments are part of the style. A file is not in style without them:

- Every file opens with a `name - summary` block describing the module;
  a `.c` file adds implementation notes for file-wide decisions.
- Every public function in a header has a block comment stating its
  behavior and parameter contract; public constants and types get at least
  a line.
- Non-trivial static functions, tables, and state have a block comment.
- Inside bodies, comment why where the code cannot show it; never restate it.
- Keep existing comments when restyling code.

Where the examples don't cover something, follow the surrounding code, then
the spirit of the examples.
