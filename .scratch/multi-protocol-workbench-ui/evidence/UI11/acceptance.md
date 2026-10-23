# UI11 工作区布局验收

基线628ba9b；独立分支codex/ui11-workspace-layout。实现a687834，同步集成调度db614b9后为0e1480c。复用Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、CMake3.30.5、C++17和SDK12，宿主/插件配套构建，未修改全局环境。

## 行为与TDD记录

沿用户确认的Workbench/DebugSession公开接口、可控传输和实际Qt操作，不接真实串口。

| 验证 | 证据 |
| --- | --- |
| 混合布局往返 | [red](red-mixed-layout.txt)全恢复为浮动；[green](green-mixed-layout.txt)两个平铺窗、指定几何浮窗、关闭配置及设备18选择均恢复，首次show不覆盖位置，0端口打开。 |
| 恢复布局入口 | [red](red-layout-action.txt)缺入口；[green](green-layout-action.txt)重排时保留当前会话对象、运行状态和开闭状态，不按存档closed停止已运行会话。 |
| 无效布局原子拒绝 | [red](red-invalid-layout.txt)三种无效布局错误success并清空；[green](green-invalid-layout.txt)Base64、Qt停靠状态与几何均file-error，当前运行会话保持。 |
| 工作区选择器竞争 | [red](red-workspace-picker.txt)后台替换成功；[green](green-workspace-picker.txt)模态阶段取消替换并保留当前会话。 |
| 三类修改取消 | [green](green-three-modification-guards.txt)：主站pending、文件草稿、从站运行值的真实Qt提示取消，原值/dirty/会话保留；复用已有正确守卫。 |
| 后续提示期间新数据 | [green](green-late-runtime-modification.txt)：前窗已明确放弃后，在后窗文件提示期间新增999，提交前复查取消替换，999和运行状态保留。 |
| 父窗口销毁 | [green](green-picker-owner-lifetime.txt)：真实文件选择器内同步删除Workbench，安全返回。 |
| 配置和外部引用 | [green](green-reference-polling-boundary.txt)：分组/参数文件引用/轮询往返，slave路径相对保存绝对恢复；外部文件删除后该从站失败、另一主站可启动，无内嵌兜底。 |
| 离桌面几何 | [green](green-offscreen-geometry-fallback.txt)：保存远离当前桌面的坐标，Qt恢复到可见屏幕。 |

version仍1，新字段可选，旧工作区默认行为和未知字段保留；曲线窗口布局不持久化。[字段与使用说明](../../../../docs/workspace-layout.md)。

## 最终相关回归

| 配置 | CTest | QtTest | 日志 |
| --- | --- | --- | --- |
| Debug | 6/6，35.37秒 | 88通过，0失败/跳过 | [构建](debug-build.log)、[CTest](debug-ctest.log)、debug/原始结果 |
| Release | 6/6，29.79秒 | 88通过，0失败/跳过 | [配置](release-configure.log)、[构建](release-build.log)、[CTest](release-ctest.log)、release/原始结果 |

范围：workspace_window_layout 15、workspace_roundtrip 14、parallel_sessions_recovery 20、parameter_file_edit_save_apply 15、slave_parameter_workflow 11、parameter_pending_write 13；QtTest计数含init/cleanup。旧工作区套件保留未知插件/字段、插件修复后的引用恢复、异步限额及无效文档保护。UI13负责最终全套，本项不重复未改协议后端。

复现：以--parallel 3构建上述六个tests目标后，运行 `ctest --preset qt-llvm-debug -R '^(workspace_window_layout|workspace_roundtrip|parallel_sessions_recovery|parameter_file_edit_save_apply|slave_parameter_workflow|parameter_pending_write)$' --verbose`，Release替换预设名。原生测试串行且无其他agent竞争桌面；尺寸验证不使用offscreen虚拟小屏。

## 目视、自检与边界

已打开并检查[混合布局](workspace-restored-layout.png)和[浮窗客户区](workspace-restored-floating.png)：左右停靠、关闭配置导航、浮窗设备18和白色记录归属正确。650×550浮窗仍需滚动，兼容入口及记录区挤占留UI13迁移/密度调整；不宣称最终用户视觉确认。截图不含原生标题栏，几何和开闭由真实QMainWindow行为核验。

自检确认宿主无协议字段分支，Qt布局先在临时窗口验证，设备选择保留目录租约和QPointer，选择器使用堆对象/busy深度/销毁检查；`git diff --check`通过。构建产物不入Git，文本散列按UTF-8/LF归一化，图片按原字节，见SHA256SUMS.txt。

未覆盖多物理屏幕插拔或真实硬件通信；离桌面坐标fallback不替代这些实测。正式用户验收仍属于UI13，旧13待硬件，旧14–19未启动。
