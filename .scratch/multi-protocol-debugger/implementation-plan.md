# 实施 ticket 索引

Status: ready-for-agent

2026-10-22 用户回复“按此发布”，确认 19 项功能切片及依赖。全部任务已按本地 Markdown 约定发布；父规格保持原文，旧版链接经历史入口继续有效。[正式规格](spec.md)、[已确认拆分](ticket-breakdown-draft.md)、[编号迁移表](ticket-migration.md)。

## 当前任务与直接依赖

本表保留原产品编号与完成历史；当前综合调度以 [新界面与后续阶段综合索引](../multi-protocol-workbench-ui/implementation-plan.md) 为准。编号只指本表链接，不按整个目录中的编号前缀猜测；同目录的旧版链接入口和 legacy-issues 归档不参与任务调度。

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
| [13](issues/13-rtu-phase-acceptance.md) | 验收 RTU 第一阶段的真实互通与负载 | RTU 阶段验收 | needs-info | UI13、10、12；另需外部条件 |
| [14A](issues/14-upp-can-receive.md) | 原始经典CAN帧观察、筛选与导出 | UPP/CAN | ready-for-agent | 软件完成，见[14A验收证据](evidence/14A/acceptance.md)；用户禁止桌面操作后采用离屏Qt验收，物理后置 |
| [14B](issues/14-upp-can-receive.md) | UPP接收重组与解析 | UPP/CAN | ready-for-agent | 14A已完成；用户授权暂定规则软件实施；设备规则/物理核验后置 |
| [15](issues/15-upp-can-exchange.md) | 完成真实 UPP/CAN 发送交互并与 RTU 并行 | UPP/CAN | needs-info | 14B软件部分；交互规则与样例；物理验收后置 |
| [16](issues/16-tcp-first-register-read.md) | 完成一次 Modbus TCP 主站读取 | TCP | ready-for-agent | 15软件部分 |
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
| 09 | `77fca54`、`95ff916` | [09 验收](evidence/09/acceptance.md)，Debug/Release 各 195 项通过 | 集成 Debug CTest 9/9；关闭确认竞态的 P2 已修复并独立复核，102 项哈希匹配 |
| 10 | `b97d660`、`963b78c`、`6797b25` | [10 验收](evidence/10/acceptance.md)，Debug/Release 各 234 项通过 | 含 09/12 与停止终态修复；四项专项 P2 已独立复核关闭，114 项哈希匹配 |
| 11 | `a4dec16` | [11 验收](evidence/11/acceptance.md)，Debug/Release 各 59 项通过 | 整合 03/04/11 后 Debug 构建与 CTest 4/4 通过 |
| 12 | `522336e`、`8be3807`、`8bd8c60`、`4c7d651` | [12 验收](evidence/12/acceptance.md)、[09/12 组合](evidence/integration09-12/acceptance.md) | 两配置各 CTest 10/10、218 项；两个专项 P2 修复并独立复核，108 项哈希匹配 |

03–12 已逐项合入并完成最终软件验收。停止写操作的终态保护已以 `6d69845` 修复合入，[定向 Debug 验收](evidence/stop-uncertain-reviewfix/acceptance.md) 5/5、98 项通过。最终源码 `762d85d` 的 Debug/Release 均 CTest 11/11、238 项通过，0 失败/跳过，114 项产物与证据哈希一致；[整体验收](evidence/rtu-03-12/acceptance.md)、[Standards / Spec 审查](evidence/rtu-03-12/review.md) 均已记录，两轴剩余问题均为 0。13 等待真实硬件条件，14–19 不启动。

## 依赖与状态规则

- Status 沿用默认五种分诊标签。ready-for-agent 表示任务描述完备；按 Blocked by 指定的软件阶段或完整任务检查证据，不能把软件阶段通过写成整项物理验收完成。
- 2026-10-24 用户明确要求硬件条件先跳过、实际物理验收后做。13暂缓，不阻塞14；14/15继续等待 UPP 规范、样例及交互规则，软件部分不等待实际硬件；19的清洁 Windows 交付条件保留。needs-info 的具体缺口按各任务当前正文记录。
- 以各任务验收勾选与 Comments 中的代码、测试和实际环境证据认定完成，不把 ready-for-agent 当作完成状态，不使用 open/claimed/resolved 等 Wayfinder 状态代替普通实施标签。
- 02 后可分别推进 03、04、11；03 后可分别推进 05、06；06 后可推进 08，07 还需 04；16 后可分别推进 17、18。
- 13 的前置 10、12 的祖先覆盖全部 RTU 开发项。软件调度为 UI13→14A原始经典CAN观察→14B UPP接收解析→15软件→16。14A/B为旧14内部切片，完整任务保留needs-info；14A范围已确认，不等待UPP缺口，14B/15仍按相关资料解锁。物理验收后置，保持RTU→UPP/CAN→TCP。
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

### 2026-10-23：接入已批准的综合计划

已发布 [UI01–UI13 与旧13–19综合索引](../multi-protocol-workbench-ui/implementation-plan.md)。旧01–12的完成记录保留，旧13增加UI13前置，旧13–19按新界面补充验收；本轮仅继续UI01原型，不开始后续产品实施。

### 2026-10-24：用户要求物理验收后置

用户明确表示“我希望硬件条件先跳过，实际物理验收后面再做”。据此撤销实际硬件验收对后续软件开发的阻塞，依赖改按对应软件阶段通过检查；既定 UPP 规范、字段预期和交互资料仍是实现前提。保留物理验收项且不标为完成，历史评论中的硬件阶段门槛由本次决定更新。软件实现继续按 RTU → UPP/CAN → TCP 推进。本次只更新计划和验收边界，未执行物理测试或启动 UPP 产品实现。

### 2026-10-24：Q1–Q3确认首个经典CAN软件切片

用户依次确认：Q1先做原始CAN帧观察、筛选及导出；Q2采用虚拟CAN样例源经正式接收后端进入现有会话界面，无硬件验收，不增加文件导入/定时回放；Q3采用经典CAN标准/扩展格式，远程帧与错误帧按后端实际信息保留，CAN FD后置。沿用已确认界面和物理验收后置安排。

据此将旧14内部明确为14A（原始帧观察，范围ready-for-agent，仍依赖UI13软件收尾）与14B（UPP接收重组解析，needs-info）；旧15仅在14B及交互资料到位后推进。旧14完整目标与后续编号不变。已收到UPP原文并整理，不再把缺口表述为整份协议未提供。更新规格、索引与验收边界；本次没有实施产品代码或执行CAN测试。

### 2026-10-24：14B暂定接收软件实施

用户授权先按建议实现、后续修改。14B以70e9301规则基线、1c3f328实现、ea245f2审查修复交付全部CAN适用功能接收解析；证据见[14B软件验收](evidence/14B/acceptance.md)。本轮只有离屏软件验证，设备规则仍待核验，旧14整体保留needs-info。旧15和TCP仍未启动，不因本软件版本通过自动调度。
