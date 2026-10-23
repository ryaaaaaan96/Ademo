# UI05 单值列待提交与设备写入验收

日期：2026-10-23。任务：[UI05](../../issues/05-inline-pending-value-write.md)。分支 `codex/ui05-pending-writes`，起点集成 `4e4aa20`；实现提交 `2250654`，通过 `aa4ebc4` 同步集成 `2187294` 的原生窗口能力。最终修正、任务勾选及本文由同次 Git 提交固定，最终提交见综合索引。

## 已实现行为

- 通用参数表接收插件声明的可编辑列、待提交标记、说明及编辑回调。刷新跳过活动编辑器的模型单元格，Modbus 插件分别保存最近读回、草稿、提交修订号和操作结果；实时读取不能覆盖活动输入或黄色待提交值。
- 保持寄存器只接受 0..65535 整数，线圈接受 true/false 或 0/1；输入寄存器和离散输入不提供编辑／设置。只有行内“设置”调用既有 RTU 写入会话，格式错误有可见提示且不发送。
- 本地发送完成不清除待提交。设备成功确认仅清除对应提交修订的草稿，显示仍为最近读回；旧确认不清除随后输入的新草稿。状态明确显示上次提交值，避免把另一个黄色草稿误解为已确认值。
- 异常、超时、断线和停止均保留待提交与结果证据；不确定结果明确标示，写入沿用既有不重试／不自动补发语义。通信记录仍由现有会话保留。
- SDK11 新增 `SessionAccess.registerViewState(owner, state, resolve)`，多个视图状态与旧协议运行数据守卫独立组合，owner 销毁时撤销；关闭、配置替换和多窗口最终复查均看得到主站待提交值。确认期间的新修改会阻止继续关闭。
- 切换设备或替换配置取消时保持原选择和输入；放弃只清除界面草稿，不撤销已经发送的设备写入。应用新参数定义后，其他设备取确认后的实时状态，不复活已放弃草稿或覆盖新观测。
- 宿主在复制模态确认回调前持有插件目录租约，保证会话和最后视图被销毁时，局部插件回调先析构、DLL 后释放。

## TDD 与运行证据

测试入口沿用规格已确认的公开会话接口、可控传输／时钟及真实 Qt 公共操作；没有添加私有对象名或精确布局断言。

1. [编辑 red](red-pending-write.txt)：双击没有编辑器。实现后，在活动输入123期间读回555，输入保持；只有设置才发出 `11 06 00 6b 00 7b …`，本地发送不清草稿，正确回显确认后恢复最近读回555。
2. [会话保护 red](red-pending-guard.txt)：旧 `hasUnsavedOrBusy()` 看不到主站草稿。接入独立视图状态后，取消设备切换、配置替换及关闭均保留值；实际 QMainWindow 关闭取消继续运行，放弃关闭保留配置，重开保持停止。
3. [可见校验 red](red-visible-validation.txt)：非法输入只有内部状态而没有可见说明；修复后65536拒绝发送并保留待提交。
4. [替换保护 red](red-replacement-guard.txt)：替换另一个设备时，旧快照会复活已放弃的草稿；修复后从确认后的实时行保留其他设备。
5. 扩展验证真实线圈帧 `11 05 00 ac ff 00 4e 8b`、只读权限、旧写确认不清新草稿、exception/timeout/断线/停止均不重试、多个视图守卫的新修改复查和 owner 生命周期。前期 offscreen 日志用于 TDD，不作为原生桌面验收。

| 配置与范围 | 结果 | 证据 |
| --- | --- | --- |
| Windows Debug 全量 | 14/14 CTest，268 项 QtTest，0失败／跳过 | [日志](debug-ctest.log)、debug/原始结果 |
| 最终 Debug 交叉复测 | 5/5 CTest，64项通过 | [日志](debug-final-ctest.log)、debug-final/原始结果 |
| 最终 Windows Release 相关回归 | 7/7 CTest，103项通过 | [日志](release-ctest.log)、release/原始结果 |

完整 Debug 后，自检及集成协调审查补充了模态回调目录租约和“提交值”显示，再次编译两配置；最终 Debug 覆盖待提交、参数文件、窗口恢复、从站编辑和工作区，Release 额外覆盖首读和原 RTU 写入。没有把局部 Release 回归称为完整全量；最终集成阶段仍执行全量双配置验收。

工具链为既有 Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17、CMake3.30.5 与 Ninja，复用 qt-llvm-debug/qt-llvm-release 预设，无全局环境变更。构建与可见测试分开调度，Windows 测试串行，避开其他代理的原生窗口遮挡检查。

定向复现：`ctest.exe --preset qt-llvm-debug -R parameter_pending_write --output-on-failure`；将预设替换为 qt-llvm-release 可复现 Release。`DEBUGGER_EVIDENCE_DIR` 指向目录时保存客户区截图。

## 视觉、自检与交接

[黄色待提交、只读行与明确提交结果](pending-value.png) 来自实际 Windows Qt 测试，已目视检查。示例显示先前提交true已成功，而新的false仍为黄色待提交；只读输入寄存器仅有读取按钮。它不是用户正式产品验收，也不代表真实串口互通。

自检确认协议字段与写入校验留在 Modbus 插件，模板不解释 Modbus，真实发送继续走既有会话；SDK/IID/构建指纹同步为11，旧插件需配套重编；旧从站修改保护回归通过；`git diff --check` 通过。整体独立 Standards/Spec 审查由集成阶段完成，不在本项冒称完成。

UI06 可扩展 `master_parameters.cpp` 的读取范围／轮询，并复用现有消息更新与草稿分离。UI07 文件页应使用独立 QObject owner 注册自己的 `LocalState` 和 resolve 回调，不替换待提交或旧从站守卫；state 为同线程只读检查，resolve 处理明确保存／放弃／取消。参数模板通过 `ParameterRow.editableCells/pendingCells/tooltips` 及 `ParameterTable.onEdit` 承载插件编辑能力。

散列清单固定本目录证据：文本按 UTF-8/LF 归一化，PNG 使用原始字节；源码由 Git 提交固定。构建产物不入 Git。UI06/07及其后续切片未在本项提前实施，旧13硬件条件和旧14–19边界不变。
