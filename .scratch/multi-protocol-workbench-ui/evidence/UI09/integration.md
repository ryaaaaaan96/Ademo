# UI09 集成核验

日期：2026-10-23。功能提交 `a2af256` 经 `33706f9` 同步集成 `a45a940`，最终验证提交为 `3c8b3d9`。核验实现和集成工作树均干净、实现分支包含当前集成基线后，通过 `2fa6317` 无冲突合入 `codex/workbench-ui-integration`。未纳入构建产物。

主工作树复用既有 Qt/LLVM-MinGW 预设完成 [Debug配置](integration-debug-configure.log) 和 [全目标重编](integration-debug-build.log)，SDK11 及 UI05/UI09 的 CMake 测试目标均保留。

为避免与开发中的 UI06/UI07 争用桌面，本次设置仅测试进程的 Qt/LLVM DLL 搜索路径与 `QT_QPA_PLATFORM=offscreen`，直接执行 `build/qt-llvm-debug/bin/window_trace_tests.exe`。结果 **5 项 QtTest 通过，0 失败／跳过，退出码0**，见 [原始结果](integration-window-trace-offscreen.txt)。覆盖窗口移动后各自查询／导出来源不变、历史默认所属会话并导出未知数据，以及传输提交、本地发送和过期来源的独立表达。

本次是实际 Qt 控件和真实会话路径的 offscreen 软件复测；平台提示不支持 `raise()`／`propagateSizeHints()`，不将其扩称为 Windows 标题栏、Z-order 或人工拖动验收。实现者在 SDK11 合并后的 Windows Debug／Release 五套关联验证均5/5、各76项，及较早 SDK10 完整双配置14/14、各260项，见 [acceptance.md](acceptance.md)。最终独立双轴审查按 UI13 集成阶段统一执行。

原证据清单按文本 UTF-8/LF、PNG 原字节核验 **72/72 一致**，未重写历史清单。新增日志及本文由本次集成证据提交固定。

UI09 已完成，UI06/UI07 保持实施中。该软件核验不代表 UI13 用户正式产品确认，旧13硬件及旧14–19的范围保持。
