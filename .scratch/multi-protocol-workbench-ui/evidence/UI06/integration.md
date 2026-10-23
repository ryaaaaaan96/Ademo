# UI06 集成核验

日期：2026-10-23。功能提交 `6f44feb` 经 `aa0f41d` 同步集成 `ff6bc7b`，最终验收提交 `31fe1f4`。核验实现分支包含当前集成基线、双方工作树干净后，通过 `0da79ed` 无冲突合入 `codex/workbench-ui-integration`。未纳入构建产物，仍使用本项 SDK11；UI07 尚未合入的 SDK 演进不提前带入。

主工作树复用既有 Qt/LLVM-MinGW 预设完成 [Debug配置](merge-debug-configure.log) 与 [全目标构建](merge-debug-build.log)。UI07 此时独占 Windows 桌面，本次设置仅测试进程的 DLL 搜索路径和 `QT_QPA_PLATFORM=offscreen`，直接运行 `build/qt-llvm-debug/bin/parameter_poll_tests.exe`。

结果 **9 项 QtTest 通过，0 失败／跳过，退出码0**，见 [原始结果](merge-parameter-poll-offscreen.txt)。覆盖选区与组目标、不同设备和数据区隔离、协议范围拆分、容量整体拒绝、切换后固定轮询范围、真实读回期间的待提交保护，以及停止后保留在途读取与读取重试。

这是实际 Qt 参数控件及会话路径的 offscreen 复测，不是新增 Windows 原生窗口验收。平台 `propagateSizeHints()` 不支持警告保留在原始日志。实现者 Windows Debug 全量16/16、282项和 Release相关8/8、119项证据见 [acceptance.md](acceptance.md)，本次未运行原生桌面回归或重复无关全量检查。

原清单路径相对于本证据目录，按其声明的文本 UTF-8/LF、PNG原字节核验 **41/41 一致**。为保留已有 `integration-debug-build.log` 等实施者历史文件，新集成日志使用 `merge-*` 名称；原证据和清单不覆盖，新日志及本文由本次提交固定。

UI06 已完成，UI10 直接前置满足；UI07 保持实施中。UI13 用户正式产品验收、旧13硬件及旧14–19的边界保持不变。
