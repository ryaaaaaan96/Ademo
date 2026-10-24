# 多协议调试工作台综合实施索引

Status: ready-for-agent

2026-10-23 用户要求“按此发布综合任务，然后继续 UI01”，批准 ticket-breakdown-draft.md 的 13 个新切片和旧 13–19 续接。当前综合调度以本索引为准；原索引保留旧编号及完成历史，不复制旧任务。

依据：[界面规格](spec.md)、[原产品规格](../multi-protocol-debugger/spec.md)、[批准拆分原文](ticket-breakdown-draft.md)。

## 已完成基线

旧 01–12 保持已完成，复用已有会话、协议、传输、数据和记录能力。验收与提交见[原实施记录](../multi-protocol-debugger/implementation-plan.md)和[最终软件验收](../multi-protocol-debugger/evidence/rtu-03-12/acceptance.md)。新界面任务未完成不等于旧功能未实现，旧证据也不等于新界面验收。

## 当前任务与直接依赖

文件内 UI01–UI13 编号是新界面任务；旧13–旧19 指向原产品目录。Status 为分诊标签，实际完成以验收勾选、提交和证据为准。

| 编号 | 可交付流程 | Status | Blocked by | 执行记录 |
| --- | --- | --- | --- | --- |
| [UI01](issues/01-prototype-layout-and-flow.md) | 确认可操作原型的布局与关键流程 | ready-for-agent | 无 | 已完成；2026-10-23 用户确认，原型 bb3aa81 |
| [UI02](issues/02-window-first-rtu-read.md) | 在新的调试窗口完成一次 RTU 读取 | ready-for-agent | UI01 | 已完成：58fa24c；合入 85619ae；[验收与集成复测](evidence/UI02/integration.md) |
| [UI03](issues/03-dockable-session-lifecycle.md) | 同时运行并管理多个可停靠调试窗口 | ready-for-agent | UI02 | 已完成：69def47；合入 94c635f；[验收与集成复测](evidence/UI03/integration.md) |
| [UI04](issues/04-parameter-json-groups-read.md) | 加载 JSON 分组并按参数或地址读取设备 | ready-for-agent | UI02 | 已完成：d35470f；合入 cf69274；[验收与集成复测](evidence/UI04/integration.md) |
| [UI05](issues/05-inline-pending-value-write.md) | 在单值列编辑并明确设置设备参数 | ready-for-agent | UI04 | 已完成：dc075b7；合入 efe2930；[验收与集成复测](evidence/UI05/integration.md) |
| [UI06](issues/06-selected-group-polling.md) | 按选中项或参数组读取并持续轮询多个设备 | ready-for-agent | UI05 | 已完成：31fe1f4；合入 0da79ed；[验收与集成复测](evidence/UI06/integration.md) |
| [UI07](issues/07-file-editor-save-apply.md) | 在独立文件页编辑、保存并应用参数配置 | ready-for-agent | UI05 | 已完成：286cbee；合入 105ef2a；[验收与集成复测](evidence/UI07/integration.md) |
| [UI08](issues/08-slave-runtime-file-workflow.md) | 在新界面操作从站模拟值并独立保存、应用文件 | ready-for-agent | UI07 | 已完成：96d9b49；合入 503ea6f；[验收与集成复测](evidence/UI08/integration.md) |
| [UI09](issues/09-window-trace-query-export.md) | 在所属窗口回溯、查询并导出通信记录 | ready-for-agent | UI03 | 已完成：3c8b3d9；合入 2fa6317；[验收与集成复测](evidence/UI09/integration.md) |
| [UI10](issues/10-qml-parameter-trends.md) | 用 QWidget 内嵌 QML 展示设备参数趋势 | ready-for-agent | UI06 | 已完成：c008399；合入 640ce33；[验收与集成复测](evidence/UI10/integration.md) |
| [UI11](issues/11-window-workspace-recovery.md) | 保存并恢复多窗口工作区和插件配置 | ready-for-agent | UI03、UI06、UI08 | 已完成：90a475d；合入 1003854；[验收与集成复测](evidence/UI11/integration.md) |
| [UI12](issues/12-plugin-ui-contract-validation.md) | 通过独立示例插件验证表格与编辑页扩展 | ready-for-agent | UI03、UI07 | 已完成：e394d58；合入 66ffa7e；[验收与集成复测](evidence/UI12/integration.md) |
| [UI13](issues/13-rtu-ui-acceptance-migration.md) | 验收 RTU 新界面完整流程并完成旧入口迁移 | ready-for-agent | UI09、UI10、UI11、UI12 | 用户已确认正式界面；配置／侧栏／标题菜单增量已归档于 87126a3，屏幕外窗口恢复已修复并完成双配置软件回归，见[收尾证据](../window-screen-recovery/evidence/acceptance.md)。最终整体两轴审查随14A集成收尾记录 |
| [旧13](../multi-protocol-debugger/issues/13-rtu-phase-acceptance.md) | 验收 RTU 第一阶段的真实互通与负载 | needs-info | UI13、旧10/12（已完成）；硬件/对端 | 用户要求物理验收后置；未完成，不阻塞后续软件开发 |
| [旧14A](../multi-protocol-debugger/issues/14-upp-can-receive.md) | 原始经典CAN帧观察、筛选与导出 | ready-for-agent | UI13软件收尾 | 软件完成；虚拟CAN正式接收链与离屏Qt筛选导出已验收，见[14A证据](../multi-protocol-debugger/evidence/14A/acceptance.md)；无UPP解析/发送，物理后置 |
| [旧14B](../multi-protocol-debugger/issues/14-upp-can-receive.md) | UPP接收重组、校验、解析与来源回溯 | needs-info | 旧14A；相关协议缺口确认 | 文档已收到，缺口见整理文档第8节；旧14整体尚未完成，物理验收后置 |
| [旧15](../multi-protocol-debugger/issues/15-upp-can-exchange.md) | 完成真实 UPP/CAN 发送交互并与 RTU 并行 | needs-info | 旧14B软件部分；交互规则与样例 | 原始帧观察通过不足以解锁；发送元数据契约待演进，物理验收后置 |
| [旧16](../multi-protocol-debugger/issues/16-tcp-first-register-read.md) | 完成一次 Modbus TCP 主站读取 | ready-for-agent | 旧15软件部分 | 未启动 |
| [旧17](../multi-protocol-debugger/issues/17-tcp-master-operations.md) | TCP 主站多设备读写、轮询与恢复 | ready-for-agent | 旧16 | 未启动 |
| [旧18](../multi-protocol-debugger/issues/18-tcp-slave-shared-data.md) | TCP 从站多客户端共享模拟设备 | ready-for-agent | 旧16 | 未启动 |
| [旧19](../multi-protocol-debugger/issues/19-windows-delivery.md) | 交付可运行的 Windows 工具与扩展说明 | needs-info | 旧17、旧18；清洁 Windows/硬件 | 未启动 |

