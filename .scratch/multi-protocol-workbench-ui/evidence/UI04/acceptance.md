# UI04 参数定义、分组与地址读取验收

日期：2026-10-23。分支 `codex/ui04-parameter-files`，基于集成 `eaba0d9`，完成前同步 `5f3c8a8`。任务：[UI04](../../issues/04-parameter-json-groups-read.md)。实现、测试、本文及任务勾选由同次 Git 提交固定，具体提交由集成索引记录。

## 实现与兼容

- Modbus 插件解释 `kind=modbus-parameter-definition`、整数 version=1 的外部参数定义，提供名称、类型、单位、说明和分组。无名称显示 PDU 地址，无分组进入默认组。文件不携带设备地址或观测值，点击时捕获明确目标。
- 选择文件不应用；“加载并应用到设备”后台读取、完整校验后替换指定设备声明，失败保留原内容和其他设备。只允许现有会话配置入口接受的停止状态，不暗中停止、重启或发送设备指令。
- 按数据区、PDU 起址及数量生成参数。范围或容量不合法时全部拒绝；重复地址保留已有定义。257 行生成通过，未继承原型 256 行限制。
- parameterRows 延续 UI02 地址行兼容并增加元数据，parameterFiles 保存来源引用，工作区相对路径往返可用。工作区保存的是主站参数声明，不是设备值，也不是从站模拟数据；恢复不热加载文件。旧从站外部数据启动规则未改。
- SDK10 增加 `SessionAccess.background`：与现有本地任务共享 4 个后台任务额度，在线程间传值，在会话线程完成；任务及回调释放前保留 DLL 租约，会话或视图销毁后不应用结果。所有插件按相同 IID/构建指纹重编。
- 表格按稳定参数 ID 记录持久模型索引，刷新不再对每行重复线性搜索整个表格；不改变插件列、动作或现有选区接口。

完整格式、读取预算及兼容规则见 [参数定义文档](../../../../docs/parameter-definition.md)。UI05 待提交值和 UI07 文件编辑保存不在本项实现范围。

## TDD 与实际验证

测试使用规格预先批准的公开会话与可控传输接口，以及真实 Qt 参数页操作；不访问真实串口，不依赖私有对象名或像素坐标。

1. [文件加载 red](red-file-load.txt)：实际界面缺少明确应用入口；补齐后设备17的“温度”行发出 RTU `11 03 00 6b 00 01 f7 46`，输入 `11 03 02 02 2b 38 f8` 后显示555及更新时间。
2. [范围生成 red](red-address-range.txt)：缺少数量入口；补齐后 65534+3 被原子拒绝，65534+2 可生成并从设备18的输入寄存器65535读取；设备19生成257行且不发送请求。
3. [来源引用 red](red-reference-relocation.txt)：保存工作区未重定位新引用；修复后相对路径往返成功，删除来源文件后仍恢复已应用主站声明、保持停止且不发送通信。
4. 错误 kind/version、非整数地址、越界、错误数据区、类型不匹配、非法分组类型、重复地址及缺失文件均保留目标与其他设备的参数配置。
5. [文件生命周期 red](red-file-lifetime.txt)：关闭视图和会话后后台任务未持有目录租约；SDK10 修复后租约保持至完成，再释放。4 个值任务同时占额，第5个拒绝；回调返回会话线程，异常以 error 返回。

完整 Debug 和 Release 均为13/13 CTest，249 项 QtTest 通过，0失败/跳过。原始日志见 [debug-ctest.log](debug-ctest.log)、[release-ctest.log](release-ctest.log) 与 debug/、release/；构建见 [debug-build.log](debug-build.log)、[release-build.log](release-build.log)。

最终自检清理了替换参数后已移除行的状态缓存，避免多次加载积累旧身份。随后重新编译 Debug/Release 并以 offscreen 运行本项8项定向检查，两者退出0，见 [Debug最终定向](debug-final-offscreen.txt)、[Release最终定向](release-final-offscreen.txt)。此前完整回归和截图使用 Windows 平台；最终定向使用 offscreen 是为了避免干扰并行 UI03 原生窗口的桌面/Z-order 验证，不能视作额外的原生窗口验收。

环境为既有 Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、C++17、CMake3.30.5、Ninja，使用 qt-llvm-debug/qt-llvm-release 预设；未安装工具或改变全局 PATH。定向复现使用 `ctest.exe --preset qt-llvm-debug -R parameter_definition_read --output-on-failure`；可通过 DEBUGGER_EVIDENCE_DIR 保存实际 Qt 客户区截图。

## 视觉、自检与未覆盖边界

[参数分组与真实读取](parameter-groups-read.png) 已目视检查名称、类型、说明、555、单位、时间及明确目标。截图由软件驱动的 Windows Qt 参数页生成，不是用户正式产品验收，不包含真实硬件互通。

自检确认：主体不解释 Modbus 参数字段；解析、生成与读取规则留在插件；后台完整校验后才配置；失败不写回；有界任务和 DLL 生命周期验证通过。`git diff --check` 通过。整体独立 Standards/Spec 审查由集成阶段完成，本文不冒称已进行最终独立审查。

散列清单仅固定本次采集日志、截图和原始结果字节；源码由 Git 提交固定。Git checkout 的 LF/CRLF 转换可能改变文本工作树散列，不将其误报为源代码差异。
