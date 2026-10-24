# UPP CAN 发送交互（15 暂定软件版）

本轮按用户授权的[暂定规则](../.scratch/multi-protocol-debugger/15-provisional-exchange-profile.md)实现全部 CAN 适用功能。规则仍可修订，软件对端不是目标硬件协议认证。TCP 未启动。

## 操作

在现有工作台选择 UPP CAN 交互、CAN 和 master。observer 继续提供14B观察。启动会话，在插件左侧选设备1..255及功能，编辑 JSON 参数后点击一次发送。面板给每个功能提供参数样例；写入使用原始十六进制字节，不推断数据类型或端序。当前请求未结束时拒绝再次提交，不自动轮询、不自动重试写入。状态标签显示最近一次面板提交结果；消息表可独立选择消息并查看字段和来源。

功能20握手，21单单元读，22多单元读，23增量读，26单单元写，27多单元写，28读写组合均可主动请求。请求/正常应答/异常、CRC及分片按暂定规则验证。2字节异常按设备、序号和功能关联；6字节异常还验证模型及偏移属于请求读写范围。20/23没有具体目标范围，不能从其请求确定异常地址。

## 配置与结果

`deviceRoutes` 是以十进制设备号为键的 JSON 对象，例如 `{"5":{"txId":801,"rxId":1110,"extended":true}}`。ID 数字使用 JSON 十进制；默认发送和接收 ID 均等于报文设备号、标准帧。设备身份始终取报文。标准ID上限0x7ff，扩展上限0x1fffffff。默认分片间隔20ms、应答超时1000ms，可分别用 `sendGapMs` 和 `responseTimeoutMs` 配置；间隔从前片本地完成计，应答期限从最后一片本地完成计。无报文交错或自动补发。

序号按设备1..255循环，重连保留，明确停止后重新启动重置。近期已结束身份在应答期限内隔离，容量有界。回显/错误ID/格式/身份/CRC/结构/不匹配数据不确认请求。最后一片提交后的早到响应等待本地完成。部分写入已接受但发送失败、超时、停止或断线时结果不确定，不能推断设备未执行。

操作 `sendAccepted` 表示至少一片被接受，`localSent` 只有全部片本地完成才真；协议成功还需匹配响应。TX/RX消息保留原始帧来源，分片序号不冒充重试次数。完整发送结果还有 `acceptedAny`、`localFramesCompleted`、`frameCount`、`allFramesLocal`、`provisional`；JSONL/CSV沿用宿主导出。

## 独立软件对端

构建后的 `upp_virtual_peer.exe --interface can0 --device 5 --duration 60000` 在本机 Qt virtualcan 等待请求，默认收发ID5；`--rx-id 321 --tx-id 456 --extended` 指十六进制ID。它有独立CRC、重组和模型存储，支持全部7功能及范围异常。测试模型1..4、字节偏移1..64，初始值等于偏移。28按测试约定先读旧值、再原子写入，不能视为设备规范结论。stdout 的 READY 与请求/应答 JSONL 可留作软件证据。工具不会自动启动产品或桌面。

后台验收仅 Qt offscreen/software，见[15验收](../.scratch/multi-protocol-debugger/evidence/15/acceptance.md)。实际适配器、硬件ACK、总线仲裁、实机时序及真实设备交互仍未验收。
