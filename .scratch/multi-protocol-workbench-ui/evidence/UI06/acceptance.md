# UI06 参数范围读取与轮询验收

日期：2026-10-23。任务：[UI06](../../issues/06-selected-group-polling.md)。独立分支 `codex/ui06-group-polling`，起点 `e88a733`；实施提交 `6f44feb`，通过 `aa0f41d` 合入集成 `ff6bc7b`（UI09）。证据和任务更新由本次最终提交固定，集成提交由综合索引记录。

## 已实现行为

- 参数操作页新增“读取选中项”和“读取当前组”。目标限定当前设备、当前组，只合并同数据区的连续地址，不读跨越的未选地址；寄存器按 125 字、位区按 2000 位上限拆分。超过 64 个独立请求整体拒绝，已排队导致的容量拒绝有明确结果。
- 明确选择“选中项／当前组”和 10..3600000 ms 周期后开始轮询。配置捕获设备、数据区、地址和周期；切换设备、组、页面不重新绑定采集。任务列表可查看所有已配置目标。
- 再次开始更新当前设备当前组的任务，保留其他设备、其他组和不带分组标记的旧任务。共用既有 RTU 会话、轮询调度、超时和读取重试；开始调整时沿用在途／队列忙碌保护，拒绝原因可见，旧配置保留。
- “停止会话轮询”通过本地 `stop-polling` 操作撤下后续采集，保持端点、手动队列、在途读取和它的读取重试。使用运行实例已校验的配置清空 polls，不允许停止操作替换其他配置。
- 实际轮询结果更新最近读回；活动编辑器及黄色待提交值保持。轮询只能包含读取，设备设置仍走既有明确写入且不自动重试的路径。

参数范围合并在插件内部 `parameter_read_scope`，Modbus 语义不进入通用宿主或参数模板。沿用 SDK11，无 IID、工作区版本或工具链变更。操作说明见 [参数轮询](../../../../docs/parameter-polling.md)。

## TDD 与行为证据

使用规格批准的公开 SessionAccess／DebugSession、可控传输与时钟，以及实际 Qt 表格选区、可见文字、可访问名称与键盘／鼠标操作，没有私有对象名或精确像素断言。

1. [选区读取 red](red-selected-read.txt) 缺少读取入口；[green](green-selected-read.txt) 验证 holding107/108 合并成一帧，input107 独立读取，不包含未选 holding110 或设备18。
2. [轮询 red](red-polling.txt) 缺少开始入口；[green](green-polling.txt) 在真实轮询更新 555→666→777 期间，活动输入和提交后的黄色草稿123均保持，切换组仍读取原107目标。
3. [停止 red](red-stop-inflight.txt) 旧 configure-polling 的忙碌保护导致在途时不能停止；[green](green-stop-inflight.txt) 增加独立本地停止操作后，可立即撤下后续轮询，在途结果仍回到原参数，会话仍可手动读取。
4. 扩展用例验证设备17/18的 200/500 ms 独立范围、125+1 寄存器拆分、65个稀疏范围整体拒绝，以及停止后在途读取仍重试一次且保留重试设置和未知配置字段。最终 [offscreen](offscreen-results.txt) 9项通过（含初始化／清理），仅作为开发过程证据。

## 原生 Windows 回归

| 配置与范围 | 结果 | 证据 |
| --- | --- | --- |
| Windows Debug 全量 | 16/16 CTest，282项 QtTest，0失败／跳过 | [日志](debug-ctest.log)、debug/原始结果 |
| Windows Release 相关回归 | 8/8 CTest，119项 QtTest，0失败／跳过 | [日志](release-ctest.log)、release/原始结果 |

Release 覆盖新增参数组轮询、待提交写入、参数文件、窗口读取、并行恢复、原 RTU 轮询／明确写入和工作区；没有将定向 Release 回归称为完整全量。构建日志为 [Debug](integration-debug-build.log) 与 [Release](integration-release-build.log)。复用 Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17 及既有 qt-llvm-debug/qt-llvm-release 预设，构建并发为3；构建与测试分开执行，Windows 测试串行且独占原生桌面时段。

`ctest.exe --preset qt-llvm-debug -R parameter_group_polling --output-on-failure` 可定向复现；切换为 qt-llvm-release 验证 Release。

[多设备轮询客户区截图](multi-device-polling.png) 来自真实 Windows Qt 测试，已目视检查读取入口、范围／周期、停止入口与双设备任务显示。截图与自动测试不代替用户对最终整合界面的确认，也不代表真实串口硬件互通。

## 自检与后续接口

自检关注范围不跨设备／数据区／空洞，调度继续使用原队列；停止不丢在途归属，保留读取重试；刷新只更新观测值，不覆盖草稿。`git diff --check` 通过。独立 Standards/Spec 整体审查留给集成阶段。

UI10 可继续读取既有 `SessionAccess.messages`：成功 operationResult 的 unit、area、start、values 提供地址值，排除 freshnessUncertain；row id 为 `unit/area/address`。曲线订阅既有手动读／轮询结果，不为显示曲线自动发命令。没有新增 SDK 数值来源接口。

UI07 保留参数页的局部新增控件与方法即可；文件页和当前选择不影响会话内已捕获 polls。后续移除兼容配置页时须保留会话级超时／读取重试的可编辑入口，本任务继续沿用原设置。

散列清单按 UTF-8/LF 归一化文本、PNG 原始字节固定证据；构建产物不入 Git。旧13仍等待真实硬件条件，旧14–19未启动。
