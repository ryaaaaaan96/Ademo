# UI08 从站参数与文件流程验收

日期：2026-10-23。分支 `codex/ui08-slave-parameters`；初始基线 `1b882ca`，实现 `75fc278`，同步 UI10 的 `ac39cb1` 与 UI12 的 `345b593`。验收修订为包含本文件的后续提交，避免在提交内部写入自身散列。

## 环境与执行

复用 Windows、Qt 6.12.0、LLVM-MinGW 22.1.7、CMake/Ninja 和 `qt-llvm-debug` / `qt-llvm-release` 预设；SDK12 指纹不变，宿主及 DLL 配套编译。每次构建 `--parallel 3`，未更改系统 PATH、执行策略或工具链。原生测试与其他 agent 串行，截图来自真实 Qt Widgets 页面。开发时另有 offscreen 定向检查。

两配置完整构建通过，日志见 `debug/full-build.log`、`release/full-build.log`；同步 UI12 后受影响目标及独立插件 fixture 再构建，见各目录 `final-build.log`。执行命令：

```powershell
cmake --build --preset qt-llvm-debug --parallel 3
cmake --build --preset qt-llvm-release --parallel 3
ctest --preset qt-llvm-debug -R 'slave_parameter_workflow|parameter_pending_write|parameter_file_edit_save_apply|parameter_group_polling|slave_edit_import_save|rtu_slave_json_read|rtu_slave_write|workspace_roundtrip|parallel_sessions_recovery' --output-on-failure
ctest --preset qt-llvm-release -R 'slave_parameter_workflow|parameter_pending_write|parameter_file_edit_save_apply|parameter_group_polling|slave_edit_import_save|rtu_slave_json_read|rtu_slave_write|workspace_roundtrip|parallel_sessions_recovery' --output-on-failure
ctest --preset qt-llvm-debug -R '^(slave_parameter_workflow|rtu_slave_json_read|rtu_slave_write|plugin_ui_contract)$' --output-on-failure
ctest --preset qt-llvm-release -R '^(slave_parameter_workflow|rtu_slave_json_read|rtu_slave_write|plugin_ui_contract)$' --output-on-failure
```

初轮两配置各 7/9：旧从站 UI 测试直接设置角色后立刻操作旧控件，没有等待新增排队角色切换，读套件超时、写套件一条失败。保留真实日志 `*-ctest.log` 与 `initial-slave*-results.txt`。三条旧 UI 用例迁移至公开 Workbench/session/ParameterView，不再绑定旧私有控件；原协议帧、数据及文件缺失断言保留。复验两配置 3/3，见 `*-retest-ctest.log`。随后同步 UI12，最终复验 Debug **4/4，21.95 秒**、Release **4/4，15.52 秒**，见 `*-final-ctest.log`。按父任务协调只重跑失败及受影响范围，之前六套已通过的回归结果仍保留。

| 套件 | Debug / Release Qt 检查数 | 证据 |
| --- | --- | --- |
| slave_parameter_workflow | 各 11 通过 | final-slave-parameter-results.txt |
| rtu_slave_json_read | 各 29 通过 | final-slave-results.txt |
| rtu_slave_write | 各 25 通过 | final-slave-write-results.txt |
| plugin_ui_contract | 各 9 通过 | final-plugin-ui-contract-results.txt |
| parameter_pending_write | 各 13 通过 | initial-parameter-write-results.txt |
| parameter_file_edit_save_apply | 各 15 通过 | initial-parameter-file-editor-results.txt |
| parameter_group_polling | 各 9 通过 | initial-parameter-poll-results.txt |
| slave_edit_import_save | 各 10 通过 | initial-edit-results.txt |
| workspace_roundtrip | 各 14 通过 | initial-workspace-results.txt |
| parallel_sessions_recovery | 各 20 通过 | initial-recovery-results.txt |

表中检查数包含 init/cleanup。workspace 有 Windows 临时插件目录清理 QWARN，功能断言全部通过；最终四套无 QWARN、无失败、无跳过。本切片不是全套最终产品验收，UI13 承担集成后完整 Debug/Release 回归。

## 行为与红绿证据

测试沿规格已确认的公开 DebugSession、Workbench/ParameterView、真实 Qt 编辑与可控传输/后台边界；文件使用真实临时外部 JSON，不 mock 自有业务模块。`red-role.txt`、`red-runtime.txt`、`red-file-page.txt`、`red-create.txt` 分别保留新页、运行表、文件页和新建设备入口缺失的实际失败。`red-partial-value.txt` 记录输入 65536 时逐键回调曾把 6553 提交为模拟值；修复为独立 `onCommit` 完整提交，保留主站 `onEdit`。`debug-slice.txt` 为开发期最终 offscreen 11/11 通过。

- 启动前可导航已配置设备；新建四区文件、保存、明确应用绑定后才可启动，不以内联空数据兜底。缺失文件仍不打开串口，其他会话独立。
- 单元格提交 42 后，实际 FC03 应答含 42；无效 65536 与 Escape 取消保持 42。停止后仍可编辑和保存。
- 运行值 42、文件草稿 777 独立；草稿保存不应用，取消应用保留 42，放弃旧运行值后明确应用才得到 777，其他设备 111 保持。
- 运行保存捕获 42，期间真实 FC06 写入 123；保存结果文件仍为 42，运行值 123 保持 dirty，草稿 777 保留。
- 打开后磁盘改为 999，应用拒绝与编辑页不同的文件；不覆盖原运行值。另一设备绑定路径不能被当前草稿覆盖。
- 超过 1 MiB 的合法文件及未知字段完整往返；从站明确使用 4 MiB 预算，256 项仅渲染分页，非容量缩水。
- 未保存角色切换取消保留原角色、原页、运行值；正常切换销毁旧守卫。提示中外部新写使旧决定失效；提示中页面销毁不崩溃。

## 目视、自检与边界

已打开并检查 [运行参数页](slave-runtime-parameters.png) 和 [文件编辑页](slave-file-editor.png)：白色背景，目标设备/启动文件/运行 dirty、文件来源和保存未应用说明可读；操作按钮、四区/偏移及表格完整，无文字截断。运行页截图是在拒绝 65536 后，表中仍显示正确值 42；文件页显示独立快照 777。

代码自检关注：宿主无 Modbus 领域分支；角色工厂在插件内；文件任务复用旧原子校验与保存修订保护；模态循环持 GUI lifetime 并检查 QPointer；设备/角色下拉框排队且有 revision；兼容页不覆盖新页守卫；主站活动编辑保护仍通过。自检补齐添加设备后守卫可能销毁页面的存活检查，最终四套再测。CMake 两次冲突均只在新增测试注册位置，完整保留 UI10/UI12 目标。`git diff --check` 通过。

这是可控传输和真实原生 Qt 验证，无真实串口、独立外部主站或用户硬件操作验收。运行值关闭提示选择保存后须等待后台完成，再次关闭；若新写仍 dirty 会继续提示。UI13 负责整体审查和兼容入口最终收束；旧13待硬件条件，旧14–19未启动。所有证据散列见 `SHA256SUMS.txt`，构建产物未提交。
