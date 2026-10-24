# 2026-10-24：参数优先的侧栏与主窗口统一关闭

## 交付行为

Modbus 主站将会话启动／停止、连接配置、设备／分组、参数配置、读取、绘图与轮询集中于可调宽左侧栏。侧边箭头可收起整栏，为参数表释放宽度；右侧始终显示目标设备与分组，文件编辑保留独立页。通信记录继续归所属会话窗口。

主工作台通过现有各会话未保存修改确认及最终重查后，关闭自己拥有的配置、重试、功能码和 QML 趋势窗口，再停止会话、隐藏会话浮窗。取消确认则保留未保存内容、通信和派生窗口。不调用全局关闭函数，不关闭无关工作台。插件窗口按 QWidget 父子关系归属工作台。

为了避免宿主识别协议按钮，SDK13 增加可选 `ParameterView::sessionControlsHost()` 插槽；未提供的插件继续用原入口。宿主在重建／销毁插件视图前收回宿主控制组件，确保停止事件仍能安全刷新。IID、构建指纹、产品插件及独立测试插件均配套重编。

## 验证与限制

Qt 6.12.0 LLVM-MinGW，Debug/Release 完整默认目标构建成功；`git diff --check` 通过。正式 GUI 尚需用户重新启动后确认，没有将自动测试等同人工验收。

- 新增真实 Qt 验收覆盖：侧栏包含宿主连接控制、收起后表格变宽且保持可见、展开恢复操作；运行中的主站与多个派生窗口共存时，取消退出保留草稿及通信，明确放弃退出关闭全部所属窗口并释放端点，独立无关 QWidget 保持可见。
- Release 的界面套件12项全部通过（含真实 QML）。完整 CTest 20/22套通过，参数读写／轮询／文件／插件／生命周期回归通过；没有声称全量全绿。
- `workspace_window_layout` 仍有上一轮已记录的屏幕外窗口左上角恢复失败，见[独立问题](../../../../window-screen-recovery/issues/01-offscreen-frame-restoration.md)。
- `parallel_sessions_recovery` 的两项原生拖动检查失败。最小复现临时记录原生命中窗口，发现落点被外部 Visual Studio Code 遮挡；既有遮挡保护拒绝停靠。诊断输出见 [native-diagnosis.txt](native-diagnosis.txt)，临时诊断代码已删除并重新构建；未绕过遮挡保护或修改原测试断言。
- Debug 首轮界面测试点击读取时尚未等待按钮刷新为可用，已改为等待公开可用状态后再点击，保持真实值与通信断言；最终定向复验见 [debug-ui-recheck.log](debug-ui-recheck.log)。初始失败记录保留。

记录：[Debug 首轮](debug-ctest.log)、[Release 全套](release-ctest.log)、[Release 界面逐项](release-ui-results.txt)、[原生停靠复验](release-native-recheck.txt)。

截图：[展开侧栏](formal-floating-master.png)、[收起侧栏后以参数为主体](parameters-focused.png)。均为真实 Qt 界面测试截图，通信使用受控对端。

旧用户实例保持运行，旧 EXE/DLL 备份在忽略目录 `build/qt-llvm-release/live-instance-backup-sidebar-20261024`，新版已在标准 Release 路径完成配套构建；需重新启动加载 SDK13。未执行 Git 提交，旧13硬件验收及后续协议阶段未启动。
