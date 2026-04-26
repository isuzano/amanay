# LDS C Engineering Standard

Version: 1.1
Scope: All large-scale C projects (LDS and future systems)

------------------------------------------------------------------------

# 1. Core Principle

Maintenance consistency is more important than individual preference.

Architecture and long-term clarity take priority over stylistic debates.

------------------------------------------------------------------------

# 2. Source of Truth Order

If any conflict exists, apply this order:

1. `.clang-format` / `.editorconfig`
2. This document
3. Other collaboration documents

------------------------------------------------------------------------

# 3. Language and Formatting Standard

- Language: C11
- Braces: K&R style (`if (...) {`)
- Indentation: TAB (visual width 4)
- Line limit: 100 columns
- Pointer style: `char *ptr`
- No one-line `if` or loop bodies
- Prefer guard clauses at function start
- Prefer `static inline` over macros when possible
- Use `goto out;` / `goto fail;` for multi-resource cleanup
- No `misc.c` or `common.h` dumping grounds

------------------------------------------------------------------------

# 4. Naming Convention

Symbols:

    <prefix>_<module>_<action>

Example:

    lds_term_spawn()
    lds_buf_push()

Types:

    <prefix>_<Thing>

Internal-only symbols must be declared `static` unless explicitly
required.

------------------------------------------------------------------------

# 5. Project Structure

    project/
     ├── src/                  # implementation (*.c)
     ├── include/              # public headers (*.h)
     ├── include/internal/     # private/internal headers (*.h)
     ├── docs/
     ├── tools/
     ├── tests/
     ├── .clang-format
     ├── .editorconfig
     ├── meson.build
     └── README.md

Rules:

- Public headers must never include internal headers.
- Internal headers may include public headers.
- Responsibility must remain in the correct module.

------------------------------------------------------------------------

# 6. File Header Standard (SPDX Mandatory)

Every .c and .h file must begin with:

``` c
/**
 * SPDX-FileCopyrightText: YYYY-YYYY Author <email>
 * SPDX-License-Identifier: MIT
 *
 * Short component description
 */
```

------------------------------------------------------------------------

# 7. Public API Documentation

All public headers use **GTK-Doc** format. This is compatible with
`gi-docgen`, `gtk-doc`, and readable as plain text.

Exported public functions must use documentation blocks:

``` c
/**
 * lds_terminal_new:
 * @arg: (not nullable): Description.
 *
 * Description of observable behavior.
 *
 * Returns: (transfer full): New instance, or %NULL on error.
 */
```

Rules:

- Document parameters
- Document return value
- Document observable side effects
- Use `%TRUE` / `%FALSE` for boolean values, not `true`/`false`
- Use `#TypeName` for cross-references to types
- Use `(nullable)` / `(not nullable)` on all pointer parameters and
    return values
- Use `(transfer none)` / `(transfer full)` on all returned pointers
- When a returned pointer is into a singleton or internal buffer,
    document its lifetime explicitly — `(transfer none)` alone is not
    sufficient

Group related declarations with plain section comments:

``` c
/* --- Font --- */
```

Do not use Doxygen (`@file`, `@brief`, `\param`) or any other format.
GTK-Doc is the only documentation format used in LDS headers.

------------------------------------------------------------------------

# 8. Internal Comment Policy

Internal comments must:

- Be rare
- Be short
- Explain WHY, not WHAT
- Explain invariants and limits
- Avoid decorative sections

Forbidden:

- Line-by-line restatement of code
- Decorative comment blocks
- Obsolete comments

Preferred internal style:

    /* technical explanation */

------------------------------------------------------------------------

# 9. Architecture Rules

When changing behavior:

- Keep responsibility in the correct module
- Avoid cross-module coupling for convenience
- Do not relocate logic improperly

Public API is a contract and must remain clean and minimal.

------------------------------------------------------------------------

# 10. Minimum Integration Gate

Before integration:

    tools/check_comment_policy.sh
    meson compile -C build
    meson test -C build --print-errorlogs

------------------------------------------------------------------------

# 11. Engineering Philosophy

1. Architecture first
2. Public contracts are sacred
3. Comments preserve intent
4. Tooling is part of the system
5. Predictability over cleverness
6. Portability through standard C11
7. Review for boundaries and long-term clarity

------------------------------------------------------------------------

# End of Document
