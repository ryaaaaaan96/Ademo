# UI10 验收证据

## 范围与环境

仅实施 UI10，基于 UI06 已合入的 `6b8cabe`，独立分支 `codex/ui10-qml-trends`。复用 Qt 6.12.0 / LLVM-MinGW 22.1.7 / CMake 3.30+ 的现有 presets，构建 `--parallel 3`。未安装工具链，未接真实串口；可控 Endpoint 和时钟之上的 DebugSession/RTU/ParameterView/QQuickWidget 均为真实实现。

沿用用户已批准的测试 seam：真实会话公开入口、可控传输/时钟、公开 Qt 控件操作及可见文本/Ready/framebuffer。未通过私有容器或复制算法验证曲线。

## 逐行为 red → green

| 行为 | 实际红灯 | 转绿证据 |
| --- | --- | --- |
| 参数入口打开真实 QML、固定设备身份、无自动请求 | `01-red.txt` 缺少绘图入口 | `01-green.txt` |
| 暂停时轮询继续、恢复/图例/缩放/清空 | `02-red.txt` 缺少暂停入口 | `02-green.txt` |
| 清空不会重现刷新周期前已收到的样本 | `03-red.txt` 重现 555 V | `03-green.txt` |
| 超出 32 曲线明确拒绝，不静默截断 | `04-red.txt` 33 项仍打开窗口 | `04-green.txt` |

后续真实会话验证覆盖异常、无归属响应、超时后 `freshnessUncertain` 候选值过滤，8 窗口容量回收，holding/input 相同地址身份隔离，QQuickWidget 跨浮动/停靠保留实例及所属视图销毁时关闭。

## SDK11 阶段探测（不是最终配套验收）

命令：`cmake --build --preset qt-llvm-debug|qt-llvm-release --target parameter_trend_tests parameter_poll_tests parameter_window_tests --parallel 3`。运行时仅在该进程加入现有 Qt/LLVM bin 的 PATH；`QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software`。对应 exe 使用 QtTest `-o <本目录文件>,txt`。

- `05-offscreen-debug.txt`：最初 6 曲线负载版本 9/9 PASS。
- `06-offscreen-release.txt`：扩展到 32 曲线容量上限，10/10 PASS；2200 次真实 RTU 读取全部 success，每条最终 2048 点，负载段 30345 ms。
- `07-offscreen-debug-capacity.txt`：增加同地址不同数据区隔离后 11/11 PASS。
- `08-offscreen-debug-poll.txt`、`09-offscreen-debug-window.txt`：相关参数轮询与窗口读取回归通过。

以上离屏警告包括平台不支持 raise/propagateSizeHints 和 Qt 字体目录缺省；不将其视作 Windows 原生窗口视觉验收。最终 SDK12 同步、Windows 原生图形后端、截图与 Debug/Release 复验待追加，任务暂不标记完成。
