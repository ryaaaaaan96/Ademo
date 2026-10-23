# 10 验收记录

## 范围与环境

仅实施当前 issue 10，复用 01–09/11/12；没有启动 13–19。Windows x64、Qt 6.12.0 LLVM-MinGW、Clang 22.1.7、C++17、CMake 3.30.5、Ninja 1.12.1 沿用已确认环境，SDK 8 配套重编译。工作区格式和路径规则见 [workspace-format.md](../../workspace-format.md)。主体只管理配置和通用插件钩子，不解析 Modbus 设备路径或绕过原始通道。

## 实际执行

命令：`./scripts/verify.ps1 -EvidenceId 10`，分别配置、编译、CTest Debug 与 Release。最终结果以本目录两配置 configure.log/build.log/ctest.log、各 *-results.txt 为准；sha256.txt 同时散列测试程序、产品 EXE、两插件 DLL、截图与日志。Debug 与 Release 均 11/11 CTest 目标通过，每配置合计 234 项 Qt 检查（workspace 13 项含 init/cleanup）。最后将协议切换销毁回归改为 stop 后可点击路径后，Debug 额外重编译并定向复跑 workspace 通过，追加日志并重新计算其产物与证据散列；Release 完整运行已包含该最终路径。

公开 Workbench API 与 Widgets 覆盖：未知根/会话/配置/协议/端点/重连/布局字段往返、未启动表单修改保存、窗口尺寸/当前标签恢复、实际打开保存按钮、单个缺插件会话禁用且不影响另一会话、incompatible 夹具与无效 DLL 加载失败原因可见、缺插件原基准目录跨 SaveAs 保留、新 catalog 修复后从原外部文件加载 777（恢复时端点 opens=0，明确启动后 opens=1）、合法相对路径移动、无效文档不替换旧会话、无插件空白页保存拒绝且不覆盖旧文件、单窗口忙/全局四任务上限、窗口销毁回调安全、后台文件任务挂起期间公开会话读取成功、全部 dirty 确认后再检查其他会话新修改。

## 审查与修复

初次专项源码审查发现三项 P2，均修复并加入公开回归，扩查另关闭一项同类 P2：

1. 未知串口枚举被非 editable combo 默认值覆盖：加载时显式保留原枚举；无操作保存仍为 230400，公开 start 被 serial.validate 拒绝且端点未打开。
2. 原 16 页禁用新增按钮后恢复空工作区，按钮未恢复：替换开始后统一刷新新增状态；回归执行 16 页→空 sessions→点击新增，得到 1 页。
3. resolveUnsaved 的回调销毁窗口后，apply/外层完成继续访问 controller：每次潜在事件泵返回后检查 QPointer，apply 返回后再次检查；回归从 dirty 从站确认中 delete Workbench，验证存活指针为空并可再次占用全部 4 个任务槽。

4. 协议选择切换的 resolveUnsaved 返回后仍访问已析构 combo：SessionPane 在确认返回后先检查 QPointer。公开 UI 回归加载缺插件条目，切换 Modbus、启动从站并修改，stop 保留 dirty 后确认 combo 可用，再切 missing，由 guard 删除窗口并安全返回。

自查保存前使用加载同一 validate，拒绝不能往返的配置且 QSaveFile 不改旧文件；全部确认后无事件泵复查 dirty/busy；未知插件引用的 referenceBase 未丢失；主体没有 Modbus 字段知识。独立只读复核由 rtu_records 完成，确认原三项及扩查协议切换同类 P2 共四项均关闭，无新增确定问题；复核未修改文件或代替本分支测试。

## 证据与限制

`workspace-restored-missing-plugin.png` 目视确认：第二会话显示缺失协议，启动禁用，串口配置保留，顶部说明保存配置快照未写设备。截图和散列每配置独立保存。Windows Qt 插件默认防卸载使临时 DLL 夹具目录在进程退出前可能出现清理警告，不影响断言和加载诊断；未将临时 DLL 或构建产物纳入 Git。

本轮使用实际 DLL 与可控字节流端点；未声称真实串口、时序或硬件性能验收，13 留待硬件条件确认。


