# UI13 窗口恢复与软件收尾验收

日期：2026-10-24。集成基线 `87126a3`，实施分支 `codex/ui13-window-recovery`；复用 Qt 6.12.0 / LLVM-MinGW Clang 22.1.7、Windows 11、现有 Debug/Release 预设。

## 问题、修复与回归入口

修改前的公开工作区行为测试失败，见 [原生边框诊断](frame-restoration-red.txt)：当前 DPR=2，可用桌面为 `(0,0,1440,852)`；Qt 恢复后的客户区为 `(1,17,640,550)`，原生外框却为 `(1,-12,640,579)`。客户区可见并不足以保证标题栏在可用桌面内。

恢复保存的浮动几何后，在原生窗口实际可见时用 `frameGeometry()` 约束位置；隐藏窗口延至首次显示处理。保留正常屏幕内位置与尺寸，不调整最大化/全屏窗口，不改变新建浮动窗口居中和停靠行为。公开工作区回归覆盖打开到已显示工作台、先恢复后显示工作台、关闭状态恢复后再打开三个路径；原有正常布局往返测试保持。

首次完整 Debug 为 21/22，见 [初次完整结果](debug-ctest.log)。剩余原生拖动失败已定位为外部 VS Code 窗口遮挡目标，见 [诊断日志](drag-occlusion-diagnostic.txt)。只在原生拖动测试中将测试所属窗口置顶；遮挡反例同时验证鼠标命中测试创建的遮挡窗。生产代码仍拒绝向被其他应用遮住的工作台停靠。临时诊断代码已移除。

## 构建与完整回归

使用仓库现有 `qt-llvm-debug`、`qt-llvm-release` 配置，全目标构建；CTest 串行运行于原生 Windows 用户环境，避免沙箱临时文件和窗口权限限制。

| 配置 | 配置/全目标构建 | 完整 CTest | Qt Test |
| --- | --- | --- | --- |
| Debug | [配置](debug-configure.log)、[构建](debug-build.log)成功 | [22/22通过](debug-ctest-final.log)，206.76秒 | 361通过，0失败/跳过 |
| Release | [配置](release-configure.log)、[构建](release-build.log)成功 | [22/22通过](release-ctest-final.log)，118.27秒 | 361通过，0失败/跳过 |

逐套原始输出保存在 `debug/`、`release/`，其中窗口布局各15项、会话恢复各21项全部通过。Debug全套期间并行构建Release，QML最大曲线用例耗时111.40秒；独立Release运行相同套件41.33秒，均在既有超时内通过，没有更改测试容量或阈值。源码及关键可执行文件散列见 [SHA256](sha256.txt)。复验命令为现有 `cmake --preset qt-llvm-debug`、`cmake --build --preset qt-llvm-debug`、`ctest --preset qt-llvm-debug --parallel 1`，Release仅替换预设名；工具使用 `C:\Qt\Tools\CMake_64\bin`。

## 审查与边界

保留已有 UI13 实现及用户 2026-10-24 对正式界面的确认。`87126a3` 是已审查的未提交实现与已批准 CAN 范围归档，不将该检查点冒充此次修复完成提交。主代理已只读审查本次五个代码/测试文件增量，无阻塞问题；最终 Standards / Spec 独立整体审查随 14A 集成收尾继续执行。

本轮软件测试不替代旧13的真实 RTU 互通、时序或负载验收，也不代表 CAN 物理验收。14A 在 UI13 软件收尾后单独实施；14B、旧15、TCP 未启动。
