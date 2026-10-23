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

以上离屏警告包括平台不支持 raise/propagateSizeHints 和 Qt 字体目录缺省；不将其视作 Windows 原生窗口视觉验收。该阶段尚待 SDK12 与原生配套复验；最终结论见下文。

## SDK11 Windows 原生阶段

经集成协调分配独占桌面时段，退出 `QT_QUICK_BACKEND=software` 覆盖，设置 `QT_QPA_PLATFORM=windows`、`QSG_INFO=1`，并将 `DEBUGGER_EVIDENCE_DIR` 分别设为 windows-debug/windows-release。仅运行 UI10 曲线测试，没有并发运行旧恢复/Z-order 测试。

- `windows-debug/trends.txt`：11/11 PASS，容量段 29074 ms，总时长 38366 ms。
- `windows-release/trends.txt`：11/11 PASS，总时长 33645 ms。
- 两者均使用日志中实际选中的 Intel Iris Xe Graphics 适配器；无 QWARN、QML 错误或交换链重建错误。QQuickWidget Ready、framebuffer 非空及跨浮动/停靠保持实例均通过。
- 目视检查两参数与 32 参数客户区截图：白底、真实 Y 值与相对秒数、单位/身份、按钮及曲线可读。32 条长图例暴露概要默认垂直居中使顶部不可见，已改为顶部对齐，最终配套复验更新截图。截图不含 Windows 非客户区；系统三按钮标志由实际 QMainWindow 测试验证。

以上为 SDK11 配套阶段，不冒充用户或真实串口验收。

## SDK12 最终配套验收

UI10 实现 `8776b22`，原生阶段证据 `32cd795`，合入已验收 SDK12 `1b882ca` 的提交为 `5125d93`。冲突只涉及 CMake 组件/测试列表与 MasterParameters 私有区；保留 UI07 文件双页、queued selection、revision 及守卫逻辑，参数页相对集成只增加 13 行绘图入口/窗口列表。随后同步 `1a96a38`，仅改变任务索引，产品代码与已测版本相同。

同步销毁回调先复制 `access.lifetime`，再销毁图形及其适配器/计时器闭包。新增公开工作台/插件目录回归，实际释放最后租约后重新发现配套插件，未依赖内部容器。

配套宿主、Modbus/Serial DLL 和四项测试均用既有 presets、`--parallel 3` 重新编译；SDK/IID/build key 为 12。最终构建检查日志及二进制 SHA256 分别位于 sdk12-debug/sdk12-release；构建产物本身不入 Git。

复验命令（两配置分别执行）：

```powershell
cmake --build --preset qt-llvm-debug --target multi_protocol_debugger parameter_trend_tests parameter_poll_tests parameter_window_tests parameter_file_editor_tests --parallel 3
ctest --preset qt-llvm-debug -R '^(parameter_qml_trends|parameter_group_polling|parameter_window_read|parameter_file_edit_save_apply)$' --output-on-failure
```

Release 将 preset 替换为 `qt-llvm-release`。运行环境：当前进程 PATH 加既有 Qt/LLVM bin；`QT_QPA_PLATFORM=windows`，移除 `QT_QUICK_BACKEND` 覆盖，`QSG_INFO=1`，`DEBUGGER_EVIDENCE_DIR` 指向对应 sdk12 目录。构建与测试分开；Windows 回归在独占时段串行进行，未并发旧 Z-order 测试。

| 最终结果 | Debug | Release |
| --- | --- | --- |
| CTest 定向回归 | 4/4 PASS，51.16 秒 | 4/4 PASS，49.21 秒 |
| 曲线 QtTest | 12/12 PASS | 12/12 PASS |
| 文件编辑 / 组轮询 / 窗口读取 | 15/15、9/9、3/3 PASS | 15/15、9/9、3/3 PASS |
| 32 曲线容量负载段 | 25437 ms | 29064 ms |
| 真实 RTU 完成 / 每曲线最终保留 | 2200 次 success / 2048 点 | 2200 次 success / 2048 点 |

原始结果：[Debug](sdk12-debug/ctest.log)、[Release](sdk12-release/ctest.log) 及各目录四份 `*-results.txt`。QQuickWidget Ready、framebuffer 非空、固定工作台 owner、浮动/停靠保留同实例、所属视图销毁以及最后 catalog 租约释放/重载均通过。原生图形日志实际选择 Intel Iris Xe Graphics，无曲线 QWARN/QML/交换链错误。

已目视检查最终 [两参数窗口](sdk12-release/qml-device-17.png) 和 [容量窗口](sdk12-debug/qml-capacity.png)：概要顶部对齐有效，白底、单位/身份、真实坐标与控制按钮可读。截图是客户区，原生三按钮由 QMainWindow flags 测试验证；不将客户区截图称作非客户区像素证明。

## 自检与边界

`git diff --check` 通过。自检确认数值来源过滤、不可变身份、清空游标、窗口/样本容量、显示和通信独立、稳定原生 owner 及 SDK12 DLL 返回栈保护；整体独立 Standards/Spec 审查交由集成阶段。`manifest.sha256` 对本目录证据逐项固定：文本按 UTF-8/LF 归一化，PNG 按原始字节；其自身不参与散列。

上述负载证明有界显示下通信持续完成，不宣称硬实时截止期或无损趋势采样。UI13 最终用户视觉验收、旧13真实硬件联调不在本自动测试完成声明中；旧14–19没有启动，未制作便携发布包。
