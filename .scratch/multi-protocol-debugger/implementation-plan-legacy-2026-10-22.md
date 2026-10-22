# 实施 issue 索引

Status: needs-triage

当前产品规格已通过 `to-spec` 汇总并发布为 [ready-for-agent](spec.md)，主要测试入口已确认。下列实施 issue 保留各自状态和依赖；规格发布不表示全部任务已就绪。开发使用 VS Code，Qt 6.12.0 / LLVM-MinGW Clang 22.1.7 / CMake / Ninja / C++17 已通过 [本机核验](toolchain-environment.md)。

Q36 已确认 [架构基线](architecture.md)，接口设计见 [接口契约草案](interface-contracts.md)。以下是本地 Markdown issue，01 的工具链部分已核验，其余产品实现尚未开展。状态使用项目默认五种标签；`ready-for-agent` 表示任务描述可执行，不代表已经完成或自动开始执行。其余任务在前置产出核验后再分诊。

## 顺序与依赖

第一阶段完成 RTU 主从与公共模块；第二阶段实现 UPP/CAN；第三阶段实现 TCP 并完成整体交付。UPP 资料核对可以在资料到位后提前开展，UPP/CAN 产品实现仍在 RTU 阶段之后。若 UPP 资料未到位，保持阶段阻塞，不自动把 TCP 提前。

| 编号 | 工作 | 阶段 | 当前状态 | 前置 |
| --- | --- | --- | --- | --- |
| [01](issues/01-toolchain-and-modbus-selection.md) | 工具链与 Modbus 实现选型 | RTU | ready-for-agent | 无 |
| [02](issues/02-contracts-and-plugin-host.md) | 公共契约、插件宿主与会话生命周期 | RTU | needs-triage | 01 |
| [03](issues/03-serial-channel.md) | 串口通道与统一记录入口 | RTU | needs-triage | 02 |
| [04](issues/04-modbus-rtu-master.md) | RTU 编解码、主站读写和轮询 | RTU | needs-triage | 01、02、03 |
| [05](issues/05-modbus-rtu-slave-json.md) | RTU 从站、多设备与外部 JSON | RTU | needs-triage | 02、03、04 的公共协议实现 |
| [06](issues/06-local-trace-recording.md) | 本地回溯、导出和过载处理 | RTU | needs-triage | 02、03 |
| [07](issues/07-widgets-and-workspace.md) | Widgets 工作台和工作区 | RTU | needs-triage | 04、05、06 |
| [08](issues/08-rtu-interop-and-capacity.md) | RTU 互通、时序和容量验证 | RTU 验收 | needs-triage | 03–07、联调环境 |
| [09](issues/09-upp-spec-and-can-target.md) | UPP 规范核对与 CAN 支持组合 | UPP/CAN 准备 | needs-info | UPP 文档、报文样例、目标 CAN 资料 |
| [10](issues/10-can-and-upp-plugin.md) | CAN 传输与真实 UPP 插件 | UPP/CAN | needs-info | 08、09 |
| [11](issues/11-modbus-tcp.md) | TCP 通道与 Modbus TCP 主从 | TCP | needs-triage | 10、已有 Modbus 公共实现 |
| [12](issues/12-release-and-extension-validation.md) | 扩展验收和 Windows 交付 | 最终验收 | needs-triage | 08、10、11 |

02 之后可按依赖先准备 06，07 的界面框架也可提前搭建；这不改变 RTU → UPP/CAN → TCP 的协议实现顺序。实际串口和 CAN 验证结果与模拟输入测试分别记录。

## 第一项可执行工作

继续 01 的 Modbus 实现选型：工具链已按本机安装固定，并通过独立验证程序的 Debug/Release 编译与运行；尚需选择满足统一 I/O 的 Modbus 实现路径，为 02/04 提供依据。本次没有安装依赖或生成产品主工程。

## 明确的前置资料

- UPP：既定规范版本、CAN ID/帧规则、至少一组真实请求与应答或主动上报样例、校验/时序/分片等已有约束。
- CAN：目标适配器型号、驱动及 Qt 后端支持情况；未确定时不声明任意硬件兼容。
- RTU 联调：实际设备或独立参考实现、端口拓扑、波特率和串口格式；在 08 开始真实联调前补齐，不阻止 01 的技术核验。
- 性能：根据 08 的实测与目标负载固定可支持范围；可配置默认容量不能冒充性能保证。

## 分诊约定

实施者在开始 issue 前核验依赖产出，缺资料转为 `needs-info`，需要人工环境操作时按实际情况使用 `ready-for-human`。不使用不存在的自定义分诊标签。完成情况追加到对应文件的 `## Comments`，引用代码、验证证据和剩余限制。

## Comments

### 2026-10-22

由 Q36 已确认的通用架构拆分 12 个 issue；01 可开展，其余按依赖与资料继续分诊。Redis 接入不列为首版实施任务，仅保留后续经测量评估的选项。
