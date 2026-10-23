# UI12 独立插件界面契约验收

2026-10-23，实施分支 `codex/ui12-plugin-ui-contract`。基于 UI07 集成 `1b882ca`，实现提交 `45cb5ca`，通过 `5618c51` 合入 UI10 集成 `bf91de0` 后重新配套编译验证。实际完成状态由本记录、issue 勾选和最终提交共同确认，分诊标签不作为完成证据。

## 实现与边界

- 独立构建 `ui_contract_plugin` DLL，通过现有发现和工厂接口提供场景型 `ParameterView`：字符串设备身份、非 Modbus 列、枚举单元格、由 JSON 声明的分组及专用诊断内容。
- 专用 `SceneEditor` 提供名称、分组、模式和复选控件，复用通用文件页完成保存与明确应用。保存不配置会话、不发送，应用只更新指定目标和确认来源路径；取消文件草稿提示保留原目标。
- 真实 Workbench 同时运行 Modbus 和测试协议，实际按钮形成受控 `PREVIEW lamp-a blink\n` 请求，输入完整 `ACK lamp-a\n` 后操作成功且通信记录属于该窗口。浮动／停靠不停通信，关闭目标窗口不停止另一会话。
- 独立子进程在安装测试 DLL 后仍保持原发现结果，重启后创建实际插件视图；旧 IID、错误构建指纹均被拒绝，正式 `bin/plugins` 不含测试协议。测试 DLL 仅在 `bin/ui-contract-fixtures`。
- 模态提示中销毁父视图和最后会话／目录后，插件代码租约安全返回并最终释放。真实测试发现并修复了宿主共享加载引用问题，见 [诊断证据](catalog-lifetime-diagnosis.md)：每个 loader 先显式 load 再 instance，避免第一目录释放时销毁另一目录仍使用的根对象。主体没有添加协议名称分支、字段或支持组合。
- [扩展说明](../../../../docs/plugin-ui-extension.md) 记录 SDK12 配套、模板／专用控件、文件内容／保存／应用、GUI 与后台边界，以及测试夹具的用途。

## TDD 与回归证据

沿用用户确认的公开 DebugSession、可控端点和真实 Qt 控件 seam。观察发送字节、操作、文件、会话状态及 UI 目标；没有依赖私有容器、内部对象名或精确像素。

| 行为 | 失败证据 | 通过证据 |
| --- | --- | --- |
| 独立 DLL 和参数视图 | [缺少插件](red-independent-plugin.txt) | [工厂／列／设备](green-independent-plugin.txt) |
| 专用控件提交受控操作 | [缺少行内入口](red-controlled-operation.txt) | [窗口及请求](green-controlled-operation.txt) |
| 插件文件编辑与明确应用 | [缺少文件页](red-plugin-file-page.txt) | [保存／应用／目标隔离](green-plugin-file-page.txt) |
| 专用诊断跟随启停 | [仍显示停止](red-diagnostic-state.txt) | [完整组合](green-plugin-contracts.txt) |
| 重叠目录生命周期 | [根 QObject 过早销毁](red-overlapping-catalog.txt) | [完整组合](green-plugin-contracts.txt)及最终两配置 |

早期 offscreen 的字体和窗口能力提示不作为原生视觉结论；最终使用 Windows Qt 平台。

| 最终配置 | 原生验证范围 | 结果 |
| --- | --- | --- |
| Debug | `plugin_ui_contract`、`session_lifecycle` | 2/2 CTest、22 QtTest 检查，0失败/跳过；[日志](windows-debug/ctest.log) |
| Release | 同上 | 2/2 CTest、22 QtTest 检查，0失败/跳过；[日志](windows-release/ctest.log) |
| Debug | 曲线浮动／停靠和最后目录租约 | 4 QtTest 检查通过；[日志](windows-debug/trend-lifetime.txt) |
| Release | 同上 | 4 QtTest 检查通过；[日志](windows-release/trend-lifetime.txt) |

每配置合计26项检查。UI12 本套为9项（含 init/cleanup），覆盖7条行为；未重复未改的曲线容量或 RTU 全量软件负载，UI13 仍负责整体双配置验收。原生日志无 QWARN、失败或跳过。

环境：Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17、CMake3.30.5、Ninja。沿用 `qt-llvm-debug` / `qt-llvm-release`，构建 `--parallel 3`，Windows 测试由 root 协调串行时段。未安装工具或修改全局环境。构建日志及宿主／示例 DLL 散列随本目录保存，构建产物不入 Git。

## 视觉与自检

已目视检查实际 Windows [场景参数窗口](windows-debug/scene-operation.png) 和 [专用文件编辑页](windows-debug/scene-file-editor.png)：白色参数／记录区域，目标与来源路径明确，枚举／复选控件、JSON 分组、保存和应用按钮完整可见。参数窗口中错误契约诊断来自刻意放入的两个拒绝夹具。截图是 QWidget 客户区，不冒称系统标题栏像素验收；原生窗口行为由 Qt 测试观察。

自检确认 UI12 只添加独立测试插件／测试／文档及必要通用加载器修复；保留另一分支的曲线目标与资源。`git diff --check` 通过。各证据文件的 SHA256 清单文本按 UTF-8/LF、图片按原字节核验。整体 Standards／Spec 独立审查仍由集成阶段执行。

限制：场景测试协议只接收完整的受控 ACK 输入块，不承诺实际设备或任意串流分帧。未访问真实串口、CAN 或硬件；不宣称 UPP/CAN、TCP、旧13硬件验收或旧19正式交付。UI13 正式产品用户操作验收仍单独进行。
