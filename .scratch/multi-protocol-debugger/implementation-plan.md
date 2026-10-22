# 实施 ticket 索引

Status: ready-for-agent

2026-10-22 用户回复“按此发布”，确认 19 项功能切片及依赖。全部任务已按本地 Markdown 约定发布；父规格保持原文，旧版链接经历史入口继续有效。[正式规格](spec.md)、[已确认拆分](ticket-breakdown-draft.md)、[编号迁移表](ticket-migration.md)。

## 当前任务与直接依赖

以下表格是唯一的当前任务清单。编号只指本表链接，不按整个目录中的编号前缀猜测；同目录的旧版链接入口和 legacy-issues 归档不参与任务调度。

| 编号 | 可交付流程 | 阶段 | Status | Blocked by |
| --- | --- | --- | --- | --- |
| [01](issues/01-rtu-session-lifecycle.md) | 启动与停止 RTU 串口会话 | RTU | ready-for-agent | 无 |
| [02](issues/02-rtu-first-register-read.md) | 完成一次 RTU 保持寄存器读取 | RTU | ready-for-agent | 01 |
| [03](issues/03-modbus-four-area-read.md) | 读取四类 Modbus 数据区 | RTU | ready-for-agent | 02 |
| [04](issues/04-modbus-explicit-write.md) | 执行不会自动重试的设备写入 | RTU | ready-for-agent | 02 |
| [05](issues/05-rtu-polling-read-retries.md) | 多设备轮询与会话级读取重试 | RTU | ready-for-agent | 03 |
| [06](issues/06-rtu-slave-json-read.md) | 从外部 JSON 启动多设备 RTU 从站并读取 | RTU | ready-for-agent | 03 |
| [07](issues/07-rtu-slave-write-response.md) | RTU 从站接受设备写入并反馈数据变化 | RTU | ready-for-agent | 04、06 |
| [08](issues/08-slave-live-edit-import-save.md) | 运行中编辑、导入和保存从站数据 | RTU | ready-for-agent | 06 |
| [09](issues/09-parallel-sessions-recovery.md) | 同时调试多个会话并处理断线恢复 | RTU | ready-for-agent | 05、07 |
| [10](issues/10-workspace-plugin-recovery.md) | 保存恢复工作区并处理插件缺失 | RTU | ready-for-agent | 08、09 |
| [11](issues/11-trace-query-export.md) | 按操作与消息回溯并导出记录 | RTU | ready-for-agent | 02 |
| [12](issues/12-bounded-disk-recording.md) | 开启磁盘回溯并在过载时保持通信 | RTU | ready-for-agent | 05、11 |
| [13](issues/13-rtu-phase-acceptance.md) | 验收 RTU 第一阶段的真实互通与负载 | RTU 阶段验收 | needs-info | 10、12；另需外部条件 |
| [14](issues/14-upp-can-receive.md) | 通过插件接收并解析真实 UPP/CAN 消息 | UPP/CAN | needs-info | 13；另需外部条件 |
| [15](issues/15-upp-can-exchange.md) | 完成真实 UPP/CAN 发送交互并与 RTU 并行 | UPP/CAN | needs-info | 14；另需外部条件 |
| [16](issues/16-tcp-first-register-read.md) | 完成一次 Modbus TCP 主站读取 | TCP | ready-for-agent | 15 |
| [17](issues/17-tcp-master-operations.md) | TCP 主站多设备读写、轮询与恢复 | TCP | ready-for-agent | 16 |
| [18](issues/18-tcp-slave-shared-data.md) | TCP 从站多客户端共享模拟设备 | TCP | ready-for-agent | 16 |
| [19](issues/19-windows-delivery.md) | 交付可运行的 Windows 工具与扩展说明 | 最终交付 | needs-info | 17、18；另需外部条件 |

## 第一项可执行工作

2026-10-22，[01：启动与停止 RTU 串口会话](issues/01-rtu-session-lifecycle.md) 和 [02：完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md) 已完成验收勾选，分别见 [01 证据](evidence/01/acceptance.md)、[02 证据](evidence/02/acceptance.md)，对应提交 `f5b962d`、`1fc90db` 已核验。分诊标签保持不变，不用它代表完成状态。本轮实施范围为 03–12；13 等待真实硬件联调条件，14–19 不启动。

Qt/LLVM-MinGW、Debug/Release 与 LLDB 验证已完成，见 [环境证据](toolchain-environment.md)，不重复选型。原工具链 issue 的 Modbus 实现路径选择已由 [02](issues/02-rtu-first-register-read.md) 完成核验，见 [路径决定](modbus-implementation-path.md)；其余功能码和主从实现仍按任务依赖推进。

