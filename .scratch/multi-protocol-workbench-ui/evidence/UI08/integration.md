# UI08 集成核验

日期：2026-10-23。最终实施提交 `96d9b49ccc261abf808b4b51ede4e78b8639f955` 已包含集成 `d17ff18`。核验双方工作树干净和祖先关系后，通过 `503ea6f` 无冲突合入 `codex/workbench-ui-integration`，未纳入构建产物。

保留通用角色挂载及排队选择／取消、从站完整值 `onCommit` 与主站 `onEdit` 分离、FileEditorPage 插件文件预算与保存保护。三条旧从站界面用例迁移到公开会话及参数视图，原协议帧与数据断言保留；宿主没有新增 Modbus 领域分支。

主工作树使用既有 Qt/LLVM-MinGW 预设完成 [Debug配置](merge-debug-configure.log) 与 [全目标构建](merge-debug-build.log)，SDK12 配套宿主、DLL、独立测试夹具和测试全部重编。

设置仅测试进程的 Qt/LLVM DLL 搜索路径与 `QT_QPA_PLATFORM=offscreen`，串行运行以下两个实际 Qt／真实会话流程：

- `slave_parameter_tests.exe`：**11项通过，0失败／跳过**；覆盖即时运行值、非法完整输入拒绝、保存和明确应用分离、保存期间新写保留、外部文件变化拒绝、文件预算及模态销毁保护。见 [从站原始结果](merge-slave-parameter-offscreen.txt)。
- `parameter_write_tests.exe`：**13项通过，0失败／跳过**；主站逐键待提交、仅明确设置才发送、写结果与草稿修订、多个守卫及关闭取消仍有效。见 [主站原始结果](merge-parameter-write-offscreen.txt)。

两进程退出码均0，合计24项。offscreen 的 `raise()`／`propagateSizeHints()` 能力警告保留在日志，不视为新增 Windows 原生窗口验收。实现者最终两配置覆盖十套相关回归、旧从站迁移复验及截图见 [acceptance.md](acceptance.md)；本次未重复全量或原生桌面检查。

原 `SHA256SUMS.txt` 以本目录为基准，文本UTF-8/LF、PNG原字节核验 **45/45一致**。原始证据不覆盖，新增集成日志及本文由本次提交固定。

UI08已完成，UI11状态留集成协调者调度。整体独立审查和用户正式产品确认留UI13，旧13硬件及旧14–19边界保持。
