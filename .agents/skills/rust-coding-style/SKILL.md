---
name: rust-coding-style
description: Rust coding style. Use when writing or editing Rust (.rs) files.
---

# Rust Coding Style

Where these rules don't cover something, follow the surrounding code, then
rustfmt and the Rust API Guidelines, then the spirit of these rules.

## Layout

- Format with rustfmt and leave its output alone. Don't fight it with
  manual alignment or `#[rustfmt::skip]`, except for a table whose layout
  carries meaning, such as a matrix or a lookup table.
- Keep lines within 80 columns. A crate carries a `rustfmt.toml` with
  `max_width = 80`; add one when it is missing.
- Braces follow rustfmt: the opening brace stays on the line of its item or
  control expression. Rust always braces control bodies, so a short body may
  stay on one line only where rustfmt keeps it there.
- In a `match`, stack patterns that share a body with `|`. Match an enum
  exhaustively rather than with a `_` arm, so that adding a variant breaks
  every place that must handle it. Use `_` only when the remaining values
  are open-ended, such as integers or a foreign `#[non_exhaustive]` enum.
- Separate items with one blank line. Inside a function, use blank lines to
  separate logical steps: the precondition asserts, the setup, the main
  loop, the result.

## Declarations

- Bind one name per `let`, except when destructuring a tuple or struct
  whose parts belong together.
- Let type inference work for locals. Annotate a type where it is not
  obvious from the right-hand side, or where it pins down a numeric width
  that matters.
- Keep items private by default. Widen to `pub(crate)` or `pub` only for
  something another module actually uses.

## Naming

- Follow the Rust naming conventions: `snake_case` for functions, methods,
  variables, and modules; `UpperCamelCase` for types, traits, and enum
  variants; `SCREAMING_SNAKE_CASE` for constants and statics.
- Don't prefix names with the module name. The module path already
  qualifies them: write `kv::parse_line` and `kv::Span`, not
  `kv::kv_parse_line` or `kv::KvSpan`.
- Treat acronyms as words: `PsxGpu`, `read_vram`, not `PSXGPU`.
- Follow the API Guidelines for method names: no `get_` prefix on getters,
  `new` for the primary constructor, and `as_`/`to_`/`into_` for
  conversions by their cost and ownership.
- Short local names (`p`, `c`, `i`, `v`, `s`) are fine in short scopes
  where the type and context make the meaning obvious. Use descriptive
  names for anything that lives longer.

## Files and modules

- Name a module file after the module (`kv.rs`, and `kv/` for its
  submodules). Don't use `mod.rs`.
- Group `use` declarations in this order, with a blank line between groups:
  `std`/`core`/`alloc`, external crates, then `crate::`/`super::` paths.
  Let rustfmt sort within a group. Don't glob-import except for a prelude
  or `use super::*` in a test module.
- Order a file as: module doc comment, implementation notes, `use`
  declarations, constants, types with their `impl` blocks, functions, then
  the `#[cfg(test)] mod tests` block last.
- Put a type's `impl` block right after the type. Put trait impls after the
  inherent `impl`.

## Errors and safety

- Return `Result` or `Option` for failures a caller can handle, and
  propagate with `?`. Panic only for a broken invariant, which is a bug.
- Prefer `expect` over `unwrap` outside tests, with a message that states
  the invariant that was expected to hold.
- Use `assert!` for preconditions that guard memory or hardware state and
  `debug_assert!` for checks too costly for release builds.
- Keep code free of `cargo clippy` warnings. Silence a lint with a narrow
  `#[allow(...)]` and a comment saying why it doesn't apply.
- Keep `unsafe` blocks as small as the operation that needs them.

## Comments

Comments are part of the style. A file is not in style without them. Keep
existing comments when restyling code.

- Write comments as complete English sentences ending with a full stop.
- Use `//!` for module docs, `///` for item docs, and `//` for everything
  else. Don't use `/* ... */` or `/** ... */` blocks.
- Doc comments are Markdown. Put identifiers in backticks and link to other
  items with intra-doc links (``[`Span`]``) where it helps the reader.

### Module comments

- Every file opens with a `//!` block. Its first line is a one-sentence
  summary, e.g. `//! Line-oriented key=value parser.` Rustdoc already shows
  the module name, so don't repeat it.
- The rest of the block describes the module from the caller's side: what
  it does, the input it accepts, and properties such as statefulness,
  allocation, and thread safety.
- A module may add an `# Examples` section showing a typical call sequence.
  Write it as a doctest that compiles and runs.
- File-wide implementation decisions go in a `// Implementation notes:`
  block of plain `//` comments after the `//!` block, so they stay out of
  the rendered docs: the trade-offs taken, assumptions about the input,
  alternatives rejected, and where a likely future change would go.

### Items

- Every public function and method has a `///` comment, even when it fits
  on one line. Start with a verb in the third person ("Returns", "Parses")
  and state the behavior.
- Then add the standard sections that apply, in this order:
  - `# Errors` for a function returning `Result`: when each error occurs,
    and what any `&mut` outputs are left as.
  - `# Panics` for every condition that panics.
  - `# Safety` for an `unsafe fn` or `unsafe trait`: every condition the
    caller must uphold. This section is mandatory.
- State only the contract the types don't already enforce. A reference
  can't be null, so don't say so; do say that a slice must be non-empty or
  an index in range.
- Public constants, statics, types, and traits get at least a one-line
  `///` comment. Say why a value was chosen when that is not obvious ("so
  that a copy fits a 32-byte buffer").
- Document each enum variant and struct field with its own `///` line
  above it. Keep them to a line where possible.
- Give non-trivial private functions, tables, and statics a `///` comment
  explaining what they are for and why they are shaped that way. Trivial
  helpers whose name and body say everything need none.

### Function bodies

- Comment why where the code cannot show it: a non-obvious technique, a
  constraint, or the reason a check is placed where it is. Never restate
  what the code visibly does.
- Precede every `unsafe` block with a `// SAFETY:` comment explaining why
  each requirement of the operation holds at this point.
- Put the comment on the line above the code it explains. Use a trailing
  comment only for a short note on a single line, such as one `match` arm.