## 本轮完成记录

| 任务 | 实施提交 | 验收证据 | 集成验证 |
| --- | --- | --- | --- |
| 03 | `dccb047` | [03 验收](evidence/03/acceptance.md)，Debug/Release 各 56 项通过 | 集成 Debug 构建与 CTest 2/2 通过 |
| 04 | `ee4a71c`、`176e1bb` | [04 验收](evidence/04/acceptance.md)，Debug/Release 各 85 项通过 | 整合 03/04 后 Debug 构建与 CTest 3/3 通过 |
| 05 | `c77a9a6` | [05 验收](evidence/05/acceptance.md)，Debug/Release 各 117 项通过 | [05/06 组合验收](evidence/integration05-06/acceptance.md)，两配置各 147 项通过 |
| 06 | `dd39760` | [06 验收](evidence/06/acceptance.md)，Debug/Release 各 131 项通过 | 同上，SDK5 合并提交 `0943bc2` |
| 07 | `48df1a6` | [07 验收](evidence/07/acceptance.md)，Debug/Release 各 156 项通过 | 整合 05/06/07/11 后 Debug 构建与 CTest 7/7 通过 |
| 08 | `0559225` | [08 验收](evidence/08/acceptance.md)，Debug/Release 各 181 项通过 | 复用完整 03–08/11 组合；专项配置覆盖问题修复并复核关闭，产物与证据哈希匹配 |
| 11 | `a4dec16` | [11 验收](evidence/11/acceptance.md)，Debug/Release 各 59 项通过 | 整合 03/04/11 后 Debug 构建与 CTest 4/4 通过 |

03–08、11 已合入，09、12 实施中。10 等待 09；13–19 不启动。

## 依赖与状态规则

- Status 沿用默认五种分诊标签。ready-for-agent 表示任务描述完备；只有其所有 Blocked by 任务有验收完成证据、外部条件满足时才可开始。
- 13 等待独立对端、实际串口与拓扑等联调信息；14/15 等待 UPP 规范、样例及 CAN 硬件/交互条件；19 等待清洁 Windows 交付验证环境。当前这四项为 needs-info，信息确认后可更新状态，但仍需遵守任务依赖。
- 以各任务验收勾选与 Comments 中的代码、测试和实际环境证据认定完成，不把 ready-for-agent 当作完成状态，不使用 open/claimed/resolved 等 Wayfinder 状态代替普通实施标签。
- 02 后可分别推进 03、04、11；03 后可分别推进 05、06；06 后可推进 08，07 还需 04；16 后可分别推进 17、18。
- 13 的前置 10、12 的祖先覆盖全部 RTU 开发项。13→14 和 15→16 包含用户确定的阶段约束，保持 RTU → UPP/CAN → TCP，不因资料缺失越过阶段。
- UPP 资料整理可以提前开展；未知线格式不编造，真实硬件验收不得用模拟替代。缺资料不阻塞独立的前期 RTU 工作。

## 版本与历史

[旧版 12 项索引](implementation-plan-legacy-2026-10-22.md) 和 [旧 issue 原文](legacy-issues/README.md) 已保留。父规格中的旧 12 项说明是发布前上下文，当前执行以本表为准；其旧链接仍可到达原文和后继任务。

## Comments

### 2026-10-22：RTU 03–12 并行实施启动

用户要求先提交现有规格、设计、任务和工具配置；共享基线为 `fb14cb8`，构建目录继续忽略。集成分支为 `codex/rtu-03-12-integration`。应用 worktree 工具因 Git 所有权检查失败，改用命令级 safe.directory 创建仓库内独立 worktree，不改全局信任配置。

- 首批：03、04、11 各在独立 worktree 和分支实施。
- 后续：03 验收后启动 05、06；04+06 后启动 07，06 后启动 08；05+07 后启动 09；08+09 后启动 10；05+11 后启动 12。
- 每项先完成对应会话入口测试、必要界面证据与任务回写，提交并逐项验证后合入；最终以共享基线执行 Standards / Spec 整体审查。
- 根代理统一更新本索引，实施者更新各自 issue 和 evidence，避免用标签误报完成。01/02 只回归，不重复实施；硬件条件未确认时不把受控传输结果计为 13 完成。

### 2026-10-22：发布确认后的 19 项任务

按用户批准的拆分发布 19 个独立任务，保留全部验收边界、直接依赖及 68 条用户故事的对应关系。12 份旧 issue 原文与评论按字节归档，旧路径转换为明确的历史入口；不以新编号覆盖旧任务的含义。未改动父规格、产品代码或已有工具链验证。
