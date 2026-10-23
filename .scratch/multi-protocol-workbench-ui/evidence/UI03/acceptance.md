# UI03：多窗口生命周期验收

日期：2026-10-23。任务：[UI03](../../issues/03-dockable-session-lifecycle.md)。工作树 `build/worktrees/ui03`，分支 `codex/ui03-session-windows`，起点 `eaba0d9`，完成前同步集成基线 `5f3c8a8`。本记录、源码和测试随同一任务提交。

## 实施结果

- 工作台与独立会话窗口均采用 QMainWindow；独立窗口由系统提供最小化、最大化／还原和关闭。SessionDock 只承载停靠，浮动和重新停靠复用同一会话内容及 DebugSession。
- 左侧会话→设备导航使用插件公开设备身份，点击定位对应窗口，最小化窗口可恢复。默认／新建独立浮动，点击浮动以工作台中心定位并限制在当前屏幕可用区域；批量浮动在中心轻微错开。
- 支持左右／上下平铺，以及 Windows 原生标题移动的悬停预览与松开落位：已有会话按左、右、上、下分割，空工作台可直接停靠。取消、拖出、主窗不可见／最小化、模态对话框或遮挡时不落位。输入透明的预览不抢夺鼠标。
- 关闭只停止对应会话并释放端点，保留配置与原有记录；导航重开不自动通信。最小化、最大化、平铺、移动、页面切换、设备选择和记录折叠不停止会话。已关闭会话不会被批量平铺悄悄重开。
- 保留既有从站未保存与忙碌保护，以及多会话逐项确认后的新修改复查。关闭取消时保留窗口和运行；守卫期间销毁工作台不继续访问已释放对象。
- WorkspaceController 通过 `sessionView/sessionTitle` 访问内容，替换时清理整组会话；旧私有 `tabs_` 容器已移除。原未知插件配置、外部文件引用和全部停止恢复回归继续通过。窗口布局持久化留给 UI11。

## TDD 与验证入口

采用规格已确认的公开会话接口、可控传输／时钟及实际 Qt 界面操作。没有引入测试专用隐藏标签页或以像素快照代替通信行为。

1. [关闭保留 red](red-close-retains.txt)：原实现关闭后删除配置，两个会话变为一个；修正后[同一测试 green](green-close-retains.txt)，另一连接继续运行，重开不再次打开端点。
2. [平铺 red](red-window-arrangement.txt)：原界面缺少同时展示入口；接入原生窗口与停靠容器后[green](green-window-arrangement.txt)。最终回归进一步覆盖原生三按钮标志、居中、最小化／最大化、原生关闭和停止后重开。
3. [原生停靠 red](red-native-dock.txt)：向实际 HWND 发送移动阶段事件后仍保持浮动；接入原生移动事件后[green](green-native-dock.txt)。最终回归覆盖空工作台、四个相对方向、取消及另一原生窗口遮挡。
4. [记录折叠 red](red-record-fold.txt)：缺少所属窗口折叠入口；补齐后完整回归验证两个会话的不同原始字节不串用，设备树选择定位、最小化恢复与折叠均不改变通信。
5. 首次完整 Debug 中一个旧从站测试仍按“关闭即删除”使用固定索引，造成操作提交到保留的原会话，见 [初次日志](debug-initial-ctest.log)。该测试已改用 `addSession` 返回索引；修正后完整 Debug／Release 均通过。协议实现未为迁就测试修改。

## 最终结果

| 配置 | CTest | QtTest | 记录 |
| --- | --- | --- | --- |
| Debug | 12/12 通过 | 247 通过，0 失败／跳过 | [完整日志](debug-ctest.log)、`debug/*results.txt` |
| Release | 12/12 通过 | 247 通过，0 失败／跳过 | [完整日志](release-ctest.log)、`release/*results.txt` |

环境复用 Windows x64、Qt 6.12.0、LLVM-MinGW Clang 22.1.7、C++17、CMake 3.30.5 和 Ninja；未安装新工具或修改全局 PATH。配置／构建使用 `qt-llvm-debug`、`qt-llvm-release` 预设。Windows 原生遮挡检查链接系统 dwmapi。

