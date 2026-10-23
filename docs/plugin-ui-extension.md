# 配套插件的参数界面与文件编辑扩展

当前配套契约为 SDK12、产品0.2；宿主、协议／传输 DLL、Qt、编译器、架构和 Debug/Release 必须一起配套构建。`DebuggerProtocolIid` / `DebuggerTransportIid` 与 `buildKey()` 都参与兼容检查。插件只在启动时发现，替换 DLL 后重启；未知或不兼容插件保留错误提示和工作区身份，不建立热加载承诺。

## 可复用边界

- `ProtocolPlugin::createSession` 承担组帧、请求校验、应答及超时。通过 `ProtocolServices` 使用共享传输和记录；专用控件不直接打开端点。
- `createParameterView` 返回 `ParameterView`。插件提供 `devices()`、`selectedDevice()` 和可取消的 `selectDevice()`；宿主据此生成会话→设备导航，不识别插件字段。
- 链接 `debugger_widgets` 后可使用 `ParameterTable`，列、行身份、动作、可编辑／待提交标记由插件提供。标准 Qt 表格的专用 delegate 或 cell widget 可承载枚举、开关等控件；插件负责其值校验及与观测刷新、操作提交的关系。改变编辑器不意味着允许把协议逻辑搬进宿主。
- 插件可以把模板表格和专用 QWidget 组合在自己的参数页中。文件内容页实现 `FileDocumentEditor`，再由 `FileEditorPage` 承担新建、打开、保存、另存、草稿保护及明确应用。格式验证先于替换；应用回调完整校验目标后原子更新，不隐式发送设备指令。详见 [文件编辑页契约](file-editor.md)。
- 自有文件引用通过 `relocateConfiguration` 做纯路径转换，不能在工作区保存／恢复时暗中读取文件或启动通信。未知字段按本插件版本兼容约定保留。

## 生命周期规则

所有 QWidget、`SessionAccess` 同步调用及其 `lifetime` 使用都在 GUI 线程。进入可能销毁视图的模态流程前先复制 `access.lifetime`，再复制插件闭包；返回后先检查 `QPointer`。视图可能被销毁时使用堆对话框及安全的 `showConfirmation`。文件选择和确认期间也需要登记 busy，防止后台工作区替换绕过保护。

`registerViewState(owner, state, resolve)` 只登记本视图的修改，不覆盖其他视图或协议模拟值守卫。选择设备应先处理旧草稿；失败保持旧目标和旧内容。后台文件任务仅传值，经 `access.background` 执行并在 GUI 回调检查视图，不能自行捕获裸 QWidget 到工作线程。

宿主对每个 QPluginLoader 显式取得 load 引用后才获取 instance，确保目录租约交叠时其中一个目录释放不销毁另一个仍使用的根对象。最后视图调用栈仍通过 SDK12 延迟租约退出 DLL；这不允许任意线程执行插件，也不意味着运行中允许换 DLL。

## 独立 DLL 示例与验证

`tests/ui_contract_plugin.cpp` 是受控的“场景”契约样例，不是寄存器模型：

- 灯组 A/B 使用字符串身份；表格列为场景、本次预览模式、动作。预览模式是枚举控件，分组来自场景 JSON，并附插件自己的诊断分组。
- 文件页提供场景名称、分组、播放模式和允许预览的专用表单，格式为 `kind=ui-contract-scene`、`version=1`。未知字段随文档往返，名称／分组最多80字符，文件仍受公共组件预算约束。
- 保存只写文件；应用在停止状态更新指定灯组的场景声明和路径。另一灯组不变，之后明确预览才形成测试发送。
- 测试线格式仅为 `PREVIEW lamp-a blink\n` / 完整输入块 `ACK lamp-a\n`，一个待确认操作、1秒超时、不自动重发。它只检验宿主操作和记录路径，不是可交付设备协议、通用流解析器或 UPP。

构建 `plugin_ui_contract_tests` 会分别编译示例 DLL、旧 IID 夹具及错误构建指纹夹具。它们只进入 `bin/ui-contract-fixtures`；正式 `bin/plugins` 仍只包含产品插件，不要把测试 DLL 复制到实际调试目录。

```powershell
& C:/Qt/Tools/CMake_64/bin/cmake.exe --preset qt-llvm-debug
& C:/Qt/Tools/CMake_64/bin/cmake.exe --build --preset qt-llvm-debug --target plugin_ui_contract_tests --parallel 3
& C:/Qt/Tools/CMake_64/bin/ctest.exe --preset qt-llvm-debug -R '^plugin_ui_contract$' --output-on-failure
```

Release 换为 `qt-llvm-release`。预设沿用本机 Qt/LLVM-MinGW，不更改系统 PATH。测试使用独立子进程证明安装 DLL 后原进程不重新发现、重启后创建真实插件视图；同时覆盖两目录交叠、错误契约拒绝、多协议会话隔离、文件保存／应用和模态父视图销毁。

这些验证不启动旧13硬件联调，不实现 UPP/CAN、TCP 或旧19正式交付，也不替代 UI13 用户对正式产品的操作验收。
