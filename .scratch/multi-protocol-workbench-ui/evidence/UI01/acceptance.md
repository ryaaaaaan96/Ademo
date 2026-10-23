# UI01 原型准备与待确认记录

日期：2026-10-23，Asia/Shanghai。任务：[UI01：确认可操作原型的布局与关键流程](../../issues/01-prototype-layout-and-flow.md)。**当前为原型准备，用户视觉与交互验收未完成，UI02 未启动。**

## 范围与来源

复用 `.scratch/multi-protocol-debugger/ui-prototype/`，没有重做原型或接入产品 SDK/通道。`dfe77be` 已提交综合任务、原有规格及原型基线；旧01–12的产品验收继续有效，旧13–19保留原编号及外部条件。

本轮修正为工具栏浅字白底、字体可读性、按屏幕可用区域选择初始窗口尺寸、窄窗口可滚动、通信记录折叠标签、端点修改后的摘要刷新，以及明确等待截图进程退出。新增 CMD 本机启动入口不要求运行 PowerShell 脚本；它仍使用本机已安装 Qt，并非独立便携交付。

## 实际验证

- 复用 Qt 6.12.0、LLVM-MinGW Clang 22.1.7、C++17、CMake 3.30.5 与 Ninja；原型 Release 配置、编译成功。
- 执行原型 run.ps1 -NativeCapture，使用真实 Windows Qt 平台及默认 Qt Quick 渲染后端，明确等待进程退出且 ExitCode 为 0。日志见 [native-capture.log](native-capture.log)。
- [capture-result.txt](capture-result.txt) 为 QQuickWidget Ready；四张截图从本次运行的 runtime/captures 复制，已目视检查工具栏对比度、中文、文件目标和主从窗口布局。
- 截图模式会自动放入模拟读取、待提交值、文件页和曲线演示状态。这不是完整用户操作断言，不能据此宣称保存/应用、窗口交互或全部验收已由用户接受。
- 仅模拟数据，不打开真实端口，不把演示协议称作 UPP；原型临时格式和容量限制不迁移进正式产品。

## 可操作入口和建议流程

双击原型目录中的 **打开界面原型.cmd**，窗口标题为“多协议调试工作台 · 界面原型（模拟数据）”。详细操作见 [原型说明](../../../multi-protocol-debugger/ui-prototype/README.md)。

建议依次比较左右/上下平铺及浮动，操作主站读取→双击编辑→轮询保留黄色待提交→行内设置，体验文件保存后明确应用、从站本地修改和独立保存，选择多个参数打开 QML 曲线并显式轮询。最后关闭一个窗口并通过左侧重新打开，观察另一会话及记录归属。

| 截图 | 用途 |
| --- | --- |
| [01-workbench.png](01-workbench.png) | 并排主从窗口、单值列、黄色待提交和各自记录 |
| [02-file-editor.png](02-file-editor.png) | 目标设备、路径及保存/应用分开的文件页 |
| [03-qml-chart.png](03-qml-chart.png) | 实际 QQuickWidget/QML 图例、暂停、清空和缩放入口 |
| [04-floating-session.png](04-floating-session.png) | 浮窗中的参数和所属记录一起移动 |

## 用户验收

- [ ] 用户完成实际操作并反馈可读性、信息密度、窗口组织与操作目标。
- [ ] 用户确认接受项和待改项，明确批准进入 UI02。

当前没有用户对本轮原型的接受反馈。上述未勾选项保留，不能将发布任务、编译通过、截图成功或代码审查通过当作确认。
