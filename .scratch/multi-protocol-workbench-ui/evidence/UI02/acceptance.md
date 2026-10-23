# UI02：正式参数窗口首读验收

日期：2026-10-23。实施分支 `codex/ui02-window-read`，起点 `3de6b5d`，提交前已合并集成分支 `f418da5`。任务为 [UI02](../../issues/02-window-first-rtu-read.md)，同次 Git 提交包含源码、测试与本记录。

## 已完成行为

- 框架新增通用 `ParameterTable`，插件提供列、稳定参数 ID、单元格与动作。Modbus 插件的参数页接入现有 `SessionAccess.submit/messages/operation`，宿主没有 Modbus 组帧或数据区分支。
- 停止时添加设备地址、数据区及单个 PDU 地址，启动后点击行内读取。每行单独显示最近成功观测与更新时间；本地发送、异常或不确定的新鲜度消息不冒充设备数据。
- 独立“连接配置…”入口承载协议、角色、端点和重连配置；窗口保留启停和简明状态。新参数页、白底本会话记录和原有回溯、磁盘记录入口共享同一会话。
- “兼容操作页”继续提供已验收的主站读写／轮询及从站文件操作；切页不销毁参数内容。公开设备选择后返回原设备，最近观测仍在且不自动发送。
- SDK 从 8 升为 9，IID 和构建指纹同时更新；旧 DLL 必须配套重编。可选 `createParameterView` 默认返回空，旧插件源代码可沿用旧视图。两种视图均由目录注册表保持 DLL 生命周期。

## 实际验证与 TDD

预先批准的测试入口为规格 Testing Decisions：公开会话接口与可控传输／时钟，加真实 Qt 界面公共操作。本项没有增加私有对象名或像素断言。

1. 首先加入实际 Qt 参数行读取流程，未实施时明确失败在缺少“添加参数”入口，见 [red](red-parameter-read.txt)。
2. 补齐框架表格、插件参数视图和宿主挂载后，通过实际控件启动与行内读取，观察真实协议 DLL 发出 `11 03 00 6b 00 01 f7 46`；控制传输返回 `11 03 02 02 2b 38 f8`，表格显示 555、更新时间和所属 RX／本地发送记录。停止后端点关闭，读取按钮禁用。
3. 同一流程扩展核验兼容页切换、公开设备选择及返回后结果保留，发送数仍为 1。测试在页面切换后等待 Qt 完成控件生命周期，并只选择当前可见按钮，避免误操作待删除的旧控件。
4. 完整 Debug 和 Release 均为 **12/12 CTest，241 项 QtTest 通过，0 失败／跳过**。最后的参数区置顶及设备选择验证分别进行 Debug／Release 定向复测，均 1/1 通过。原 11 套产品回归保留通过。

完整日志：[Debug](debug-ctest.log)、[Release](release-ctest.log)；最终定向：[Debug](debug-final-targeted.log)、[Release](release-final-targeted.log)。原始 QtTest 输出存放在 `debug/` 与 `release/`。

环境继续为 Windows x64、Qt 6.12.0、LLVM-MinGW Clang 22.1.7、C++17、CMake 3.30.5、Ninja；使用仓库 `qt-llvm-debug` 和 `qt-llvm-release` 预设，没有安装或修改全局工具配置。

复现：使用 `C:/Qt/Tools/CMake_64/bin/cmake.exe --preset qt-llvm-debug`（Release 替换预设）、`cmake.exe --build --preset qt-llvm-debug`、`ctest.exe --preset qt-llvm-debug --output-on-failure`。单项测试使用 `-R parameter_window_read`。设置 `DEBUGGER_EVIDENCE_DIR` 为输出目录可保存参数页截图。

## 视觉与审查边界

[正式参数页截图](parameter-first-read.png) 来自实际 Windows Qt Widgets 的可控读取流程，已目视检查参数值、时间、状态及白底所属记录。它是程序驱动的软件证据，不是用户正式产品验收，也不代表真实串口互通。

自检确认：协议字段留在 Modbus 插件，表格只承载通用数据与动作；行操作捕获稳定 ID；会话与 DLL 生命周期延续既有租约；配置变更通过原会话保护；原工作区与从站未保存回归通过；`git diff --check` 无问题。整体独立审查由集成阶段执行，本记录不冒称已完成最终双轴审查。

## 交接

- UI03 使用 `Workbench::parameterView(index)` 和 `ParameterView::devices/selectedDevice/selectDevice` 接设备导航；宿主改动集中在 `src/workbench.cpp`。
- UI04 继续 `plugins/master_parameters.cpp` 的正式文件／分组与地址范围流程。当前 `parameterRows` 只是会话配置中的 `unit/area/address` 地址行，不是外部 JSON 参数文件格式。
- UI03 后的原生多窗口管理、UI05 行内写入、UI07 文件页、UI09 完整逐窗回溯等均未在本切片提前实现；从站暂用兼容页。旧13硬件及旧14–19未启动。
