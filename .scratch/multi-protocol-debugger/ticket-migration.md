# 实施 ticket 编号迁移

2026-10-22 用户确认发布 19 项功能切片。当前编号以 [实施索引](implementation-plan.md) 为准；旧版 12 项编号只用于历史追溯，不作为新任务依赖。

| 旧版任务原文 | 当前承接任务 |
| --- | --- |
| [旧 01：固定工具链并选择满足统一 I/O 的 Modbus 实现](legacy-issues/01-toolchain-and-modbus-selection.md) | [01 启动与停止 RTU 串口会话](issues/01-rtu-session-lifecycle.md)、[02 完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md) |
| [旧 02：建立公共契约、插件宿主与会话生命周期](legacy-issues/02-contracts-and-plugin-host.md) | [01 启动与停止 RTU 串口会话](issues/01-rtu-session-lifecycle.md)、[10 保存恢复工作区并处理插件缺失](issues/10-workspace-plugin-recovery.md)、[14 通过插件接收并解析真实 UPP/CAN 消息](issues/14-upp-can-receive.md)、[15 完成真实 UPP/CAN 发送交互并与 RTU 并行](issues/15-upp-can-exchange.md) |
| [旧 03：实现串口通道与统一原始记录入口](legacy-issues/03-serial-channel.md) | [01 启动与停止 RTU 串口会话](issues/01-rtu-session-lifecycle.md)、[02 完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md) |
| [旧 04：实现 Modbus RTU 编解码、主站读写和轮询](legacy-issues/04-modbus-rtu-master.md) | [02 完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md)、[03 读取四类 Modbus 数据区](issues/03-modbus-four-area-read.md)、[04 执行不会自动重试的设备写入](issues/04-modbus-explicit-write.md)、[05 多设备轮询与会话级读取重试](issues/05-rtu-polling-read-retries.md)、[09 同时调试多个会话并处理断线恢复](issues/09-parallel-sessions-recovery.md) |
| [旧 05：实现 RTU 多从站模拟与外部 JSON 数据](legacy-issues/05-modbus-rtu-slave-json.md) | [06 从外部 JSON 启动多设备 RTU 从站并读取](issues/06-rtu-slave-json-read.md)、[07 RTU 从站接受设备写入并反馈数据变化](issues/07-rtu-slave-write-response.md)、[08 运行中编辑、导入和保存从站数据](issues/08-slave-live-edit-import-save.md) |
| [旧 06：实现本地回溯、导出和通信优先的过载处理](legacy-issues/06-local-trace-recording.md) | [02 完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md)、[11 按操作与消息回溯并导出记录](issues/11-trace-query-export.md)、[12 开启磁盘回溯并在过载时保持通信](issues/12-bounded-disk-recording.md) |
| [旧 07：实现 Widgets 工作台、Modbus 视图和工作区](legacy-issues/07-widgets-and-workspace.md) | [01 启动与停止 RTU 串口会话](issues/01-rtu-session-lifecycle.md)、[02 完成一次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md)、[03 读取四类 Modbus 数据区](issues/03-modbus-four-area-read.md)、[04 执行不会自动重试的设备写入](issues/04-modbus-explicit-write.md)、[05 多设备轮询与会话级读取重试](issues/05-rtu-polling-read-retries.md)、[06 从外部 JSON 启动多设备 RTU 从站并读取](issues/06-rtu-slave-json-read.md)、[07 RTU 从站接受设备写入并反馈数据变化](issues/07-rtu-slave-write-response.md)、[08 运行中编辑、导入和保存从站数据](issues/08-slave-live-edit-import-save.md)、[09 同时调试多个会话并处理断线恢复](issues/09-parallel-sessions-recovery.md)、[10 保存恢复工作区并处理插件缺失](issues/10-workspace-plugin-recovery.md)、[11 按操作与消息回溯并导出记录](issues/11-trace-query-export.md)、[12 开启磁盘回溯并在过载时保持通信](issues/12-bounded-disk-recording.md) |
| [旧 08：验证 RTU 互通、时序与可支持负载](legacy-issues/08-rtu-interop-and-capacity.md) | [13 验收 RTU 第一阶段的真实互通与负载](issues/13-rtu-phase-acceptance.md) |
| [旧 09：核对既定 UPP 规范与目标 CAN 支持组合](legacy-issues/09-upp-spec-and-can-target.md) | [14 通过插件接收并解析真实 UPP/CAN 消息](issues/14-upp-can-receive.md)、[15 完成真实 UPP/CAN 发送交互并与 RTU 并行](issues/15-upp-can-exchange.md) |
| [旧 10：实现 CAN 传输与真实 UPP 协议插件](legacy-issues/10-can-and-upp-plugin.md) | [14 通过插件接收并解析真实 UPP/CAN 消息](issues/14-upp-can-receive.md)、[15 完成真实 UPP/CAN 发送交互并与 RTU 并行](issues/15-upp-can-exchange.md) |
| [旧 11：最后接入 TCP 通道与 Modbus TCP 主从](legacy-issues/11-modbus-tcp.md) | [16 完成一次 Modbus TCP 主站读取](issues/16-tcp-first-register-read.md)、[17 TCP 主站多设备读写、轮询与恢复](issues/17-tcp-master-operations.md)、[18 TCP 从站多客户端共享模拟设备](issues/18-tcp-slave-shared-data.md) |
| [旧 12：完成插件扩展验收与 Windows 交付验证](legacy-issues/12-release-and-extension-validation.md) | [19 交付可运行的 Windows 工具与扩展说明](issues/19-windows-delivery.md) |

旧 01 的工具链验证已经完成，证据直接复用；Modbus 实现选择由新 02 承接。旧 09 的 UPP 资料缺口由新 14/15 跟踪。新任务是功能切片，未把旧任务中尚未实现的产品能力误标为完成。

旧 issue 完整文件保存在与原目录同层的归档目录，因此其既有相对文档链接仍有效。原 issue 路径保留无分诊状态的旧链接入口，防止父规格或历史文档的链接失效；只有当前索引列出的 19 个文件参与实施。
