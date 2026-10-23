# UI07 集成核验

日期：2026-10-23。最终实施提交 `286cbee41a5da60468291b2895269db001c665e1` 已包含集成 `233ec2a`，双方工作树干净。通过 `105ef2a` 无冲突合入 `codex/workbench-ui-integration`；未纳入构建产物。

主工作树复用既有 Qt/LLVM-MinGW 预设完成 [Debug配置](merge-debug-configure.log) 与 [全目标重编](merge-debug-build.log)，宿主、协议和传输插件及测试均为 SDK12 配套产物。随后串行执行真实 Qt 文件页与工作区测试，未运行原生 Z-order 套件或重复全部协议回归。

- 设置仅测试进程的 DLL 搜索路径及 `QT_QPA_PLATFORM=offscreen`，直接运行 `parameter_file_editor_tests.exe`：**15项通过，0失败／跳过**，见 [文件页原始结果](merge-file-editor-offscreen.txt)。覆盖保存／应用分离、草稿和目标保护、模态生命周期、文件选择器期间的工作区替换阻止及运行中零发送。
- 同样运行 `workspace_tests.exe`：13项通过、1项失败，见 [原始失败记录](merge-workspace-offscreen.txt)。唯一失败为窗口尺寸恢复期望1080×810，而 offscreen 屏幕限制实际为798×774；其他工作区与生命周期检查通过。本次未修改产品或断言规避该限制。
- 确认桌面空闲后改用 Windows 平台，执行 `ctest --preset qt-llvm-debug -R '^workspace_roundtrip$' --parallel 1 --output-on-failure`：**1/1 CTest、14项QtTest通过，0失败／跳过**，用时3.13秒。见 [CTest日志](merge-workspace-windows-ctest.log)、[原始结果](merge-workspace-windows-results.txt)。测试结束释放桌面。该复测确认此处尺寸差异限于 offscreen 屏幕约束，后续窗口布局与边界仍由 UI11 验证。

实现者最终 Windows Debug／Release 各9/9、各97项关联验收及历史全量阶段记录见 [acceptance.md](acceptance.md)；本次不把最终定向结果写成最终双配置全量。

原 `sha256.txt` 以本目录为路径基准，文本 UTF-8/LF、图片原字节核验 **94/94一致**。原清单和历史日志不覆盖，新集成日志及本文由本次提交固定。

UI07 已完成。UI08/UI12 的调度状态由集成协调者更新，本次保持其原状态；UI13 用户正式产品验收、旧13真实硬件和旧14–19边界不变。
