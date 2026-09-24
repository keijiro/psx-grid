# C Code Style

Use these guidelines when writing or editing C code. Keep the style of
surrounding code in mind; unrelated formatting changes are unnecessary.

## Layout

- Aim for lines of about 100 characters. There is no hard limit; break long
  expressions where the structure remains clear.
- Indent with four spaces, not tabs. Use blank lines to separate logical steps
  and keep related code together.
- Prefer one statement per line.
- Put a space after commas, after control keywords (`if (condition)`,
  `for (...)`), and around binary operators. Do not put a space between a
  function name and its argument list (`update(value)`).

## Comments

- Comment intent, constraints, and behavior that the code alone does not make
  clear. Keep comments close to the code they explain and update them when it
  changes; avoid narrating obvious operations.
- Add a brief file header or a comment above a function implementation when
  its purpose or contract is not clear from the code and names. Omit comments
  for self-explanatory files and functions.
