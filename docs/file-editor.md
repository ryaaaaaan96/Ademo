# 插件文件编辑页

UI07 提供 `debugger_widgets` 中的 `FileEditorPage` 和 `FileDocumentEditor`（`include/debugger/file_editor.h`）。宿主工作台仍只承载插件参数视图，不识别 Modbus 字段或文件类型。主站实例位于 `plugins/master_parameters.cpp`，具体表单位于 `plugins/parameter_file_editor.cpp`。

插件实现 `FileDocumentEditor`：`document()` 返回当前草稿；`validate()` 完整验证格式；`setDocument()` 先完整验证，失败保留现有内容；`emptyDocument()` 创建本格式的新文档；用户修改后调用 `changed`。格式编辑器不处理文件、会话配置或设备发送。主站编辑器保留未知根字段及行字段，支持正式 version=1 参数定义往返。

`FileEditorPage(access, editor, apply, parent)` 接收公开 `SessionAccess` 和内容控件。`bindTarget(id, label, document, path)` 在草稿保护通过后切换目标，失败返回 false。相同目标保留当前独立编辑快照；单纯刷新运行数据不重置文件页。应用回调收到捕获的目标 ID、完整有效文档及已确认来源路径，返回空字符串表示成功，非空字符串向用户说明拒绝原因。回调须原子更新目标，服从既有会话 configure/修改保护，不能隐式发送设备指令。

公共组件以独立 owner 注册 `registerViewState`，复用四任务上限的 `background`，读取/序列化文件上限 1 MiB；任务仅捕获值，GUI 完成回调检查 QPointer。`QSaveFile` 保证写入失败不截断原文件，编辑 revision 确保迟到保存不清除新修改。保存、打开、新建、另存与应用是独立动作，覆盖文件有确认提示。后续 UI08/UI12 可在插件中复用相同组件，提供各自格式的 editor 与 apply；不得把协议字段下沉到宿主。

UI07 增加 SDK12 `SessionAccess.lifetime`，以不透明 `shared_ptr<void>` 保留插件目录。任何可能进入模态事件循环的插件回调应先复制该租约，再复制需要调用的插件闭包，并以 QPointer 检查 owner 是否存活。局部闭包先于租约析构；宿主将最后一次租约释放延后到下一事件回调，覆盖插件返回指令仍位于 DLL 的情况。后台任务继续使用原有任务租约。SDK、协议/传输 IID 与构建指纹均为 12，所有宿主和 DLL 须一起重编并重启。

视图、SessionAccess 同步回调和 lifetime 的使用/释放均在 GUI 线程。延迟卸载只保护本线程的视图返回栈，不是任意工作线程执行插件代码的许可。后台作业必须通过 host background 的独立任务租约执行，继续遵守只传值、不访问 QWidget 的约定。