复现：`C:/Qt/Tools/CMake_64/bin/cmake.exe --preset qt-llvm-debug`，随后 `cmake.exe --build --preset qt-llvm-debug --parallel 3`、`ctest.exe --preset qt-llvm-debug --output-on-failure`；Release 替换预设。窗口定向选择 `-R parallel_sessions_recovery`。可见 UI 测试应串行执行，原生移动测试短暂设置并恢复光标，避免与其他桌面操作或可见测试抢占 Z 序。

## UI04 集成后的交叉验证

功能提交 `b26a926` 后，将集成 `1032460`（包含 UI04 验证提交 `4e4aa20`）合并为 `da9c783`。README/CMake 双方增量均保留，SDK10、参数文件后台加载与原生会话窗口无冲突；Qt/LLVM-MinGW 预设不变。两配置重新构建全部目标成功，见 `post-merge-*-configure.log`、`post-merge-*-build.log`。

在独占桌面时段串行执行 `parameter_definition_read|parameter_window_read|parallel_sessions_recovery|slave_edit_import_save|workspace_roundtrip`：Debug 5/5、54 项通过（27.70 秒），Release 5/5、54 项通过（22.52 秒），均无失败／跳过。日志为 [Debug](post-merge-debug-ctest.log)、[Release](post-merge-release-ctest.log)，原始结果为 `post-merge-*-results.txt`。覆盖异步参数定义加载与新表格首次读取、窗口导航／四向拖动／遮挡取消、工作区文件引用往返，以及原有未保存和忙碌关闭保护；无需兼容改码。前表的 12/12 完整回归对应 UI04 合并前的本项功能提交，本节为 SDK10 合并后的相关复核，不把定向结果写成完整回归。

## 视觉与审查边界

已目视检查 [双会话平铺](two-session-workbench.png)、[浮窗中的所属记录](floating-session-records.png)及[关闭确认期间新数据保留](close-confirmation-new-data-retained.png)。截图由真实 Qt `QWidget::grab` 取得客户区，不包含系统标题栏，不据此宣称原生标题按钮外观已由用户接受。窄窗口允许滚动，后续 UI04–UI13 继续完成参数文件、写入、文件页、曲线及完整布局。

原生自动检查通过 HWND 的移动消息驱动实际窗口事件路径，覆盖目标判断和真实 QWidget 重挂载；不是人工按住标题拖动／Esc 的体验验收。多显示器、不同缩放比例与用户正式产品验收尚待 UI13 实际桌面检查；本项不声称真实串口或硬件联调。

自检：协议字段仍在插件；视图与会话随窗口移动保持对象身份；插件视图先于会话/DLL 租约销毁；取消关闭和窗口在守卫期间销毁的回归通过；工作区未知字段与外部文件引用回归通过；`git diff --check` 通过。最终独立双轴审查由集成阶段执行。

## 后续交接

- UI09 可通过 `Workbench::sessionView(index)` 扩展所属会话的基本记录；完整 TracePanel/RecordingPanel 目前仍按 UI02 兼容路径随同一内容移动。
- UI11 使用 `sessionWindow/isSessionOpen/isSessionFloating/sessionTitle` 和 `floatSession/dockSession/tileSessions/selectSession`；`sessionCount` 包含关闭保留的配置，`closeSession` 不删除。窗口布局持久化尚未在本项实施。
- UI05/07/08 需把待提交值、文件草稿等新增状态接入未保存保护，不能只覆盖现有从站守卫；UI03 只验证原有保护。
- UI10 的绘图窗口应固定由工作台持有原生归属，并由所属会话管理数据和销毁，避免随着参数窗反复 reparent 导致 QQuickWidget 渲染上下文重建。

SHA256SUMS.txt 对文本使用 Git 提交形式（UTF-8、LF）计算，对 PNG 使用文件原始字节。Windows checkout 的 CRLF 转换不代表提交内容损坏；验收对象是同次提交源码与证据，构建产物保持忽略。
