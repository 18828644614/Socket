# Task Plan: 补充发送与接收数据章节

## Goal
以初学者能理解和运行的方式，补充“发送与接收数据”章节，并提供 Linux 与 Windows 示例。

## Phases
- [x] Phase 1: 计划和仓库调查
- [x] Phase 2: 阅读现有章节并记录内容要求
- [x] Phase 3: 编写 Linux/Windows 教程和示例
- [x] Phase 4: 复核文档、代码和工作区变更

## Key Questions
1. 现有项目中“发送与接收数据”章节位于哪个文件，前后章节如何衔接？
2. Linux 和 Windows 示例应采用哪些 API、编译命令和运行步骤？
3. 初学者最容易误解的 TCP 接收、消息边界、返回值和关闭连接问题是否已解释？

## Decisions Made
- 延续仓库已有文档结构和代码风格，尽量只修改目标章节。
- TCP 示例同时覆盖服务器和客户端，Linux 使用 POSIX socket，Windows 使用 Winsock2。

## Errors Encountered
- 第一次 `apply_patch` 少写了 `02-TCP Socket` 目录，导致找不到目标文件；已记录到 `.learnings/ERRORS.md`，修正路径后重试。

## Status
**Completed** - 章节、平台示例和验证已完成；Linux 实际编译受 WSL `E_ACCESSDENIED` 环境问题限制，Windows 已完成编译和运行回显验证。
