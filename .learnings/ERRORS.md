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

## [ERR-20260831-001] node_static_check_command

**Logged**: 2026-08-31T15:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### 摘要（Summary）
静态编译检查命令因 Node.js `-e` 脚本多写一个右花括号而失败，未影响文档内容。

### 原始错误（Error）
```text
SyntaxError: Unexpected token '}'
```

### 上下文（Context）
- 通过 Node.js 读取 Markdown 中的 C 代码块，并把 Linux 代码送入 GCC 标准输入进行语法检查。
- 命令末尾的循环闭合符号数量不匹配，导致脚本在执行前解析失败。

### 建议修复（Suggested Fix）
删除多余的 `}` 后重新执行静态检查命令。

### 元数据（Metadata）
- Reproducible: yes
- Related Files: 02-TCP Socket/TCP聊天室项目.md
- See Also: none

### 解决情况（Resolution）
- **Resolved**: 2026-08-31T15:00:00+08:00
- **Commit/PR**: none
- **Notes**: 已修正命令并重新执行检查。

---

## [ERR-20260831-001] code_block_extraction_index

**Logged**: 2026-08-31T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### 摘要（Summary）
提取 Markdown 中 C 代码块做编译检查时，误把代码块索引当成包含伪代码块，导致 Windows 客户端临时文件没有生成。

### 原始错误（Error）
```text
InvalidOperation: Cannot index into a null array.
cc1.exe: fatal error: C:\\Users\\ADMINI~1\\AppData\\Local\\Temp\\socket_boundary_check\\win_client.c: No such file or directory
```

### 上下文（Context）
- 文档中的伪代码使用了三反引号代码围栏，Linux/Windows 完整示例使用了 `~~~c`。
- 正则只匹配 `~~~c`，实际得到 4 个完整 C 代码块，而不是预期的 5 个。
- 该错误只影响临时验证命令，不影响文档内容。

### 建议修复（Suggested Fix）
提取代码块前先统计匹配数量并按实际顺序确认索引；更稳妥的做法是给临时文件使用明确的代码块标记或直接从文档复制目标块。

### 元数据（Metadata）
- Reproducible: yes
- Related Files: 02-TCP Socket/消息边界与拆包.md
- See Also: none

### 解决情况（Resolution）
- **Resolved**: 2026-08-31T00:00:00+08:00
- **Commit/PR**: none
- **Notes**: 已按实际 4 个 `~~~c` 代码块重新提取 Windows 服务端和客户端，并继续编译验证。

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
