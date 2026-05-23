---
name: bsc-coroutine
description: "BiSheng C coroutines. When you need to understand _Async functions, _Await expressions, Future<T> trait, PollResult<T>, poll/free, Scheduler for concurrent task execution, or async/await restrictions, use this Skill."
---

# BiSheng C Coroutines Skill

## 1. Overview

Stackless coroutines. Requires `#include "future.hbs"`.

## 2. PollResult<T> and Future<T>

`poll()` returns `struct PollResult<T>`, not void. This is critical for checking completion.

```c
struct PollResult<T> {
    _Bool isPending;  // true if not yet complete
    T res;            // result value (valid when isPending == false)
};

// PollResult methods:
_Bool struct PollResult<T>::is_pending(struct PollResult<T>* this);
struct PollResult<T> struct PollResult<T>::pending();
_Bool struct PollResult<T>::is_completed(struct PollResult<T>* this, T* out);
struct PollResult<T> struct PollResult<T>::completed(T result);

_Trait Future<T> {
    struct PollResult<T> poll(This* this);
    void free(This* this);
};
```

## 3. Basic Syntax

```c
#include "future.hbs"

_Async int fetchData(int id) {
    return id * 2;
}

_Async int processAll() {
    int a = _Await fetchData(1);
    int b = _Await fetchData(2);
    return a + b;
}

int main() {
    _Trait Future<int>* task = processAll();  // returns Future, does not execute
    struct PollResult<int> pr = task->poll(); // drives the coroutine, returns PollResult
    // pr.isPending == false when done; pr.res holds the result
    task->free();                             // free coroutine resources
    return 0;
}
```

## 4. Rules

- `_Async` functions return `_Trait Future<ReturnType>*` when called
- For `void` return: use `_Trait Future<struct Void>*` (compiler auto-creates `struct Void`)
- `_Await` can only appear inside `_Async` functions
- `_Async` supports recursion
- `_Async` can modify member functions: `_Async void int::g(int* this);`
- `constexpr` cannot modify `_Async` functions
- No variable-length arrays in `_Async` functions

### 4.1 Where `_Await` CAN appear

- Assignments: `int x = _Await f();`
- Return statements: `return _Await f();`
- Nested `_Await` calls: `_Await f(_Await g())` — allowed in nested args
- Multi-level nesting: `_Await test1(_Await test2(start))` — OK
- Function args with no other side-effect expressions: `f(3, _Await g())`
- Directly awaiting a Future variable: `_Await futurePtr;`

### 4.2 Where `_Await` CANNOT appear

- if/while/for/do-while **conditions**: `if (_Await f()) { ... }`
- Binary expressions: `_Await f() + 1`
- Function args alongside other function calls (side-effects): `f(t(), _Await g())`
- **Multiple `_Await` in same call argument list**: `f(_Await g(), _Await h())`

The distinction: nested `_Await` as an argument (`_Await f(_Await g())`) is allowed, but multiple `_Await` at the **same argument level** (`f(_Await g(), _Await h())`) is forbidden.

### 4.3 Workarounds

```c
// Wrong: binary expression
int result = _Await compute(1) + _Await compute(2);

// Correct: split into assignments
int a = _Await compute(1);
int b = _Await compute(2);
int result = a + b;

// Wrong: side-effect expression alongside _Await
test(t(), _Await read(2));

// Correct: pre-evaluate the side-effect call
int tmp = t();
test(tmp, _Await read(2));
```

## 5. Scheduler (Concurrent Task Execution)

Requires `#include "scheduler.hbs"`.

The `Scheduler` manages a thread pool and task queue for concurrent async execution. Only one Scheduler per process.

### API

| Method | Signature | Description |
|--------|-----------|-------------|
| `init` | `struct Scheduler::init(unsigned int threadCount)` | Initialize scheduler with N threads |
| `spawn` | `struct Scheduler::spawn(_Trait Future<struct Void>* future)` → `struct Task*` | Queue an async task (void return only) |
| `run` | `struct Scheduler::run()` | Execute all queued tasks |
| `destroy` | `struct Scheduler::destroy()` | Destroy scheduler, free resources |

### Lifecycle
1. `init` — must be called once before `spawn`/`run`; cannot re-initialize
2. `spawn` — queues tasks but does NOT execute them
3. `run` — starts executing queued tasks on the thread pool
4. `destroy` — cleanup; process cannot terminate without this

### Example

```c
#include "scheduler.hbs"

atomic_int g_count = 10;

_Async void doWork(int i) {
    printf("Task %d\n", i);
    atomic_fetch_sub(&g_count, 1);
    if (atomic_load(&g_count) == 0) {
        struct Scheduler::destroy();
    }
}

int main() {
    struct Scheduler::init(4);           // 4 threads
    for (int i = 0; i < 10; i++) {
        struct Scheduler::spawn(doWork(i));  // queue tasks
    }
    struct Scheduler::run();             // execute
    return 0;
}
```

Note: task execution order is non-deterministic — suitable for tasks that don't require strict ordering.

`spawn` also accepts explicitly constructed `_Trait Future<struct Void>*` (not just `_Async` function calls).

## 6. Complete Example

```c
#include "future.hbs"
#include <stdio.h>

_Async int computeValue(int x) {
    return x * 2;
}

// Recursion supported
_Async int factorial(int n) {
    if (n <= 1) return 1;
    int sub = _Await factorial(n - 1);
    return n * sub;
}

// _Await on a Future variable directly
_Async int awaitVariable() {
    _Trait Future<int>* fut = computeValue(5);
    return _Await fut;
}

_Async void logMessage(int id) {
    int val = _Await computeValue(id);
}

int main() {
    _Trait Future<int>* task1 = factorial(5);
    struct PollResult<int> pr1 = task1->poll();
    // pr1.res contains the result when pr1.isPending == false
    task1->free();

    _Trait Future<struct Void>* task2 = logMessage(42);
    task2->poll();
    task2->free();

    return 0;
}
```

> For trait pointers (Future<T> uses them), see `bsc-trait` Skill
> For async errors (BSC-E08xx), see `bsc-errors` Skill
