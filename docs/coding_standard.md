# Tandem Controls Coding Standard

This document defines the C++17 coding and formatting conventions for Tandem Controls (and lightweight formatting conventions for supporting web/Python tooling). Embedded architecture and real-time safety rules live in [AGENTS.md](../AGENTS.md).

---

## 1. Base Standard & Automated Formatting

Tandem Controls C++ formatting derives from the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) via `BasedOnStyle: Google` in [.clang-format](../.clang-format) (reference: [Clang-Format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)).

Only these explicit deviations from `BasedOnStyle: Google` are configured in [.clang-format](../.clang-format):

| Option | Value | Rationale |
| :--- | :--- | :--- |
| `Standard` | `c++17` | Match `lib/tandem_core/` and firmware C++17 target standard |
| `ColumnLimit` | `150` | Avoid cramped line wrapping on modern displays and long assertions |
| `AllowShortFunctionsOnASingleLine` | `Inline` | Allow short inline/empty functions in headers while keeping `.cpp` definitions multi-line |
| `AllowShortIfStatementsOnASingleLine` | `Never` | Keep control-flow branches readable and consistently braced |
| `AllowShortLoopsOnASingleLine` | `false` | Keep loop bodies on their own lines |
| `ReflowComments` | `false` | Preserve manual comment line breaks, ASCII diagrams, and bullet lists |

Shared editor whitespace/encoding defaults (UTF-8, `LF` line endings, final newline, indentation) are defined in [.editorconfig](../.editorconfig).

---

## 2. Header & Include Hygiene

- **File extensions & names:** Use `.h` for all headers and `.cpp` for all C++ sources, named in `snake_case`.
- **Include guards:** Start every header with `#pragma once` on line 1 (never `#ifndef` header guards).
- **Self-contained headers:** Every `.cpp` file must include its own matching `.h` file first to verify the header compiles standalone.
- **Include ordering:** Follow Google's `IncludeBlocks: Regroup` grouping:
  1. Matching header (for `.cpp` files, e.g., `#include "tandem/axis_shaping.h"`)
  2. C / SDK / RTOS / framework / test angle-bracket `.h` headers (e.g., `<Arduino.h>`, `<freertos/FreeRTOS.h>`, `<unity.h>`)
  3. C++ standard library headers without `.h` (e.g., `<cstdint>`, `<cstring>`, `<cmath>`)
  4. Project headers in quotes (`"boards/..."`, `"tandem/..."`, `"tandem_hal/..."`, local `"..."`)
  - `SortIncludes: CaseSensitive` naturally preserves `<freertos/FreeRTOS.h>` ahead of lowercase FreeRTOS headers (`<freertos/portmacro.h>`, `<freertos/queue.h>`).
  - Prefer C++ `<c*>` headers (`<cstdint>`, `<cstddef>`, `<cstring>`, `<cstdio>`, `<cmath>`) and `std::`-qualified symbols over legacy C `.h` headers.
- **`// clang-format off` exceptions:** Permitted only for hand-aligned tabular binary layouts (such as HID report descriptors in [hid_report.h](../lib/tandem_core/include/tandem/hid_report.h)) and order-sensitive vendor SDK include sequences. Always restore formatting immediately with `// clang-format on`.

---

## 3. Naming Conventions

| Construct | Convention | Examples |
| :--- | :--- | :--- |
| Namespaces | `snake_case` | `tandem`, `tandem::config`, `tandem::hal`, `tandem::board`, `tandem::stick` |
| Translation-unit internals | Anonymous `namespace` | `namespace { ... }  // namespace` (prefer over `static` free functions) |
| Types (`struct`, `class`, `enum class`, aliases) | `PascalCase` | `StickState`, `HidConfigTxQueue`, `CurveType` |
| Scoped `enum class` values | `PascalCase` | `StickId::Left`, `CurveType::Expo` |
| Unscoped bitmask `enum` values | `kCamelCase` | `kStateFlagConnected`, `kStateFlagFailsafe` |
| Functions, methods, locals, parameters, public `struct` fields | `snake_case` | `shape_axis`, `deadzone`, `out_len` |
| Private / protected `class` data members | `snake_case_` | `hid_`, `active_`, `config_` |
| Constants (`constexpr`, `const`) | `kCamelCase` | `kMaxAxes`, `kHidReportIdJoystick` (prefer `inline constexpr` in headers) |
| File-scope mutable globals (firmware apps only; minimize) | `g_snake_case` | `g_scanner`, `g_last_state` |
| Preprocessor macros & build flags | `TANDEM_SCREAMING_SNAKE_CASE` | `TANDEM_CAP_NATIVE_USB`, `TANDEM_STICK_ID` |

---

## 4. C++17 & Embedded Idioms

- **West-const:** Write `const T*`, `const T&`, and `const uint32_t` consistently (external C SDK callback overrides are the only exception when matching vendor signatures).
- **`noexcept`:** Mark pure `tandem_core` functions, `tandem_hal` adapters, and transport hot-path methods `noexcept`.
- **Fixed-width integers:** Use `uint8_t`, `int16_t`, `uint32_t`, `size_t` for protocol, HID, ADC, and timing types; add `U` suffixes on unsigned bitmasks, shifts, and integer constants.
- **Casts & initialization:** Use `static_cast` or `reinterpret_cast` (no C-style casts); brace-initialize structs and buffers (`StickState state{};`); use `nullptr` rather than `NULL` or `0` for pointers.
- **Class hygiene:** Mark single-argument constructors `explicit` and overridden virtual methods `override`.
- **Control flow braces:** Always brace `if`, `else`, `for`, and `while` bodies (including single-statement blocks).
- **Comments:** Use `///` doc comments above public API declarations in headers, and `//` for implementation notes and inline comments.

---

## 5. Non-C++ Files (EditorConfig Conventions)

CI linting is scoped to C/C++; supporting web and Python files follow [.editorconfig](../.editorconfig) and these lightweight conventions:

- **Tandem Configurator (`tools/configurator/` — JS, HTML, CSS):**
  - 2-space indentation, `"use strict"`, double quotes, semicolons, trailing commas in multiline literals.
  - `camelCase` functions/variables, `PascalCase` classes, `SCREAMING_SNAKE_CASE` top-level constants; `const` by default (`let` only when reassigned, never `var`).
  - Lowercase HTML tags/attributes with double-quoted values; CSS `--kebab-case` custom properties in `:root`.
- **Python (`tools/*.py`):**
  - PEP 8, 4-space indentation, 150-column max line length, type annotations on helper functions, standard library imports sorted ahead of third-party/local imports.

---

## 6. Tooling & Local Setup

CI pins a single `clang-format` release via PyPI in [tools/check_format.py](../tools/check_format.py) (`PINNED_CLANG_FORMAT_VERSION`, enforced by the `C++ Format Check` job in [.github/workflows/ci.yml](../.github/workflows/ci.yml)). Install the exact version locally so editor and CI behavior match:

```bash
python -m pip install clang-format==23.1.5
```

Common commands from the repository root:

```bash
# Check all C/C++ files (warns on local version mismatch)
python tools/check_format.py

# Check strictly requiring the CI-pinned clang-format version
python tools/check_format.py --strict-version

# Auto-format all C/C++ files in-place
python tools/check_format.py --fix
```

**Optional VS Code configuration:** Set `"C_Cpp.clang_format_path"` in your VS Code user settings to the pip-installed `clang-format` binary (for example `C:\Users\<user>\AppData\Local\Programs\Python\Python311\Scripts\clang-format.exe` on Windows) and enable `"editor.formatOnSave": true` so IDE formatting matches CI rather than the bundled extension binary.
