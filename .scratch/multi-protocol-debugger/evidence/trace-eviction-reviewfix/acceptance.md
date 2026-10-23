# 回溯缩容淘汰的发送尝试清理

日期：2026-10-23。独立 `codex/rtu-records` 分支快进至集成基线 `023fcee`；沿用 Qt 6.12.0 / LLVM-MinGW Clang 22.1.7 / C++17 / Debug。

整体 Standards 审查确认：`configureTrace()` 删除已完成操作时遗漏 `sendAttempts_`，重复扩容/发送/缩容可积累旧发送条目。本次在删除操作前同步移除对应发送尝试，与 `submit()` 的普通淘汰路径一致。核对了全部操作删除路径：仅有这两处；停止与恢复路径整体清理发送尝试，不另改架构或 SDK。

公开回归 `evictedOperationIgnoresLateSendResult` 使用真实 Modbus 协议、受控端点和时钟：请求被接受但本地发送回调未到，超时后为 `transport-error`（本地发送超时的真实领域终态）；分别经缩容及普通提交淘汰，再交付旧发送令牌。两种路径都只追加无操作归属的 `stale-send-result` 诊断，不增加 `local-sent`，不改新操作的状态、结果、完成标志或本地发送标志。测试不访问私有容器。

修复前同一用例的 `configure-shrink` 明确失败，`submit-eviction` 通过，见 [失败对照](red-results.txt)。修复后必要 Debug 定向套件 3/3 CTest、51 项 QtTest 通过，0 失败/跳过：session 13、trace 12、recording 26；见 [CTest](ctest.log) 及三个结果文件。未执行 Release 或全量测试，最终组合回归交由集成统一执行；原 11/12 验收证据保持。

复现命令（仓库根目录）：

```powershell
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --preset qt-llvm-debug
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --build --preset qt-llvm-debug --target trace_tests session_tests recording_tests
& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --preset qt-llvm-debug -R '^(trace_query_export|session_lifecycle|bounded_disk_recording)$' --verbose
```

自审确认修复只增加与普通淘汰相同的按操作 ID 删除，保留未完成操作限制、过期计数与通知；无需更改磁盘格式。证据与本次三个可执行文件 SHA-256 见 [哈希清单](sha256.txt)。构建产物不提交；13 未启动，当前实施索引未修改。
