
# Working Principles

## 1. Think Before Coding

Don't assume. Don't hide confusion. Surface tradeoffs.

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

Minimum code that solves the problem. Nothing speculative.

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

Touch only what you must. Clean up only your own mess.

When editing existing code:

- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.

When your changes create orphans:

- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

Define success criteria. Loop until verified.

Transform tasks into verifiable goals:

- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

# BiSheng C (BSC) — MUST READ before writing any .cbs code

## Pointer Qualifier Grammar (CRITICAL)

`_Owned` and `_Borrow` are **pointer qualifiers**, like `const`. They go AFTER `*`, NEVER before the type.

Grammar: `base_type *qualifier variable_name`

Think of it like `const`: you write `int *const p`, not `const int* p` for a const pointer.
Same rule: `int *_Owned p`, not `_Owned int* p`.

### Correct

```c
char *_Owned msg = (char *_Owned)malloc(14);
int *_Owned p = (int *_Owned)malloc(sizeof(int));
const int *_Borrow r = &_Const x;
int *_Borrow mr = &_Mut y;
free((void *_Owned)p);
```

### Wrong — causes compilation error: "unknown type name '_Owned'"

```c
_Owned char* msg = malloc(14);     // WRONG — _Owned is not a type
_Borrow int* r = &_Const x;       // WRONG — _Borrow is not a type
```

**Self-check before returning BSC code**: if you wrote `_Owned T*` or `_Borrow T*`,
rewrite to `T *_Owned`. The pattern is ALWAYS `T *_Owned`.

## Quick Reference

- Files: `.cbs` (source), `.hbs` (header). Compile: `clang file.cbs -o output`
- `.c`/`.h` files can be compiled as BSC with `-x bsc` (or `-xbsc`, both work): `clang -x bsc file.c -o output`
- `#include "bishengc_safety.hbs"` for safe APIs
- Prefer `safe_malloc<T>(val)` (returns `T *_Owned`, no cast) and `safe_free((void *_Owned)p)`
- Raw↔`_Owned` direct C casts are **forbidden even in `_Unsafe`**. Use the builtins:
  - Raw → `_Owned`: `__take_from_raw(raw_ptr)` (after null-check if nullability-check is on)
  - `_Owned` → raw: `__move_to_raw(owned_ptr)` (transfers ownership) OR `(T *)&_Mut *owned_ptr` (just borrows, keeps ownership)

## BSC Project Compile Command

```
Compiler: /home/zly/bsc/llvm-project/build/bin/clang   (NOT system clang)
Includes: -I/home/zly/bsc/llvm-project/install/include/libcbs
Link:     -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
Flags:    -Wall -Wextra -Wno-nullability-completeness -g
```

Build model: **single translation unit** — `src/main.cbs` `#include`s all other `.cbs`
(business `src/*.cbs`, then adapters `src/platform/*.cbs`). libcbs `String`/`Vec` need
`-lstdcbs` linked (they are NOT header-only). `String` is NOT NUL-terminated.

Commands:
```
make                    # build bin/httpd (runs check-layers first)
make smoke              # toolchain smoke test
make test-<name>        # build+run tests/test_<name>.cbs (e.g. make test-router)
bash tests/valgrind_units.sh    # run all unit tests under valgrind (BSC tests can pass while UAF!)
bash tests/run_integration.sh   # curl-based AC-2..AC-5 + traversal
bash tests/check_no_unsafe.sh   # enforce: business src/*.cbs is _Unsafe-free
# syntax-only one file: <compiler> <flags> <includes> -fsyntax-only <file>
```

**Layering rule (mandatory):** business `src/*.cbs` must be `_Unsafe`-free; all `_Unsafe`
lives in adapters `src/platform/*.cbs` (each a `_Safe` interface with `_Unsafe` blocks
inside). Enforced by `make` via `check-layers`.

## Code Verification

After writing or editing any `.cbs`/`.hbs` file (or `.c`/`.h` compiled with `-x bsc`), run the project's verify command from the section above. Fix all compiler errors before reporting the task complete.

## LSP — Use Proactively

BSC-aware clangd exposes ownership flow and live ranges. Before non-trivial changes to
`_Owned`/`_Borrow` code, `hover` on the variable to see where it was moved, borrowed, or
freed — don't guess from local context. For the full reference (availability probe,
known clangd limitations, fallback-to-Grep rules), load `/bsc-lsp`.

## When to Load Other Skills

- Non-trivial BSC change (new struct, new `_Owned`/`_Borrow` signature, refactor crossing ownership timelines, `_Safe`/`_Unsafe` boundary decision) → load `/bsc-design` BEFORE planning.
- Translating/porting C to BSC → load `/c-to-bsc` BEFORE starting. Changing file extensions alone is NOT a translation.

## When to Delegate to the `bsc-planner` Agent

Use the `bsc-planner` subagent (via the Agent tool) when planning a non-trivial BSC change that requires reading many `.cbs` files to understand ownership flow — refactors, new APIs, cross-module impact analysis, anything that would burn main context. The agent is read-only and returns a plan; the main thread implements. Do NOT delegate for single-file edits, typo fixes, or borrow-checker errors on a known function — handle those directly.

## When to Invoke `bsc-learn` (At Session End)

After a non-trivial BSC coding session — any session where you modified or created `.cbs`/`.hbs` files, hit borrow-checker or ownership errors, translated C to BSC, or debugged destructor/move semantics — invoke `bsc-learn` via the Agent tool.

Pass it:
1. A brief summary of the main issues you encountered (what the checker rejected, what took iteration, what was surprising).
2. The git commit range covering this session (e.g. `HEAD~3..HEAD` or specific SHAs).

The agent reads the diff and the existing skill files, then returns proposed skill improvements as text. Present the proposals to the user for approval before applying any edits. Do NOT apply edits autonomously.

Skip `bsc-learn` for: sessions with only comment edits, single-line typo fixes, or no `.cbs` changes.
