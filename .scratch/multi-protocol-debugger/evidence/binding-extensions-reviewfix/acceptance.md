# 从站绑定扩展字段保留修复验收

整体 Spec 审查发现 P2：保存或导入设备成功后 publishBindings 从运行 devices_ 重建仅 unit/path，导致设备绑定未知字段丢失。现改为遍历原 configuration.devices，按 unit 仅 overlay 当前 path，保留条目顺序、所有扩展与未编辑设备。

新增公开会话回归 `bindingExtensionsSurviveSaveImportAndWorkspaceRoundTrip`：两个设备分别带 vendor 和不同结构 future；仅设备17另存后完整比较两条绑定，再导入新的设备17文件并完整比较两条绑定；通过 Workbench 保存/加载工作区后再次完整比较两条绝对路径绑定，恢复保持 Stopped 且不把运行 data 模型写入协议配置。该回归同时覆盖另一个设备未被修改的配置。

沿用现有 Qt/LLVM-MinGW Debug，仅重编 slave_edit_tests 并运行 `ctest --preset qt-llvm-debug -R '^slave_edit_import_save$' --verbose`，1/1 CTest、10 项 Qt 检查通过。实际命令、日志、结果与 SHA256 见本目录；最终双配置由根集成分支统一执行。未修改索引或 trace_query。
