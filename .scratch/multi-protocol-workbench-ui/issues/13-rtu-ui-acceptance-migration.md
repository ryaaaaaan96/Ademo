# UI13：验收 RTU 新界面完整流程并完成旧入口迁移

Status: ready-for-agent
Stage: RTU 新界面
Blocked by: [UI09](09-window-trace-query-export.md)、[UI10](10-qml-parameter-trends.md)、[UI11](11-window-workspace-recovery.md)、[UI12](12-plugin-ui-contract-validation.md)、[UI01](01-prototype-layout-and-flow.md)、[UI08](08-slave-runtime-file-workflow.md)

依据：[界面规格](../spec.md)、[综合实施索引](../implementation-plan.md)、[已批准拆分](../ticket-breakdown-draft.md)。复用已验收的旧 01–12 与 [Qt/LLVM-MinGW 工具链](../../multi-protocol-debugger/toolchain-environment.md)，不重做协议或传输基线。

## What to build

 在正式产品中完整走通创建、多会话/多设备、参数读写与轮询、文件保存/应用、曲线、回溯、工作区恢复和关闭，由用户验收新界面。

- [ ] 结合实际 Qt 交互与会话公开接口回归，覆盖待提交值、文件编辑和模拟值三类修改，验证多窗口提示期间的新数据不丢失。
- [ ] 对比 UI01 的已确认反馈，实际检查窗口浮动/停靠、表格可读性、操作目标及记录归属；原型截图不能替代正式界面验收。
- [ ] 在新路径覆盖通过后移除临时兼容入口和重复旧操作面板，保留协议、数据和配置兼容能力；相关旧测试随公共行为迁移。
- [ ] 记录 Debug/Release、实际 Qt/QML 运行与软件负载证据，审查两份规格的组合实现；不宣称完成真实硬件验收。

**覆盖新规格故事：** 68、70、71，以及 UI01–UI12 的组合交互。

## 验证方式

沿用已确认的调试会话公开入口及可控传输、时钟，并补充本切片真实 Qt 界面/QML 操作验收。观察用户可见状态与同一会话的发送、数据、文件及记录，不依赖私有容器或精确像素。执行相关定向回归和必要 Debug/Release 验证，附实际环境、截图及未覆盖条件。

## Comments

### 2026-10-23：经用户确认发布综合任务

用户要求按综合拆分发布任务，然后继续 UI01，复用已有原型并启动供操作；确认界面后再进入正式实施。任务发布不代表本项已完成，ready-for-agent 也不代表其依赖已满足。原产品旧 13 仍需真实硬件条件，旧 14–19 不提前启动。