## 执行门槛

- 用户于 2026-10-23 明确确认 UI01，批准进入 UI02–UI13 正式实施。以 codex/workbench-ui-integration 为集成分支，每项使用独立 worktree 和分支，按依赖验证并合入。UI13 正式产品操作验收单独记录。
- UI02 后 UI03/UI04 可分别推进；UI05 后 UI06/UI07 可分别推进；按真实依赖逐项验证，不人为增加框架前置。
- UI13 祖先覆盖全部新界面软件流程。2026-10-24 用户要求先跳过硬件条件，实际物理验收后做；旧13保留待验，不阻塞后续软件实现。
- 软件顺序为 RTU/UI13收尾 → 14A原始经典CAN观察 → 14B UPP接收解析 → 15 UPP发送交互 → TCP。14A范围已确认且不依赖UPP歧义；14B/15按各自相关协议资料与软件证据解锁，不再等待物理验收。14A/B是现有旧14内部切片，不新增或重编号后续任务。
- 真实串口/CAN 互通、时序和负载分别保留待办。旧19的软件打包准备可按软件依赖推进，最终交付仍如实记录清洁 Windows 验证及未完成物理验收，不将软件证据写成硬件支持认证。
- 工具链、协议/主体边界和已确认测试入口保留；SDK/文件兼容演进在首个需要的正式切片中验证，不继承原型的临时 JSON 或容量限制。

