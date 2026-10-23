# Release 协议切换期间销毁窗口：诊断记录

采用 diagnosing-bugs 技能。公开 Qt 工作区测试原有 `protocolSwitchGuardCanDestroyWindow` 在本轮 Release 全量中触发 0xc0000005；Debug 当时通过。原始证据见 release-initial-ctest.log、red-workspace-window-lifetime.txt。

## 可复现循环

先完成 Release 构建，Windows Qt 平台运行：

```powershell
./build/qt-llvm-release/bin/workspace_tests.exe protocolSwitchGuardCanDestroyWindow -o result.txt,txt
```

单用例约0.4秒复现，相同 Qt 调用栈为 QComboBox::setCurrentIndex → count/Qt 内部调用；见 red-workspace-minimal.txt。场景是存在从站草稿，协议下拉切换进入修改守卫，守卫同步删除整个 Workbench，随后原下拉框的信号发送栈继续返回。测试成员持有 PluginCatalog，目录未在此过程中卸载。

## 假设与排查

1. 下拉框自身仍在发送信号时 sender 被守卫同步删除。预测：仅把可能进入修改保护的重建排到下一 GUI 事件，保持真实删除和现有 QPointer，崩溃消失。
2. SDK12 插件租约过早释放。预测：持有外部目录仍应规避崩溃；实际失败测试全程持有成员目录，且异常在 Qt DLL，因此不支持此项。
3. updateProtocol 在守卫返回后仍访问已销毁页面。预测：必须修正回调本身的存活检查；代码已有 resolveUnsaved 后立即检查 QPointer，改变外层调用时机而不改变该检查即可区分。

把窗口最终销毁断言改为 QTRY 后、尚未修改生产代码，依然约0.38秒崩溃（red-workspace-eventual-destroy.txt），说明观察异步化没有掩盖崩溃。一次先取消再销毁的探针改变了堆状态且未复现（probe-workspace-cancel-then-destroy.txt）；没有把该单次通过当成修复。

## 修复与验证

只在 SessionPane 协议选择遇到 dirty/busy 状态时使用带 owner 上下文的 `QTimer::singleShot(0, this, updateProtocol)`；清洁切换保持原行为。先退出 QComboBox 的信号栈，再执行可能销毁窗口的守卫。原有 QPointer 和取消回滚保留，owner 先销毁时排队回调自动取消。

同一最小用例约0.45秒通过（green-workspace-eventual-destroy.txt）。另加独立 `protocolSwitchCancellationRestoresSelectionAndKeepsDraft`，断言取消后最终选择恢复 modbus.rtu、草稿仍在、会话仍 Stopped。原销毁测试仍执行真实 delete 并观察窗口消失，没有改成 deleteLater 或删除覆盖。最后在 Debug/Release 全量验证，结果见 acceptance.md。

未保留临时诊断日志或生产探针。修复落在宿主协议选择触发处，不改变 Modbus/串口语义，也不提供任意线程执行插件的机制。

## 设备选择与文件对话框的独立边界

随后按同一具体模式验证真实设备下拉框：在文件草稿提示中删除独立 ParameterView，并断言另一个会话内容/记录不变。初始进程以 -1073740940（heap corruption）退出。只把设备选择排队仍然失败，说明不能把所有失败归结为信号返回栈。

使用既有 LLVM-MinGW 的 LLDB 捕获原始栈（probe-device-lldb.txt）：`FileEditorPage::~FileEditorPage → QWidget::~QWidget → QObjectPrivate::deleteChildren → free`，下层仍在确认框 exec。独立 view 的 unique_ptr 是唯一删除者，不涉及 SessionPane raw pointer。静态 QMessageBox 便利函数的栈上子对话框被父析构当作堆子对象删除。改为堆对话框和 QPointer 后，排队设备选择路径通过。保持堆对话框但仅撤回排队，实际命令再以 -1073741819（0xc0000005）失败，栈恢复为 QComboBox::setCurrentIndex/count（red-device-sender-stack.txt）：由此分别确认对话框所有权和 sender 返回栈两个根因，而非用延迟隐藏析构问题。

UI05 待提交值提示的数据行也以 -1073740940 复现（red-pending-dialog-owner.txt）。两类提示现共同使用 `showConfirmation`；打开/另存选择器同样改为堆 QFileDialog。文件草稿和 pending 两个数据行均执行真实父视图销毁和最终析构，另会话不变（green-file-pending-owner-destruction.txt）。文件页没有依赖 SessionPane 的悬空指针。

另一条正常操作竞争使用公开 Workbench：已有后台 openWorkspace 在“另存为”选择器期间完成。初始返回 success 并替换页面，期望为 cancelled（red-file-picker-workspace.txt）。文件页所有模态阶段纳入自身 busy；退出模态先检查 QPointer，再恢复 busy，避免外部工作区重载绕过保护。该用例及其余文件页行为最终 offscreen 15/15 通过（green-modal-lifecycle.txt）；最终原生双配置定向结果见 acceptance.md。

组合参数窗口回归捕获到排队选择的后续问题：添加设备1后立即添加并明确选择17，早期排队的1随后覆盖了17，实际帧为 `01 03 00 00 00 01 …`（red-stale-device-selection.txt）。新增选择修订号；任何较新的明确选择均使旧排队请求失效。原测试仍要求真实帧 `11 03 00 6b 00 01 f7 46`，没有放宽目标断言。文件页、参数窗口、轮询、写入及工作区共同回归后交付。
