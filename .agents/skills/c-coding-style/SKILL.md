---
name: c-coding-style
description: C coding style. Use when writing or editing C (.c, .h) files.
---

# C Coding Style

Where these rules don't cover something, follow the surrounding code, then
the spirit of these rules.

## Layout

- Indent with four spaces. Never use tabs.
- Keep lines within 80 columns.
- Put every brace on its own line (Allman style): function bodies, control
  statements, `typedef struct` / `typedef enum` bodies, and the initializer
  list of an array.
- A control statement whose body is a single short statement may put it on
  the same line without braces: `if (len == 0) return false;`,
  `while (*p == ' ') p++;`. Never put an unbraced body on the next line.
- If either branch of an `if`/`else` needs braces, brace both.
- In a `switch`, align `case` labels with the `switch` keyword and indent
  their bodies one level. Stack labels that share a body. End the `default`
  case with `break` even when nothing follows.
- Separate functions with one blank line. Inside a function, use blank
  lines to separate logical steps: the precondition asserts, the setup, the
  main loop, the result.

## Spacing and declarations

- Put a space after `if`, `for`, `while`, and `switch`, but not between a
  function name and its `(`. Surround binary operators with spaces.
- Attach `*` to the type: `const char* p`, `KvPair* out`. Declare one
  variable per line.

## Naming

- Functions and variables use `snake_case`. Public functions carry the
  module prefix (`kv_parse_line`); static functions do not (`skip_space`).
- Types use `PascalCase`. Public types carry the module prefix (`KvSpan`,
  `KvResult`); file-local types do not (`CharClass`).
- Macros and enum constants use `UPPER_SNAKE_CASE`. Enum constants are
  prefixed after their type (`KV_OK` for `KvResult`, `CHAR_END` for
  `CharClass`); public macros carry the module prefix (`KV_NAME_MAX`).
- Define structs and enums as anonymous `typedef`s without a tag, unless a
  tag is needed for self-reference or forward declaration.
- Short local names (`p`, `c`, `i`, `v`, `s`) are fine in short scopes
  where the type and context make the meaning obvious. Use descriptive
  names for anything that lives longer.

## Files

- A header is guarded by `MODULE_H`, and closes with `#endif // MODULE_H`.
- A header includes exactly what its declarations need.
- In a `.c` file, include its own header first, then a blank line, then the
  system headers in alphabetical order.
- Order a header as: file comment, include guard, includes, usage comment,
  macros, types, inline functions, function declarations.

## Comments

Comments are part of the style. A file is not in style without them. Keep
existing comments when restyling code.

- Write comments as complete English sentences ending with a full stop.
- Use `/* ... */` blocks, with a leading ` * ` on each line and the
  delimiters on their own lines, for file headers and for anything that
  documents a declaration in more than a line. Use `//` for one-line notes
  on declarations, for comments inside function bodies, and for trailing
  comments.

### File comments

- Every file opens with a block whose first line is `name - Summary`,
  e.g. `kv.h - Line-oriented key=value parser`.
- A header's file comment then describes the module from the caller's
  side: what it does, the input it accepts, and properties such as
  statefulness, allocation, and thread safety.
- A `.c` file's file comment adds an `Implementation notes:` section for
  file-wide decisions: the trade-offs taken, assumptions about the input,
  alternatives rejected, and where a likely future change would go.
- A header may add a `Usage` block comment after its includes showing a
  typical call sequence.

### Declarations

- Every public function in a header has a block comment, even when it fits
  on one line. Start with a verb in the third person ("Returns", "Parses")
  and state the behavior. Then describe what happens on failure, including
  what the outputs are left as. End with the parameter contract (e.g.
  "`out` must not be NULL.") in its own paragraph or closing sentence.
- Public constants and types get at least a one-line `//` comment. Say why
  a value was chosen when that is not obvious ("so that a copy fits a
  32-byte buffer with its NUL").
- Document enum members with aligned trailing `//` comments.
- Give non-trivial static functions, tables, and file-scope state a block
  comment explaining what they are for and why they are shaped that way.
  Trivial helpers whose name and body say everything need none.

### Function bodies

- Comment why where the code cannot show it: a non-obvious technique, a
  constraint, or the reason a check is placed where it is. Never restate
  what the code visibly does.
- Put the comment on the line above the code it explains. Use a trailing
  comment only for a short note on a single line, such as one `case`
  label.
