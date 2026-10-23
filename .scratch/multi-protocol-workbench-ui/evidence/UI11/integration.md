# UI11 集成核验

日期：2026-10-23。功能实现 `a687834`，最终证据提交 `90a475d45172afb9a867441fae4f77a1a494a197` 已包含当前集成 `db614b9`。核验双方工作树干净与祖先关系后，通过 `1003854` 无冲突合入 `codex/workbench-ui-integration`；未纳入构建产物。

保留首次显示的恢复位置、关闭窗口配置与状态、恢复布局只重排而不停止通信、完整工作区恢复全部停止，以及三类修改守卫和文件选择器busy保护。650×550小浮窗的密度与兼容入口收束留给UI13，未将本项布局截图冒称最终视觉验收。

主工作树复用既有Qt/LLVM-MinGW预设完成 [Debug配置](merge-debug-configure.log) 和 [全目标构建](merge-debug-build.log)。在经协调的独占Windows桌面时段串行执行：

`ctest --preset qt-llvm-debug -R '^(workspace_window_layout|workspace_roundtrip)$' --parallel 1 --output-on-failure`

结果 **2/2 CTest通过，29项QtTest通过，0失败／跳过**，总耗时12.84秒。布局套件15项、既有工作区14项，见 [CTest日志](merge-workspace-windows-ctest.log)、[布局原始结果](merge-workspace-layout-windows-results.txt)、[工作区原始结果](merge-workspace-windows-results.txt)。尺寸检查使用Windows实际屏幕，未用offscreen虚拟小屏替代；测试结束已释放桌面。

实施者两配置相关6/6、各88项及外部文件／修改保护／离桌面坐标的完整本项证据见 [acceptance.md](acceptance.md)，本次未重复无新风险的全量协议测试。

原 `SHA256SUMS.txt` 以本目录为基准，文本UTF-8/LF、PNG原字节核验 **33/33一致**。原证据不覆盖，新增集成日志及本文由本次提交固定。

UI11已完成，UI13状态留集成协调者调度。多物理显示器及用户正式产品确认仍未由本次自动测试覆盖，旧13硬件及旧14–19边界保持。
