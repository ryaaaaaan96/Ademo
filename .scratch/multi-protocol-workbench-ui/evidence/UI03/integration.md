# UI03 集成核验

日期：2026-10-23。实现分支 `codex/ui03-session-windows` 的功能提交为 `b26a926`，同步当前集成 `1032460` 后提交交叉验证证据为 `69def47ed3b79491ecc199956a5b05fef7dbe921`。通过 `94c635f` 无冲突合入 `codex/workbench-ui-integration`；合入前两棵工作树均干净，未纳入构建产物，README/CMake 中 UI03 和 UI04 的增量均保留。

主工作树复用原有 Qt/LLVM-MinGW 预设重新 [配置](integration-debug-configure.log) 和 [构建](integration-debug-build.log) Debug。独占桌面时段，以 Windows Qt 平台串行执行：

`ctest --preset qt-llvm-debug -R '^(parallel_sessions_recovery|workspace_roundtrip)$' --parallel 1 --output-on-failure`

结果为 **2/2 CTest 通过，33 项 QtTest 通过，0 失败／跳过**，总耗时19.18秒。见 [CTest日志](integration-debug-ctest.log)、[窗口生命周期原始结果](integration-recovery-results.txt)、[工作区原始结果](integration-workspace-results.txt)。覆盖原生窗口停靠／取消／遮挡、会话关闭保留及重开、记录与设备归属、旧未保存保护及工作区往返。测试结束已释放桌面占用。

实现者在 SDK10 合并后的 Debug／Release 五套交叉验证各54项，以及此前两配置完整12套各247项，见 [acceptance.md](acceptance.md)。本次没有把定向复测写成完整回归，也不重复无新风险的全量检查。

原清单按其声明的文本 UTF-8/LF、PNG 原始字节方式核验 **68/68 一致**。原证据清单保持不变，新增集成日志及本文由本次提交固定。`git diff --check` 通过。

UI09 的直接前置已满足；UI05 保持实施中。正式产品的标题拖动／Esc体验、跨显示器及用户整体确认仍由 UI13 验收，旧13硬件条件和旧14–19边界不变。