## Comments

### 2026-10-23：综合任务发布并继续 UI01

已发布 UI01–UI13，旧13–19原编号和历史保留，旧13增加UI13前置；旧18澄清操作页即时修改与文件页保存/应用的差别。当前仅推进原型，尚无用户视觉与交互确认。

### 2026-10-23：UI01 已具备操作入口，尚未完成用户验收

复用并修正已有原型，原生 Qt/QML 截图冒烟通过；空从站保存崩溃已修复并独立复核。既有产品完整 Debug CTest 11/11、238项通过，产品代码未变。已通过 CMD 入口重新打开原型供用户操作，见 [UI01 准备证据与反馈项](evidence/UI01/acceptance.md)。Standards / Spec 代码审查均无剩余问题，但用户确认项仍未勾选；UI02–UI13及旧13–19未启动。

### 2026-10-23：UI01 用户确认并授权正式实施

用户明确表示“前端完成了可以开始接下来的实施了”，并调用 implement-spec。UI01 以 bb3aa81 原型为接受基准：白底通信记录、会话和绘图的 QMainWindow 系统三按钮、拖动悬停停靠／平铺、点击浮动居中。按综合实施索引进入 UI02–UI13，每项独立 worktree 和分支，验证后合入 codex/workbench-ui-integration。该确认来自用户反馈，不把程序冒烟扩称为人工逐步测试，也不代替 UI13 正式产品验收。旧13等待真实硬件，旧14–19不启动。

### 2026-10-23：正式软件实施、复审及最终双配置验收完成

UI02–UI13 软件切片均已按依赖合入。整体 Standards 初审两项生命周期问题由单个实施者集中修复，并修复直接关联的确认重入保护；Standards / Spec 修后复审均无遗留。最终源码4ae8f32在主目录Debug/Release各22/22套、358项通过，证据1321ea7。独立实施worktree已清理，正式Release已通过根目录CMD启动供操作。UI13仅用户正式操作确认仍待完成；旧13硬件和旧14–19门槛保持，详见[最终记录](evidence/UI13/final-review.md)。

### 2026-10-24：用户要求物理验收后置

用户明确表示“我希望硬件条件先跳过，实际物理验收后面再做”。据此撤销实际硬件验收对后续软件开发的阻塞，依赖改按对应软件阶段通过检查；既定 UPP 规范、字段预期和交互资料仍是实现前提。保留物理验收项且不标为完成，历史评论中的硬件阶段门槛由本次决定更新。软件实现继续按 RTU → UPP/CAN → TCP 推进。本次只更新计划和验收边界，未执行物理测试或启动 UPP 产品实现。

### 2026-10-24：Q1–Q3确认首个经典CAN软件切片

用户依次确认：Q1先做原始CAN帧观察、筛选及导出；Q2采用虚拟CAN样例源经正式接收后端进入现有会话界面，无硬件验收，不增加文件导入/定时回放；Q3采用经典CAN标准/扩展格式，远程帧与错误帧按后端实际信息保留，CAN FD后置。沿用已确认界面和物理验收后置安排。

据此将旧14内部明确为14A（原始帧观察，范围ready-for-agent，仍依赖UI13软件收尾）与14B（UPP接收重组解析，needs-info）；旧15仅在14B及交互资料到位后推进。旧14完整目标与后续编号不变。已收到UPP原文并整理，不再把缺口表述为整份协议未提供。更新规格、索引与验收边界；本次没有实施产品代码或执行CAN测试。
