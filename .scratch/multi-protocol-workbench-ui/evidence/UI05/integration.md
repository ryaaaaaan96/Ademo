# UI05 集成核验

日期：2026-10-23。实现分支 `codex/ui05-pending-writes` 最终提交 `dc075b7`，合入前已通过 `aa4ebc4` 同步集成 `2187294`，双方工作树干净。通过 `efe2930` 无冲突合入 `codex/workbench-ui-integration`；提交未包含构建产物。

主工作树使用既有 Qt/LLVM-MinGW 预设重新 [配置](integration-debug-configure.log) 并完成 [Debug全目标构建](integration-debug-build.log)，宿主、插件及测试按 SDK11 配套重编。

UI09 正在独占桌面执行原生 Windows 检查，本次未争用桌面。设置仅本次测试进程的 Qt/LLVM DLL 搜索路径及 `QT_QPA_PLATFORM=offscreen`，直接运行 `build/qt-llvm-debug/bin/parameter_write_tests.exe`。结果 **13 项通过，0 失败／跳过，退出码0**，见 [原始结果](integration-parameter-write-offscreen.txt)。测试覆盖真实 Qt 编辑控件与会话路径：活动编辑与新观测隔离、明确设置、提交修订、非法值拒绝、失败保留且无重试、多 owner 修改保护，以及关闭取消／放弃后的配置保留。

offscreen 输出中的 `propagateSizeHints()`／`raise()` 不支持警告属于该平台能力限制。本次不将窗口关闭逻辑检查扩称为 Windows 标题栏、遮挡或拖动验收，也没有运行需要 HWND/Z-order 的原生 recovery 套件。实现者的最终 Windows Debug 5/5、64项和 Release 7/7、103项交叉验证，以及先前完整 Debug14/14、268项，继续见 [acceptance.md](acceptance.md)；最终整体双配置验收另行执行。

原证据清单按文本 UTF-8/LF、PNG 原字节核验 **45/45 一致**，未重写历史清单。新增集成日志及本文由本次提交固定。

UI06 与 UI07 的直接前置已满足，可分别扩展读取轮询与独立文件编辑页；UI09 保持实施中。该软件集成核验不代表 UI13 用户正式产品验收，旧13硬件和旧14–19边界不变。
