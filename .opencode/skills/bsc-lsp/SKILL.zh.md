---
name: bsc-lsp
description: "BSC 代码智能（通过 LSP）。当你需要检查所有权流、活动范围、借用或调用图时——例如，在引入借用之前、追踪 _Owned 变量的移动位置、或诊断借用检查器错误时，使用此技能。解释了 BSC 感知的 clangd 返回什么、何时可靠、以及何时回退到 Grep。"
---

# LSP 工具——BSC 代码智能

LSP 工具（`hover`、`findReferences`、`goToDefinition`、`goToImplementation`、`documentSymbol`、`workspaceSymbol`、`prepareCallHierarchy`、`incomingCalls`、`outgoingCalls`）在 clangd 运行并配置了 `.cbs`/`.hbs` 时具备 BSC 感知能力。

## 重要：首先检查 LSP 可用性

在依赖 LSP 之前，先进行一次探测调用（例如在已知符号上使用 `hover`）。如果返回 `No LSP server available for file type: .cbs`，则说明此环境中未注册 BSC 感知的 LSP。回退到手动分析（Read + Grep）并告知用户。

## 在推理所有权时主动使用 LSP

当你即将对涉及 `_Owned` 或 `_Borrow` 指针的 BSC 代码进行非平凡更改时，首先使用 LSP，而不是根据局部上下文猜测。具体来说：

1. **在添加/删除接受 `_Owned` 参数的函数调用之前**：
   对变量使用 `hover` 查看它是否已被移动。
2. **在引入新借用之前（`&_Const x` 或 `&_Mut x`）**：
   对 `x` 使用 `hover` 查看现有借用者——重叠的可变借用 = 编译错误。
3. **当借用检查器错误提到某个变量时**：
   对该变量的声明使用 `hover`。所有权流会告诉你它在哪里被移动/借用/释放。

## 对 `_Owned` / `_Borrow` 指针变量使用 hover

返回变量在其函数内的完整所有权生命周期：

- **所有权流**——带行号和代码片段的时间顺序事件：
  `Declared`（声明）、`Moved in from x`（从 x 移入）、`Moved out to y`（移出到 y）、`Moved into foo()`（移入 foo()）、`Copied in`（复制入）、
  `Mut borrow of x`（x 的可变借用）、`Immut borrow of x`（x 的不可变借用）、`Mut reborrow of x`（x 的可变重新借用）、`Init null`（初始化为空）、`Freed`（释放）、`Dropped`（丢弃）
- **活动范围**——从第一个事件行到最后一个事件行

示例输出：
```
Ownership Flow:
line 10: Declared
  int *_Owned p = safe_malloc(42);
line 18: Moved into consume()
  consume(p);
Live range: lines 10-18
```

## 经验法则

- 使用 **LSP** 处理关于特定函数中特定变量的所有权/类型问题。
- 使用 **Grep** 进行跨文件发现和调用位置枚举。
- 它们是互补的，不是替代品。
