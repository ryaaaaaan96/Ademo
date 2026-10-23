# UI10：用 QWidget 内嵌 QML 展示设备参数趋势

Status: ready-for-agent
Stage: RTU 新界面
Blocked by: [UI06](06-selected-group-polling.md)

依据：[界面规格](../spec.md)、[综合实施索引](../implementation-plan.md)、[已批准拆分](../ticket-breakdown-draft.md)。复用已验收的旧 01–12 与 [Qt/LLVM-MinGW 工具链](../../multi-protocol-debugger/toolchain-environment.md)，不重做协议或传输基线。

## What to build

 从参数入口打开实际 QML 曲线，选择同设备多个数值参数，启用已有轮询后持续观察，并能操作图例、暂停显示、清空和缩放。

- [ ] QWidget/QQuickWidget 承载真实 QML 渲染，数值来源保持会话/设备/参数身份，不由 QML 直接访问端口。
- [ ] 打开曲线不启动读取，暂停显示不停止通信，清空图形不删除协议数据或记录。
- [ ] 验证采样/刷新受控和视图切换不串数据；不把原型曲线数量、行数或独立归一化显示当作正式约束。

**覆盖新规格故事：** 53–59、69。

## 验证方式

沿用已确认的调试会话公开入口及可控传输、时钟，并补充本切片真实 Qt 界面/QML 操作验收。观察用户可见状态与同一会话的发送、数据、文件及记录，不依赖私有容器或精确像素。执行相关定向回归和必要 Debug/Release 验证，附实际环境、截图及未覆盖条件。

## Comments

### 2026-10-23：经用户确认发布综合任务

用户要求按综合拆分发布任务，然后继续 UI01，复用已有原型并启动供操作；确认界面后再进入正式实施。任务发布不代表本项已完成，ready-for-agent 也不代表其依赖已满足。原产品旧 13 仍需真实硬件条件，旧 14–19 不提前启动。
