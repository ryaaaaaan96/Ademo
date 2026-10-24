# 14B UPP CAN 接收解析软件验收

日期：2026-10-24。实施分支codex/upp14b-receive，基线70e9301；复用Qt 6.12.0 / LLVM-MinGW Clang22.1.7 / CMake / Ninja和既有SDK14。没有安装工具链、改变桌面设置或启动原生窗口验收。

## 范围与依据

用户明确授权“先按照建议来做项目后续再进行修改”，采用[暂定接收规则](../../14b-provisional-profile.md)，保留[原始协议冲突](../../../../docs/protocols/upp-can-format.md)。14B覆盖CAN适用20/21/22/23/26/27/28的请求/响应及A0/A1/A2/A3/A6/A7/A8异常响应结构；不包含以太网专用功能、产品UPP发送或设备写入执行。旧15和TCP未启动，物理互通/负载未验收。

长度0=256、同ID/格式不交错、末片无填充、CRC-16/MODBUS、异常2字节、多字节字段大端及默认超时属于已确认/授权暂定选择，不能声称协议方已统一勘误。SOE只在用户明确指定soeModels且时标A1时按bit0→bit7解析CP56原始字段；其他模型仅保留字节，不假定工程值或时区。配置和使用见[UPP接收说明](../../../../docs/upp-receive.md)。

## 组件选择与边界

COMMS候选已核对：[官方说明](https://commschamp.github.io/comms_doc/)提供header-only字段/消息模板，适用于后续稳定格式，但不能替项目决定CAN重组、方向歧义和当前冲突。此次选择插件内部有界Reader及解析器，避免为暂定小范围加入依赖；不是宣称COMMS不兼容。未向宿主加入UPP名称判断、未扩展发送接口、未改变SDK ABI，宿主与插件仍按相同配置配套构建。

UPP插件通过正式Endpoint后的DebugSession输入接收原始帧，发布Message及全部sourceRanges；一个重组缓冲最大256字节、32帧，关联缓存最多256项且有超时，宿主原有消息/记录限额继续生效。CAN ID与设备地址分别保留，原始标志留在TraceRecord元数据；设备身份取报文。auto方向不以rx猜响应，双义PDU标ambiguous；显式配置方向或不重叠CAN ID方向列表后才能按唯一上下文关联。0x21关联核对数据长度，重复请求/双义同键输入使关联歧义，响应消费上下文；重组中止、超时、断线和停止清理重组与关联；CRC/PDU错误不发布成功解析。CRC错误或尚未认定为响应的畸形PDU不消费已有请求上下文，仍由超时或明确响应结束其生命周期；已关联但长度错误的0x21响应消费上下文并报告pdu-error。

## 开发与审查证据

实现检查点1c3f328，审查修复ea245f2。

- initial-red：旧版无UPP插件，公开会话启动失败；slice1-green接入通过。
- slice2-red/green：完整CRC与字段测试先失败，后通过。
- slice3-red/green：跨CAN帧握手回复与完整来源关联先失败，后通过。
- slice4-red/green：全部7功能请求/响应及7异常的静态向量先失败，后通过。
- slice5：测试初次moc宏解析失败已修正；随后发现受控输入异步交付，测试helper等待Qt事件后按公开结果检查，未修改生产断言。专用视图随后实现并通过。早期原始结果保留，不用未执行的新测试结果冒充绿灯。
- slice6：首次virtualcan在受限执行环境停留Starting，见virtualcan-sandbox-initial.txt；以批准的本机回环/临时文件权限运行相同offscreen测试成功。导出格式从测试误写JSONL改为既有公开API的jsonl；产品导出接口未改。
- Standards发现P2：已知请求后又收到同键双义报文时会误用旧上下文。红结果review-red.txt证明错误matched；修复为ambiguity标记，公开回归通过。
- Spec发现两个P2：分片超时未清关联、UPP样例沿用raw默认500ms导致默认100ms重组超时。前者在discard时清关联，后者UPP默认20ms、raw保持500ms。真实样例回归去掉显式interval，验证默认路径。review-green.txt最终35通过、0失败、0跳过；主代理安排两轴独立复核ea245f2：Standards原1项P2关闭，Spec原2项P2关闭，均无新增finding。

测试向量在tests/upp_vectors.h和upp_edge_vectors.h，是原文字段结构/暂定规则与独立PowerShell CRC计算的固定字节。已用Modbus已知01030000000A→C5CD作算法核验；UPP握手0550070120→4889、回复05500F012032304141FFFFFFFF→B77A。测试期望不调用生产CRC或私有解析器，输入不冒称真实设备抓包。

## 离屏界面与真实软件后端

独立can_sample_source --profile upp通过本机Qt virtualcan发送3帧，正式CAN后端生成2条UPP消息（请求1帧、回复2帧），CAN ID321但设备地址5。Qt离屏控件选择回复显示字段与两个来源分片；JSONL实际文件包含原始字节、元数据、Message和sourceRanges。debug/与release/保存upp-virtual-view.png、sample-source.jsonl、upp-session.jsonl。

主代理只读核验保存的图像：白底、消息/字段/来源可读，暂定规则及未验证设备提示可见。不是桌面截图验收或系统输入自动化。其余边界使用可控QCanBusDevice经正式can_endpoint进入公开DebugSession：所有功能、7/8/9/256字节、CRC跨帧、CRC错误、PDU长度、方向歧义、上下文消费、超时、重复片、末片多余字节、停止/重连旧回调、记录容量、配置拒绝、工作区恢复停止及RTU/CAN并行隔离。

## 完整回归

| 配置 | CTest套件 | QtTest通过 | 失败 | 原生专用跳过 | 用时 |
| --- | --- | --- | --- | --- | --- |
| Debug offscreen | 24/24 | 403 | 0 | 4 | 155.79秒 |
| Release offscreen | 24/24 | 403 | 0 | 4 | 117.55秒 |

UPP专项各35项通过，0失败/跳过。默认windows预设保留但本轮未运行，原生菜单/拖动4项仅在非windows QPA下跳过，不记为通过：titleMenuDocksWithoutExtraRowOrRestartingSession、nativeWindowMoveDropsIntoEmptyWorkbench、nativeMoveCancellationAndOcclusionKeepFloating、nativeMoveCanTileOnEverySideOfAnotherSession。没有执行桌面操作。

全量构建日志debug-build.log、release-build.log及release-build-final.log；完整回归日志debug-ctest.log、release-ctest.log；两配置各24份QtTest原始结果保留在debug/和release/。test-environments.json核验48个测试组合均offscreen/software并读取现有系统字体。binary-sha256.json记录宿主、UPP/CAN/RTU配套DLL、样例源和UPP测试二进制。

14B暂定软件版完成，实际设备协议规则与物理验收继续后置。旧14整体不标全部完成；旧15与TCP保持未启动。最终合入由主代理记录。
