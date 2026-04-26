# Contributing to Amanay

Amanay is part of the Light Desktop Stack (LDS). Contributions are welcome,
but this project has a clear engineering standard and a deliberately small
scope. Read this document before opening a pull request.

## Scope first

Amanay is not a general-purpose terminal. It is a GTK4 terminal for LDS with
a small, predictable surface. Before working on anything, ask whether it fits
that model.

Contributions that are unlikely to be accepted:

- Feature additions that expand the surface beyond the current model
- Alternative UI paradigms (tabs-as-separate-windows, tiling trees, etc.)
- Dependency additions without a strong architectural justification
- Refactors that trade clarity for brevity

If you are unsure whether something fits, open an issue and describe it before
writing any code.

## Engineering standard

This project follows the LDS C Engineering Standard defined in
`docs/LDS_C_Engineering_Standard.md`. That document is the source of truth for
all style, naming, and architecture decisions. The summary below covers the
most common points, but the full document takes precedence.

### Language and formatting

- C11 only
- K&R brace style: `if (cond) {`
- Indentation: TAB (visual width 4)
- Line limit: 100 columns
- Pointer style: `char *ptr`, not `char* ptr`
- No one-liner `if` or loop bodies — always use braces
- Prefer guard clauses at the top of functions
- Use `goto out;` / `goto fail;` for multi-resource cleanup
- No `misc.c` or `common.h` catch-all files

The `.clang-format` and `.editorconfig` files are authoritative for formatting.
Run `clang-format` before committing.

### Naming

Symbols follow the pattern `<prefix>_<module>_<action>`:

```c
lds_terminal_create()
lds_term_spawn()
```

Types follow `<prefix>_<Thing>`:

```c
LdsTerminalState
LdsTerminalTerm
```

Internal-only symbols must be `static` unless there is an explicit reason not
to be.

### File headers

Every `.c` and `.h` file must begin with an SPDX header:

```c
/**
 * SPDX-FileCopyrightText: YYYY-YYYY Your Name <you@example.com>
 * SPDX-License-Identifier: MIT
 *
 * Short description of this component.
 */
```

New files without this header will not be accepted.

### Public API documentation

All public headers use GTK-Doc format. Exported functions must have a
documentation block:

```c
/**
 * lds_terminal_create:
 * @state: (not nullable): Application state.
 * @args: (nullable): Command-line arguments.
 *
 * Creates a new terminal window and registers it with @state.
 *
 * Returns: (transfer none): The new terminal, or %NULL on failure.
 */
```

Do not use Doxygen. Do not use `@brief` or `\param`. GTK-Doc only.

### Internal comments

Internal comments must be rare, short, and explain *why* — not *what*. The
code explains what. Comments explain invariants, constraints, and non-obvious
decisions.

Forbidden comment patterns (checked automatically by `tools/check_comment_policy.sh`):

- `/* Forward declarations */`
- `/* Private helpers */`
- `/* Public API - ... */`
- Line-by-line restatements of the code below

### Module boundaries

- Public headers (`include/`) must never include internal headers
  (`include/internal/`)
- Internal headers may include public headers
- Keep responsibility in the correct module — do not move logic for convenience

## Identity model

This project uses a dual identity model:

- `lds-terminal` — technical identity (application ID, executable, schema,
  resource prefix, icon name, `WM_CLASS`)
- `Amanay` — visual identity (UI text visible to the user)

Do not mix them. `Amanay` never appears in code. `lds-terminal` never appears
in UI strings. The `tools/identity_guard.sh` script checks this automatically
and runs on every push via GitHub Actions.

## Building

```sh
meson setup build
meson compile -C build
```

To enable strict mode (warnings as errors, which is the default):

```sh
meson setup build -Dstrict_build=true
```

Other options:

```sh
meson setup build -Ddefault_renderer=ngl     # renderer: auto, cairo, ngl, vulkan
meson setup build -Dtests=false              # skip test targets
```

## Running tests

```sh
meson test -C build --print-errorlogs
```

Tests cover lifecycle, concurrency, settings, search, link detection, and
several regression scenarios. All tests must pass before a contribution is
ready.

## Checklist before submitting

Run these three steps in order. All must pass cleanly:

```sh
tools/check_comment_policy.sh
meson compile -C build
meson test -C build --print-errorlogs
```

Then run the identity guard:

```sh
tools/identity_guard.sh
```

A pull request that fails any of these checks will not be reviewed until fixed.

## Pull request guidelines

- Keep pull requests focused. One logical change per PR.
- Describe *what* changed and *why* in the PR description.
- Do not mix formatting changes with behavioral changes.
- Reference any related issue in the description.
- If the change touches public API or module architecture, update the relevant
  documentation in `docs/`.

## Reporting issues

Open a GitHub issue and include:

- Your OS and desktop environment
- GTK4, Libadwaita, and VTE versions
- A minimal reproduction case if applicable
- Relevant output from running `lds-terminal` with `LDS_TERMINAL_DIAG=1`
  set in the environment

## License

By contributing to this project, you agree that your contributions will be
licensed under the MIT License, the same license that covers the project. See
`LICENSE` for the full text.
