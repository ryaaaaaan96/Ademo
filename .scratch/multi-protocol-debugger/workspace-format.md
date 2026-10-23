# 工作区文件格式（issue 10）

工作区为 UTF-8 JSON，`version` 为整数 `1`，`sessions` 为至多 16 项的数组。每项包含 `title` 和 `configuration`；后者包含非空 `protocolId`、`transportId`、`role` 字符串，以及 `endpoint`、`protocol`、`recovery` 对象。`recovery` 沿用 09 的自动重连配置。这里只保存配置，绝不序列化运行中通道、未确认写操作或内存设备数据；设备内容仍由外部文件负责。

根 `layout` 保存窗口 geometry（Qt base64）和 activeSession；会话 `layout` 保存 scrollX/scrollY。未知根、会话、configuration、layout 及插件对象字段通过原始 JSON 合并保留，已知字段以当前界面真实配置覆盖。未启动时修改的串口和轮询也会保存。

主体调用协议与传输插件的 SDK 8 `relocateConfiguration(config, fromBase, toBase)` 纯值钩子，不解释插件路径。`toBase` 为空时返回绝对引用，否则返回目标目录下的相对引用；钩子不得做磁盘 I/O。Modbus 实现只转换 `devices[].path`，保存和加载均保留设备未知字段。缺插件时不猜测字段，原 JSON 与绝对 `referenceBase` 一起保存；另存后仍保留原基准目录。修复插件并重启后，插件按保留基准解析原引用。不兼容、加载失败及缺失不会阻止其他会话配置恢复；依赖会话的启动按钮禁用并显示插件诊断。

`Workbench.openWorkspace(path)` / `saveWorkspace(path)` 返回操作 ID，零表示路径无效或任务忙；`workspaceOperation(id)` 返回 working/success/file-error/cancelled 与说明。保留最近 16 个操作。每个窗口一次任务、全局最多 4 个已提交任务，无无界排队。路径必须为绝对路径，文件上限 8 MiB。读、JSON 解析与完整结构校验、写和 QSaveFile 原子提交在后台执行；窗口销毁后的回调由 QPointer 忽略。保存前使用同一结构校验，不会把无插件空白页写成无法再打开的文件，也不会破坏已有文件。

打开先完整验证再询问现有会话的未保存数据。所有确认结束后，在无事件泵区间复查全部会话 dirty/busy，再一次性替换标签；失败或取消保留旧会话。打开后所有会话保持 Stopped，由操作人员启动；从站启动时才后台读取并验证外部设备文件。保存是点击时配置快照，后台执行期间的新修改需再次保存，界面明确说明此语义。

SDK 8 IID 和构建指纹要求主体与全部插件配套重编译，不承诺 ABI 兼容旧版。未使用真实串口或硬件，本阶段证据为公开会话、可控端点、实际 DLL 加载与 Widgets 流程。
