# 原始经典 CAN 帧观察（14A）

本切片提供 `can` 传输与 `can.raw` 观察插件，不解析 UPP、不提供业务发帧。继续使用正式窗口、左侧会话操作和白底通信记录；CAN ID 不充当设备地址。

## 无硬件操作

1. 按 README 构建并启动配套产品。在会话“连接配置…”选择“CAN 原始帧观察”，传输自动匹配“CAN（Qt SerialBus）”、角色 `observer`。
2. 后端选 `virtualcan`，接口填 `can0`（可用 `can0` 至 `can9`），关闭配置窗后明确点击“启动会话”。
3. 双击根目录 `run-can-sample.cmd` 显式启动独立样例源（默认can0、200帧、间隔250ms）；它沿用本机Qt环境且不需要PowerShell脚本执行权限。也可在终端运行下列可执行文件；样例不会随产品或工作区自动启动：

```powershell
$env:PATH = 'C:/Qt/6.12.0/llvm-mingw_64/bin;C:/Qt/Tools/llvm-mingw2217_64/bin;' + $env:PATH
.\build\qt-llvm-release\bin\can_sample_source.exe --interface can0 --count 200 --interval 250
```

4. 样例每四帧依次是标准数据 `0x123`、扩展数据 `0x01ABCDE3`、零长度数据 `0x456`、期望长度4的远程帧 `0x321`。主表显示宿主观测时间、方向、ID、格式/类型、实际可用字节和期望响应长度；底部记录仍归属同一会话。
5. CAN ID 筛选按十六进制（可写 `123` 或 `0x123`），可组合格式和类型。筛选只影响主表及“当前筛选的保留帧”导出，不改变采集和磁盘记录。
6. 选择“当前筛选的保留帧”或“全部保留帧”、CSV或JSONL、文件路径后点击“导出快照”。后台保存点击时的有界快照；CSV包含字节、ID、格式/类型及完整元数据，JSONL保留原始记录ID/连接代次/会话/宿主时间/后端元数据。summary包含正常过期、磁盘丢失和轮换计数，导出不恢复已丢失内容。
7. “停止并释放端点”关闭正式后端；保存并打开工作区后保持停止。虚拟样例源到达count后自行结束，可重复手动启动。恢复窗口布局不会启动样例源。

## 后端信息边界

- 正式接收经 Qt SerialBus `QCanBusDevice`，每次输入保持一帧边界，零长度也进入统一记录；近期容量沿用会话记录上限。
- Qt RTR payload 是用于表示期望响应长度的占位字节，原始记录bytes为空；占位表示和长度来源保留在metadata，不能冒充线上数据。
- Qt ErrorFrame 的 `frameId()` 固定返回0，因此实际ID和标准/扩展格式显示不可用，仍保留Qt原值、错误位和后端错误数据。独立DLC未由Qt API提供，标记不可用，不拿payload长度编造DLC。
- 宿主 `observedUtc`/`monotonicUs` 与 Qt 后端 timestamp 分开保存。后端时基/精度未经确认，不标为硬件时间。FD/未知/无效帧保留完整输入和实际标志，标为不支持，绝不截断伪装经典帧。
- Qt6.12 `virtualcan` 实测可传标准、扩展、零长度和远程帧；它会把错误帧变为ID0的数据帧，不能用虚拟链路证明错误帧保真。错误帧由受控QCanBusDevice经同一正式转换和DebugSession入口补测；证据分别记录。
- `virtualcan` 使用Qt内部本地转发服务，不代表新增产品TCP协议功能。硬件CAN后端只使用本机已安装Qt插件与其默认配置；型号、驱动、总线速率、时序和静默模式均未做物理验收。“只观察”不保证电气层监听模式。

依据：[Qt VirtualCAN](https://doc.qt.io/qt-6/qtserialbus-virtualcan-overview.html)、[QCanBusFrame](https://doc.qt.io/qt-6/qcanbusframe.html)。14B、旧15和TCP留待后续授权及资料条件。

## 自动验收不操作桌面

2026-10-24用户明确要求后续验收不操作其电脑桌面。Debug/Release使用独立`-offscreen`预设和QtQuick软件后端，正式virtualcan接收后再通过Qt控件筛选、导出并校验文件内容。离屏渲染图不是用户桌面截图。4项Windows原生菜单/拖动用例只在windows QPA执行，本轮离屏明确跳过；UI13此前的原生通过记录保持历史事实，不冒充本轮重跑。

离屏预设使用 `tests/offscreen-platform.json` 的1920×1080虚拟屏及系统已有字体目录，避免默认800×800虚拟屏对窗口恢复/居中断言的夹取；不修改真实显示器分辨率。Windows原生菜单/拖动四项在离屏平台显式跳过，原断言保留。
