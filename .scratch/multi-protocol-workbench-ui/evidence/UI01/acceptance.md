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
- 既有产品代码未改，执行一次完整 Debug CTest 回归，11/11、238 项通过，0 失败/跳过，见 [日志](product-regression.log)与 [原始 QtTest 结果](product-regression/)。这不代表新产品界面已实施，本轮未重复旧产品 Release 全量验证。

## 审查后的必要修复

Standards 与 Spec 两轴独立只读审查综合发布与原型。Standards 初审无确定问题；Spec 发现一项 P1：新建空从站会话点击“保存运行值到文件”会访问空设备数组。现先校验目标索引，无设备时提示“请先添加设备，再保存模拟运行值”，不进入文件对话框。截图模式新增实际按钮点击路径并保存提示，随后继续完成全部截图和 QQuickWidget Ready，进程正常退出。该检查是原型启动冒烟，不代替用户完整操作验收。

另将 UI07/UI13 的 Blocked by 收敛为草案的直接前置，去除重复列出的传递依赖；依赖图和执行顺序不变。

两轴已独立复核 3cf8ba8，Standards 剩余0、Spec 原P1修复后剩余0，详见 [审查记录](review.md)。修复后通过本机 CMD 入口重新启动，实际运行窗口标题为“多协议调试工作台 · 界面原型（模拟数据）”；启动本身不勾选用户确认。当前版本的程序、原型源文件和本目录证据散列见 SHA256SUMS.txt；构建产物本身不纳入 Git。

## 可操作入口和建议流程

双击原型目录中的 **打开界面原型.cmd**，窗口标题为“多协议调试工作台 · 界面原型（模拟数据）”。详细操作见 [原型说明](../../../multi-protocol-debugger/ui-prototype/README.md)。

建议依次比较左右/上下平铺及浮动，操作主站读取→双击编辑→轮询保留黄色待提交→行内设置，体验文件保存后明确应用、从站本地修改和独立保存，选择多个参数打开 QML 曲线并显式轮询。最后关闭一个窗口并通过左侧重新打开，观察另一会话及记录归属。

| 截图 | 用途 |
| --- | --- |
| [01-workbench.png](01-workbench.png) | 并排主从窗口、单值列、黄色待提交和各自记录 |
| [02-file-editor.png](02-file-editor.png) | 目标设备、路径及保存/应用分开的文件页 |
| [03-qml-chart.png](03-qml-chart.png) | 实际 QQuickWidget/QML 图例、暂停、清空和缩放入口 |
| [04-floating-session.png](04-floating-session.png) | 浮窗中的参数和所属记录一起移动 |
| [05-empty-slave-save.png](05-empty-slave-save.png) | 空从站点击保存时显示先添加设备，原型继续运行 |

## 用户验收

- [ ] 用户完成实际操作并反馈可读性、信息密度、窗口组织与操作目标。
- [ ] 用户确认接受项和待改项，明确批准进入 UI02。

当前没有用户对本轮原型的接受反馈。上述未勾选项保留，不能将发布任务、编译通过、截图成功或代码审查通过当作确认。
