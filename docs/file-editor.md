# 插件文件编辑页

UI07 提供 `debugger_widgets` 中的 `FileEditorPage` 和 `FileDocumentEditor`（`include/debugger/file_editor.h`）。宿主工作台仍只承载插件参数视图，不识别 Modbus 字段或文件类型。主站实例位于 `plugins/master_parameters.cpp`，具体表单位于 `plugins/parameter_file_editor.cpp`。

插件实现 `FileDocumentEditor`：`document()` 返回当前草稿；`validate()` 完整验证格式；`setDocument()` 先完整验证，失败保留现有内容；`emptyDocument()` 创建本格式的新文档；用户修改后调用 `changed`。格式编辑器不处理文件、会话配置或设备发送。主站编辑器保留未知根字段及行字段，支持正式 version=1 参数定义往返。

`FileEditorPage(access, editor, apply, parent)` 接收公开 `SessionAccess` 和内容控件。`bindTarget(id, label, document, path)` 在草稿保护通过后切换目标，失败返回 false。相同目标保留当前独立编辑快照；单纯刷新运行数据不重置文件页。应用回调收到捕获的目标 ID、完整有效文档及已确认来源路径，返回空字符串表示成功，非空字符串向用户说明拒绝原因。回调须原子更新目标，服从既有会话 configure/修改保护，不能隐式发送设备指令。

公共组件以独立 owner 注册 `registerViewState`，复用四任务上限的 `background`，读取/序列化文件上限 1 MiB；任务仅捕获值，GUI 完成回调检查 QPointer。`QSaveFile` 保证写入失败不截断原文件，编辑 revision 确保迟到保存不清除新修改。保存、打开、新建、另存与应用是独立动作，覆盖文件有确认提示。主站、从站和契约验证插件复用相同组件，分别提供格式 editor 与 apply；协议字段不下沉到宿主。

UI08 增加可选 `FileEditorPage::Options`：默认 `maximumBytes` 仍为 1 MiB，从站明确使用原文件能力的 4 MiB。`canSave(path)` 由插件检查目标文件与其他设备绑定及自身文件任务的冲突，覆盖确认返回后再次检查。公共页只展示返回的拒绝原因，不解释设备或协议。保存使用紧凑 JSON，保留所有字段值。`isBusy()` 供所在插件协调运行值文件操作；文件草稿仍由页面自身守卫管理。

通用 `ParameterTable::onEdit` 保留主站逐键维护待提交值的行为；UI08 增加 `onCommit`，只在完整编辑提交后排队触发，Escape 不触发。从站使用后者校验完整值后更新模拟数据，避免无效输入的中间数字先变为运行值。程序刷新表格不会调用该提交入口。

文件页的选择器、覆盖确认和草稿确认也计入 busy，已发起的后台工作区读取不能在这些模态阶段替换页面。每次退出模态循环先检查 QPointer，再恢复状态；不使用引用页面成员的局部回滚对象。对话框使用堆对象并用 QPointer 清理，以允许父视图在模态循环中销毁。

需要确认框的插件可复用 `debugger/confirmation.h` 的 `showConfirmation`（链接 debugger_widgets）。调用方仍须先持有 lifetime、返回后检查自身存活，并注册适当 busy 状态；此函数不管理会话。Qt 下拉框信号可能继续访问 sender，应先退出信号发送栈再进入可能销毁整个视图的守卫。主站原生设备选择先恢复旧显示并排队选择；公开 `ParameterView::selectDevice` 仍同步执行守卫，成功后无信号更新下拉框，保留 bool 成功/取消契约。

UI07 增加 SDK12 `SessionAccess.lifetime`，以不透明 `shared_ptr<void>` 保留插件目录。任何可能进入模态事件循环的插件回调应先复制该租约，再复制需要调用的插件闭包，并以 QPointer 检查 owner 是否存活。局部闭包先于租约析构；宿主将最后一次租约释放延后到下一事件回调，覆盖插件返回指令仍位于 DLL 的情况。后台任务继续使用原有任务租约。SDK、协议/传输 IID 与构建指纹均为 12，所有宿主和 DLL 须一起重编并重启。

视图、SessionAccess 同步回调和 lifetime 的使用/释放均在 GUI 线程。延迟卸载只保护本线程的视图返回栈，不是任意工作线程执行插件代码的许可。后台作业必须通过 host background 的独立任务租约执行，继续遵守只传值、不访问 QWidget 的约定。
