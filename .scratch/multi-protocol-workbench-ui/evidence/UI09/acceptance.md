# UI09：逐窗口回溯、历史与导出验收

日期：2026-10-23。任务：[UI09](../../issues/09-window-trace-query-export.md)。实施树 `build/worktrees/ui09`，分支 `codex/ui09-window-traces`，起点为集成 `4450c30`（UI03/UI04、SDK10）。源码、测试、任务与本记录随任务提交；最新集成同步后的验证在下文另记。

## 实施结果

- 每窗底部挂载一个 `SessionRecords`，包括“通信概要／回溯与导出／磁盘与历史”，折叠只隐藏记录区。窗口浮动、平铺、选择其他会话不替换其 DebugSession 或查询、导出目标。
- 删除原先重复的基本文本与两个独立长面板挂载；继续复用 `TracePanel`、`RecordingPanel` 和既有会话查询、后台记录／归档／导出，没有新增协议分支或记录后端。
- 概要从既有有界记录取最新80项，每项最多展示128字节及256字说明，文档最多100行；完整字节由导出取得。展开页沿用各类最多100项概要及有界后台快照，不复制无限缓存。三页记录输出均为白底深字。
- 历史 UUID 默认绑定所属窗口；刷新列表时，即使本会话尚无历史，也不会静默改选其他会话。跨会话历史必须由用户明确选择，结果与导出说明标明快照所属会话。
- 重新查询／列表刷新先废弃旧查询快照并禁用旧导出，失败后保持禁用；新的成功结果才可导出。已启动导出保留捕获快照，另一查询完成不会在导出未结束时重新开放导出入口。
- 近期和历史均保留未知原始记录、消息来源区间、缺失来源、过期／丢失／轮换及导出完整性说明。操作摘要分别显示已提交传输、本地发送完成和协议结果，本地操作有独立标签，不将本地发送当作对端确认。

## TDD 与软件验证

沿用规格已批准的公开会话接口、可控传输及真实 Qt 界面操作。新测试按用户可见标签、无障碍名称及公开 Workbench 接口驱动，不依赖私有对象名、窗口层级或精确像素。

1. [逐窗查询 red](red-window-query.txt) 在缺少所属记录区“回溯与导出”入口处失败；补齐后 [green](green-window-query.txt)。两个真实会话分别收到不同未知原始字节，第一窗产生错误消息；浮动第一窗并选择第二窗后，查询和导出仍只含第一窗证据。
2. [历史目标 red](red-history-owner.txt) 发现历史目标默认空白；修正默认 UUID、列表刷新和历史原始记录展示后 [green](green-history-owner.txt)。两个会话先后写入同一隔离归档目录，第一窗查询／导出仍只属于其 UUID，未知数据未丢失。
3. [阶段与过期 red](red-stages-retention.txt) 发现旧操作摘要没有区分传输提交和本地发送完成；补齐后 [green](green-stages-retention.txt)。缩小原始容量并注入大输入后，摘要有界、消息来源显示 expired，JSONL 导出保留缺失来源。
4. [新查询旧导出 red](red-history-change.txt) 发现开始查询另一目标时旧导出仍可用；修复后最终 Windows 套件通过。初次补充用例错误地将“没有分段的会话”当作查询失败，该状态按既有归档契约允许导出保留信息，见 [用例修正记录](history-query-case-refinement.txt)；最终改用不存在的目录验证真正失败，未更改后端的空历史语义。

| 配置 | CTest | QtTest | 证据 |
| --- | --- | --- | --- |
| Debug（SDK10） | 14/14 通过 | 260 通过，0 失败／跳过 | [完整日志](debug-ctest.log)、`debug/*results.txt` |
| Release（SDK10） | 14/14 通过 | 260 通过，0 失败／跳过 | [完整日志](release-ctest.log)、`release/*results.txt` |

使用已安装的 Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17、CMake3.30.5 和 Ninja；未安装工具、改变全局环境或使用真实端口。Qt 界面流程通过真实会话和独立契约测试 DLL 运行；该 DLL 不放入产品插件目录，也不宣称支持真实 UPP。

复现：`cmake.exe --preset qt-llvm-debug`、`cmake.exe --build --preset qt-llvm-debug --parallel 3`、`ctest.exe --preset qt-llvm-debug --output-on-failure`；Release 替换预设。新流程定向为 `-R window_trace_workflow`。可见 Qt 测试串行运行，避免其他窗口干扰原生停靠回归。

## 截图与验收边界

Release 的实际 Windows Qt 截图运行 [windows-capture.txt](windows-capture.txt) 为4项通过；[所属窗口回溯页](window-trace-query.png)、[回溯结果客户区](window-trace-result.png)、[历史页](window-history-export.png)、[历史结果客户区](window-history-result.png)已目视检查。结果显示所属 UUID、白底深字和分阶段状态；长表单和输出可滚动。截图由 QWidget::grab 取得，不包含系统标题栏，不替代用户产品验收。

本项没有启动旧13硬件联调或旧14–19；多显示器拖动手感及正式产品整体操作确认继续由 UI13 记录。自检确认原有后端和容量策略未改、各页引用同一会话、后台导出捕获有界值快照且查询失败不沿用旧目标。最终独立双轴审查由集成阶段执行。

## 最新集成同步复核（SDK11）

实施提交 `a2af256`；合并提交 `33706f9` 纳入当时最新集成 `a45a940`（含 UI05、SDK11 多视图 guard）。唯一文本冲突为 CMake 的新增测试段，保留 UI05 与 UI09 两个目标。记录实现没有改写参数表、host 租约或既有记录后端。

两配置以 `--parallel 3` 完整重编成功，见 [Debug 构建](post-merge-debug-build.log)、[Release 构建](post-merge-release-build.log)。其后串行运行 Windows 平台关联 CTest：`window_trace_workflow|parameter_pending_write|parallel_sessions_recovery|trace_query_export|bounded_disk_recording`。

| 配置 | CTest | QtTest | 时间 | 证据 |
| --- | --- | --- | --- | --- |
| Debug（SDK11） | 5/5 通过 | 76 通过，0 失败／跳过 | 30.28 秒 | [日志](post-merge-debug-ctest.log)、`post-merge-debug/*results.txt` |
| Release（SDK11） | 5/5 通过 | 76 通过，0 失败／跳过 | 23.00 秒 | [日志](post-merge-release-ctest.log)、`post-merge-release/*results.txt` |

关联验证覆盖新窗口记录流程、UI05 待写及租约守卫、UI03 原生窗口恢复、近期查询导出与磁盘有界服务。未重复无关协议全量；上表与之前 SDK10 的 14/14 全量结果明确区分。桌面已释放，自检 `git diff --check` 通过。

[SHA256 清单](SHA256SUMS.txt)固定本目录证据（不包含清单本身）；文本按 UTF-8/LF 归一化，PNG 使用原始字节，源码由 Git 提交固定。构建产物不入 Git。
