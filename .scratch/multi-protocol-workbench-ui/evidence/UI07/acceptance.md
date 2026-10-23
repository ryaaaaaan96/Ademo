# UI07 独立文件页、保存与应用验收

日期：2026-10-23。任务：[UI07](../../issues/07-file-editor-save-apply.md)。分支 `codex/ui07-file-editor`，起点 `e88a733`；实现提交 `9315f52`，通过 `f3f697d` 合入集成 `233ec2a`（包含 UI06/UI09 和最新调度记录）。最终证据与任务勾选由同次 Git 提交固定，最终提交见综合索引。

## 完成行为

- 每个主站参数视图有独立“参数操作/文件编辑”页；文件页明确显示设备和已确认来源，编辑表提供名称、数据区、PDU 地址、自动类型、单位、说明与分组。新建、添加/删除、打开、保存、另存不要求用户手写 JSON。正式 version=1 参数定义和未知扩展字段可往返。
- 编辑、打开、保存只操作编辑文档或磁盘文件。保存使用后台有界任务与 QSaveFile，覆盖已有文件需确认；失败保留草稿和原文件。保存捕获开始时快照，期间的新编辑不会被迟到完成清除。路径输入与已确认来源独立，输入另一文件名不会把未打开文件绑定到应用结果。
- 只有明确点击“应用到目标设备”才完整校验并原子更新对应设备声明与来源，其他设备/会话保留。运行中可保存文件，但应用被现有会话配置入口拒绝；测试端点统计保存/应用流程 0 次发送、只有显式启动的一次 open。
- 文件草稿以独立 QObject owner 注册 UI05 多视图修改保护，不覆盖待提交值或旧从站守卫。切设备、关闭、新建、重载均可取消；失败和取消保留内容。实际原生会话窗口关闭取消保留草稿，放弃只关闭该窗口，其他会话保持打开，重开不应用文件。
- 公共 `FileEditorPage` 管文件流程，插件 `FileDocumentEditor` 管内容及格式；主站具体表单在独立插件源文件，宿主不解释 Modbus。UI08/UI12 复用入口与约定见 [公共组件文档](../../../../docs/file-editor.md)。
- SDK12 增加 GUI 线程 `SessionAccess.lifetime`，同步模态回调先持有租约、后复制闭包，宿主将最后释放延迟至下一事件回调，保护插件闭包和返回指令。后台仍用原有 background 任务租约；不承诺任意工作线程插件执行安全。SDK/IID/构建指纹配套升级，所有 DLL 须一起重编并重启。

## TDD 及整体验证

使用规格已批准的公开会话入口、可控端点和真实 Qt 控件操作，不用私有模型断言或精确像素。

1. [页面 red](red-file-page.txt)：没有文件页；实现独立页面后验证编辑、保存和显式应用分离。
2. [新建 red](red-new-open.txt)：缺少新建入口；完成新建、打开和另存整条流程。
3. [设备重绑定 red](red-target-binding.txt)：加载设备18后文件页仍指向17；显式绑定后目标和内容一致。
4. [数据区 red](red-area-change.txt)：holding 改 input 时类型不变，旧变化通知漏掉草稿；修复为数据区自身通知变化。
5. [来源绑定 red](red-source-binding.txt)：仅输入新路径导致未打开文件成为来源；修复为成功打开/保存才更新确认来源。
6. [DLL租约 red](red-callback-lifetime.txt)：会话销毁后公开视图访问对象没有保留插件目录；SDK12 保持租约并延迟最后释放，实际 weak_ptr 生命周期观察通过。
7. [原生完整文件行为](green-lifetime-close.txt)：12项 QtTest 通过，包含保存期间新修改、pending+文件草稿组合、失败/取消、双会话关闭、运行中0发送；早期 offscreen 日志只用于 TDD，不冒充桌面验收。

集成回归首次为16/17，UI06两处断言依赖固定70ms/已满足的前一行值，无法保证最后一条异步结果已显示。[轮询 red](red-integrated-poll-refresh.txt) 和 [第二操作 red](red-integrated-read-refresh.txt) 保留真实失败；只将最终777和30的观察改为 QTRY 等待同一预期，不改生产读取/轮询逻辑。随后全量 Debug 通过。

Release 发现协议选择守卫销毁窗口后 Qt sender 仍继续返回。通过单例红绿循环修复触发时机，再独立验证设备选择：LLDB 定位到静态 QMessageBox 的栈子对象被父析构删除，改为公共堆对话框组件；仅撤回设备排队又出现 sender 栈错误，分别固定两个根因。文件选择器期间后台工作区替换也以真实用例验证 busy 保护。排队设备请求增加修订号，避免早期选择覆盖后来明确的目标。完整假设、原始栈、red/green与限制见 [生命周期诊断](lifecycle-diagnosis.md)。

| 配置与范围 | 结果 | 证据 |
| --- | --- | --- |
| Windows Debug 全量（最终模态修复前） | 17/17 CTest，294项 QtTest，0失败/跳过 | [日志](debug-ctest.log)、debug/原始结果 |
| Windows Release 全量（协议切换修复后，最终设备/文件模态修复前） | 17/17 CTest，295项 QtTest，0失败/跳过 | [日志](release-ctest.log)、release/原始结果 |
| 最终 Windows Debug 相关回归 | 9/9 CTest，97项 QtTest，0失败/跳过 | [日志](debug-final-ctest.log)、debug-final/原始结果 |
| 最终 Windows Release 相关回归 | 9/9 CTest，97项 QtTest，0失败/跳过 | [日志](release-final-ctest.log)、release-final/原始结果 |

工具链：既有 Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17、CMake3.30.5、Ninja；固定 `qt-llvm-debug` / `qt-llvm-release`，构建 `--parallel 3`，Windows 测试串行并经集成协调独占原生窗口时段。未修改全局环境或执行策略。

最终相关范围为文件页15项、待提交写入、读取/轮询、参数定义、参数窗口、工作区、原生窗口恢复、旧从站编辑和记录视图。没有把最终局部复测冒称最终全量；整体UI13阶段仍做双配置完整验收。

复现：`cmake --build --preset qt-llvm-debug --parallel 3` 完成后单独运行 `ctest --preset qt-llvm-debug --output-on-failure`；Release替换预设名。定向文件页用 `-R parameter_file_edit_save_apply`；`DEBUGGER_EVIDENCE_DIR` 可保存实际控件客户区截图。

## 视觉、自检及边界

[实际 Windows 文件编辑页](file-editor-save-apply.png) 已目视检查：白色背景，设备17、来源路径、表格字段及独立保存/应用按钮完整可见，无文字裁剪。只保留本切片截图；旧切片重跑截图未重复纳入。这是自动化操作验收，不代替用户正式确认或真实硬件互通。

自检确认 Modbus 字段留在插件，公共组件不发送设备命令；保存和应用边界、草稿 revision、原子文件替换、QPointer和目录租约、源路径绑定均有对应验证。`git diff --check` 通过。独立 Standards/Spec 整体审查由集成阶段执行，不在本项冒称完成。

限制：关闭提示中选择保存启动后台任务，完成后需再次关闭；打开/保存文件预算1 MiB、每设备4096参数；只实现主站参数文件内容页，其他角色由UI08/UI12提供。本项不启动旧13硬件联调或旧14–19。

散列清单固定本目录验收证据，文本按UTF-8/LF归一化，图片按原始字节，构建产物不入 Git。
