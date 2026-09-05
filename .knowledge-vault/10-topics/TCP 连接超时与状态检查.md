---
type: topic
status: active
created: 2026-08-31
updated: 2026-08-31
tags:
  - TCP
  - Winsock
  - 超时
  - 非阻塞
aliases:
  - 非阻塞 connect 超时
---

# TCP 连接超时与状态检查

## 核心结论

Windows 中可以把 Socket 临时设置为非阻塞，让 `connect()` 尽快返回；若返回 `WSAEWOULDBLOCK` 或 `WSAEINPROGRESS`，使用 `select()` 等待可写或异常事件，再用 `getsockopt(..., SO_ERROR, ...)` 判断连接最终成功还是失败。

## 关键边界

- `ioctlsocket(..., FIONBIO, ...)` 只改变阻塞模式：非零值开启非阻塞，零值恢复阻塞。
- `fd_set` 是供 `select()` 使用的观察集合；`FD_ZERO()` 清空集合，`FD_SET()` 加入 Socket。
- `select()` 返回就绪只表示连接过程已有结果，不等于连接成功。
- `getsockopt(..., SO_ERROR, ...)` 查询连接结果；`getsockopt()` 自己失败和 `SO_ERROR != 0` 是两层不同错误。
- 超时或连接失败后通常应关闭旧 Socket，并用 `socket()` 创建新 Socket 再重连。

## 详细教程

参见仓库文件 `02-TCP Socket/超时、断线与异常.md` 的第 4.4 节“Windows 关键函数逐项拆解”。

## 来源

- [[../00-inbox/2026-08-31-windows-非阻塞连接超时讲解]]
