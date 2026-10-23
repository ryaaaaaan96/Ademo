# 09 / 12 组合验收

日期：2026-10-23（Asia/Shanghai）。分支 `codex/rtu-records`，独立 worktree `.worktrees/rtu-records`。以已验收的 12 `8be3807` 合并包含 09 最终提交 `95ff916` 的集成分支；组合生产修复提交为 `8bd8c60`。没有启动 10 或 13，本记录只覆盖 09 与 12 的衔接。

## 组合修改

- SDK7 会话恢复路径直接结束通信操作时调用 `recordOperation`，使 transport-error、未确认写入的 uncertain 及原因进入磁盘归档。
- session 事件包含宿主 recovery 策略。启动、恢复参数变化和协议配置发布会追加配置事件，支持先开启记录再启动；归档查询/导出通过 sessionConfiguration 保留最后可用配置。该字段不冒充每条历史操作发生时的配置。
- 保留 08 的 localOnly、working、停止后模型保护、configure 撤旧模型、ownsProtocol 与后台作业租约规则；重连不会重新读取从站文件或补发写入。
- 保留每个会话的 RecordingPanel，并与 09 的固定顶部启停/重连和可滚动内容布局组合。

## 专项审查修复与回归

根代理 master 只读审查发现两个 P2，修复后已进行源码复核并关闭：

| 发现 | 修复及公开行为回归 |
| --- | --- |
| 初始化失败后 lossDirty 可能让后台任务持续循环且占用槽位 | 初始化失败只排空已接纳事件，保留独立丢失统计后自动结束。`initializationFailuresReleaseBackgroundCapacity` 在同一被占目录创建并保留 12 个错误对象，不手动停止；错误均为目录冲突，另一个目录仍可开启，证明容量自动释放 |
| 归档按消息 operationId 提前过滤，漏掉另一操作观察窗口内的消息 | 删除提前过滤，继续使用有界消息预算并统一经 filterSnapshot 关联。`queuedOperationsKeepSameLiveAndArchivedMessages` 排队 A/B 读取，A 的已确认响应也在 B 的观察窗口；实时与归档在同一截止点保留相同消息，operationId 仍为 A |

`disconnectedWriteAndRecoveryPolicySurviveArchive` 额外覆盖：先记录后启动；写入已提交并本地完成后断线；原操作为 transport-error 且 uncertain=true；自动重连不补发写入；修改 recovery 后归档查询和 JSONL 均保留新策略及原操作终态。

首次共享消息回归比较了停止前实时快照与停止后归档，后者正常多出取消消息，因此失败。已将两者统一为停止后的同一截止点；对原提前过滤缺陷仍有检测能力。下面保存的是修正后的最终结果。

## 复现与最终结果

复用 Windows 11 x64、Qt 6.12.0 LLVM-MinGW、Clang 22.1.7、C++17、CMake 3.30.5、Ninja 1.12.1。执行：

```powershell
./scripts/verify.ps1 -EvidenceId integration09-12
```

| 检查 | Debug | Release |
| --- | --- | --- |
| 配置/构建 | [配置](qt-llvm-debug/configure.log)、[构建](qt-llvm-debug/build.log)成功 | [配置](qt-llvm-release/configure.log)、[构建](qt-llvm-release/build.log)成功 |
| CTest | [10/10 通过](qt-llvm-debug/ctest.log) | [10/10 通过](qt-llvm-release/ctest.log) |
| 回溯记录套件 | [23 passed / 0 failed](qt-llvm-debug/recording-results.txt) | [23 passed / 0 failed](qt-llvm-release/recording-results.txt) |
| 全部 QtTest | 218 passed / 0 failed / 0 skipped | 218 passed / 0 failed / 0 skipped |
| 哈希 | [清单](qt-llvm-debug/sha256.txt)，逐项重算 0 不符 | [清单](qt-llvm-release/sha256.txt)，逐项重算 0 不符 |

218 项含 10 个套件各自初始化/清理及参数化行；较 12 的 SDK6 基线 201 项，增加 09 的 14 项和本次 3 项组合回归。原 `evidence/12` 保持不变，不把旧证据覆盖成新版本结果。新目录保存所有实际测试日志、回归截图及产物哈希。

硬件、独立 Modbus 对端、真实拔线时序、磁盘写满/断电和持续吞吐均未新增验证，继续沿用 12/13 的实际边界。整体 Standards / Spec 审查由 03–12 最终集成阶段记录。
