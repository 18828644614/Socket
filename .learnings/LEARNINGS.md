# Learnings

## [LRN-20261014-001] beginner_thread_example_correction

**Logged**: 2026-10-14T00:33:19+08:00
**Priority**: medium
**Status**: pending
**Area**: docs

### 摘要（Summary）
初学者反馈 Windows 并发示例同时出现 `_beginthreadex`、`static unsigned __stdcall`、结构体参数、`goto` 清理出口和 `CRITICAL_SECTION` 时难以理解；示例需要先突出并发模型，再逐步引入平台细节。

### 详情（Details）
原示例使用 C 风格线程 API 和动态分配上下文，适合展示底层写法，但对尚未掌握函数指针、调用约定和资源清理的读者负担过大。用户还询问 `unique_lock`，说明文档应明确区分 C 与 C++：`std::mutex`、`std::lock_guard` 和 `std::unique_lock` 是 C++ 标准库功能，不能直接用于 `.c` 文件。当前章节已将 Windows 主示例改为 C++ `std::thread`，并保留原写法的解释和 Linux POSIX 版本作为平台对照。

### 建议行动（Suggested Action）
编写面向新手的并发代码时，先提供能表达核心模型的最小示例，再单独解释 API 签名、参数打包、线程分离和 socket 清理等底层细节；验证示例时同时检查编译器是否真正支持 `std::thread`。

### 元数据（Metadata）
- Source: user_feedback
- Related Files: 04-并发与I-O模型/线程与进程模型.md
- Tags: beginner, concurrency, windows, cpp, documentation
- See Also: none

---
