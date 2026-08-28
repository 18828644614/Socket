# Errors

## [ERR-20260828-001] apply_patch_target_path

**Logged**: 2026-08-28T15:40:00+08:00
**Priority**: low
**Status**: resolved
**Area**: docs

### 摘要（Summary）
第一次编辑目标章节时，补丁使用了错误的相对目录，导致 `apply_patch` 找不到文件。

### 原始错误（Error）
```text
apply_patch verification failed: Failed to read E:\Linux\socket\发送与接收数据.md: 系统找不到指定的文件。 (os error 2)
```

### 上下文（Context）
- 实际目标文件位于 `E:\Linux\socket\02-TCP Socket\发送与接收数据.md`。
- 第一次补丁把文件路径误写成了仓库根目录下的 `发送与接收数据.md`。
- 补丁验证失败后没有产生目标文件修改。

### 建议修复（Suggested Fix）
编辑前从 `rg --files` 获取并复制完整目标路径；对包含空格或非 ASCII 字符的路径，使用明确的完整路径。

### 元数据（Metadata）
- Reproducible: yes
- Related Files: 02-TCP Socket/发送与接收数据.md
- See Also: none

### 解决情况（Resolution）
- **Resolved**: 2026-08-28T15:40:00+08:00
- **Commit/PR**: none
- **Notes**: 已改用完整路径 `E:\Linux\socket\02-TCP Socket\发送与接收数据.md` 重新执行编辑。

---

## [ERR-20260828-002] wsl_compile_access_denied

**Logged**: 2026-08-28T15:50:00+08:00
**Priority**: medium
**Status**: pending
**Area**: tests

### 摘要（Summary）
尝试使用 WSL 编译 Linux 示例时，WSL 实例启动失败，无法完成 Linux 侧的实际编译验证。

### 原始错误（Error）
```text
Wsl/Service/CreateInstance/E_ACCESSDENIED
```

### 上下文（Context）
- Windows 主机上的 `gcc` 是 MinGW，缺少 Linux 的 `arpa/inet.h` 等 POSIX 头文件。
- `wsl.exe` 已存在，但通过标准输入调用 `wsl gcc ...` 时启动实例失败。
- Windows 两个完整代码块已使用 `x86_64-w64-mingw32-gcc` 完成语法检查。

### 建议修复（Suggested Fix）
在启用并可启动 WSL 的 Linux 环境中，分别使用 GCC 对 Linux 服务端和客户端代码块执行语法检查或编译运行测试。

### 元数据（Metadata）
- Reproducible: yes
- Related Files: 02-TCP Socket/发送与接收数据.md
- See Also: none

---

## [ERR-20260828-003] mingw_link_order_check

**Logged**: 2026-08-28T15:55:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### 摘要（Summary）
Windows 示例的第一次运行测试命令链接失败，原因是测试命令把 Winsock 库放在标准输入源文件之前。

### 原始错误（Error）
```text
undefined reference to `__imp_send'
undefined reference to `__imp_WSAStartup'
collect2.exe: error: ld returned 1 exit status
```

### 上下文（Context）
- 使用 `x86_64-w64-mingw32-gcc` 从 Markdown 代码块的标准输入编译。
- 第一次命令形如 `... -o server.exe -lws2_32 -`，库出现在源文件之前。
- MinGW/GCC 链接时需要把库参数放在目标文件或源文件之后。

### 建议修复（Suggested Fix）
使用 `... -o server.exe - -lws2_32`，或在正常源文件编译命令末尾追加 `-lws2_32`。

### 元数据（Metadata）
- Reproducible: yes
- Related Files: 02-TCP Socket/发送与接收数据.md
- See Also: none

### 解决情况（Resolution）
- **Resolved**: 2026-08-28T15:55:00+08:00
- **Commit/PR**: none
- **Notes**: 已确认文档中的 MinGW 命令把 `-lws2_32` 放在源文件名之后；重试测试将使用正确顺序。

---
