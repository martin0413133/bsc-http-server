---
name: bsc-lsp
description: "BSC code intelligence via LSP. Load this Skill when you need to inspect ownership flow, live ranges, borrows, or call graphs in BSC code — e.g. before introducing a borrow, tracing where an _Owned variable was moved, or diagnosing a borrow-checker error. Explains what BSC-aware clangd returns, when it's reliable, and when to fall back to Grep."
---

# LSP Tool — BSC Code Intelligence

The LSP tool (`hover`, `findReferences`, `goToDefinition`, `goToImplementation`, `documentSymbol`, `workspaceSymbol`, `prepareCallHierarchy`, `incomingCalls`, `outgoingCalls`) is BSC-aware when clangd is running and configured for `.cbs`/`.hbs`.

## IMPORTANT: Check LSP availability first

Before relying on LSP, probe with one call (e.g. `hover` on a known symbol). If it returns `No LSP server available for file type: .cbs`, the BSC-aware LSP is NOT registered in this environment. Fall back to manual analysis (Read + Grep) and tell the user.

## Use LSP proactively when reasoning about ownership

When you're about to make a non-trivial change to BSC code involving `_Owned` or `_Borrow` pointers, USE LSP FIRST instead of guessing from local context. Specifically:

1. **Before adding/removing a function call that takes an `_Owned` argument**:
   `hover` on the variable to see if it's been moved already.
2. **Before introducing a new borrow** (`&_Const x` or `&_Mut x`):
   `hover` on `x` to see existing borrowers — overlapping mutable borrows = compile error.
3. **When a borrow-checker error mentions a variable**:
   `hover` on that variable's declaration. The Ownership Flow tells you exactly where it was moved/borrowed/freed.

## hover on `_Owned` / `_Borrow` pointer variables

Returns the variable's full ownership lifecycle within its function:

- **Ownership Flow** — chronological events with line numbers and code snippets:
  `Declared`, `Moved in from x`, `Moved out to y`, `Moved into foo()`, `Copied in`,
  `Mut borrow of x`, `Immut borrow of x`, `Mut reborrow of x`, `Init null`, `Freed`, `Dropped`
- **Live range** — first to last event line

Example output:
```
Ownership Flow:
line 10: Declared
  int *_Owned p = safe_malloc(42);
line 18: Moved into consume()
  consume(p);
Live range: lines 10-18
```

## Known limitations — when to fall back to Grep

clangd does not fully understand BSC's member-function call syntax (`obj->method(...)` resolved against `T::method`). This affects:

- **findReferences** on a member function often returns only the declaration site, missing call sites. Always cross-check with `Grep` before renaming/deleting a symbol.
- **incomingCalls** may report "no incoming calls" for a function clearly called via `obj->method()`. Use `Grep` for call-site discovery.

## Net rule of thumb

- Use **LSP** for ownership/type questions about a specific variable in a specific function.
- Use **Grep** for cross-file discovery and call-site enumeration.
- They are complements, not substitutes.
