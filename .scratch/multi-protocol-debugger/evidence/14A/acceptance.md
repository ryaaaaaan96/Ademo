# 14A 原始经典 CAN 帧观察软件验收

日期：2026-10-24。独立实施分支 `codex/can14a-observation`，基于 UI13 合入提交 `9704eba`；沿用 Windows 11 x64、Qt 6.12.0、LLVM-MinGW Clang 22.1.7、C++17 和已有 CMake/Ninja 预设。

## 当前记录

实现检查点为 `69c641b`；真实界面发现的协议切换刷新问题已以 `da04758` 修复，`816047f` 同步集成分支 `09e44f3`。8bc1199加入离屏支持，f9f5f45修复审查发现的增量fixture同步问题，f8a64b5完善离屏虚拟屏和字体配置。最终软件验证结果及明确未执行条件见下文，物理验收不在完成范围。

## 实现与公开验证入口

- 正式 `can` 传输通过 QCanBus/QCanBusDevice 接收。`can.raw` 插件读取统一原始记录，专属表按 CAN ID、帧格式及类型筛选，CSV/JSONL 捕获点击时的有界快照并后台导出，不创建假 UPP Message/读取 Operation。
- SDK14 使用通用 requiredTransportCapabilities 和 supportsTransport 匹配能力。frame 输入保留边界和零长度，byte-stream 保留既有分块。recordStatus 提供真实归属、容量、过期与磁盘丢失/轮换；preferredForNewSession 保持 Modbus 默认入口。发送接口未扩展，观察插件拒绝业务操作。
- TDD入口沿用规格确认的 DebugSession 公开方法、正式 Endpoint 和 Qt 控件。首个CAN会话测试 [红结果](initial-red.txt) 证明旧版未接入；专属表 [红结果](view-red.txt) 以及独立样例工具 [红结果](sample-red.txt) 分别在实现前失败。实现后专项覆盖正常/扩展/零长、远程/错误/未知/无效/FD、筛选不改底层采集、CSV/JSONL来源与字节、容量过期、stop/start/重连旧回调、CAN/RTU隔离、工作区恢复停止和独立DLL导出生命周期。
- 错误帧使用受控 QCanBusDevice 注入，经正式 createCanEndpoint 转换及 DebugSession，再观察记录和界面；未直接测试私有转换函数。独立进程 can_sample_source 则通过本机实际 virtualcan 后端进入正式会话，二者证据不混写。

## 实际界面反馈修复

首次桌面操作从默认 Modbus 切换至 CAN 后，协议/传输/角色均已兼容，但“启动会话”仍禁用。原因是切换传输先用旧角色触发了一次状态刷新；随后角色在 QSignalBlocker 下更新，没有最终刷新。修复在插件视图重建后，检查视图仍存活并刷新最终组合状态。公开 Workbench 回归通过连接配置入口切换、检查按钮可用、实际点击启动/停止、切回 Modbus，见 [红结果](protocol-switch-red.txt)与 [绿结果](protocol-switch-green.txt)：专项最终11通过、0失败/跳过。

测试专属DLL复制到构建目录的独立fixture，再验证后台导出期间销毁视图/会话/外部catalog引用，文件仍完成且catalog租约最终释放，避免Windows延迟释放DLL导致临时目录残留警告。

本机启动提供 `run-can-sample.cmd`，默认can0、200帧、250ms，绕开PowerShell脚本执行策略。首次自动化 `--help` 遇到 Qt 在无控制台时弹出原生帮助窗，已仅终止该help进程；工具改为 parse + stdout 输出，后续验证结果另列，不把被中断尝试记为成功。

## Qt virtualcan 实测边界

主代理运行独立双设备探针，源码 [virtualcan-probe.cpp](virtualcan-probe.cpp)、[构建文件](virtualcan-probe-CMakeLists.txt)。该保存源码的接收端 CanFdKey=false；第一份日志将该行改为true，其余一致。

- [接收端启用FD](virtualcan-probe-fd-enabled.log)：标准/同ID扩展/零长度数据/RTR四字节占位与零长RTR实际往返；ErrorFrame 的 BusOff64/BusError128 被该后端变为DataFrame/id0/error0，即使 writeFrame=true 也不能算错误帧保真。
- [经典接收端](virtualcan-probe-classic-receiver.log)：接收端 CanFdKey=false 仍收到带FD标记的完整12字节帧。因此产品显式标为不支持并保留完整输入，不依赖配置过滤或截断。
- RTR payload 仅表示期望响应长度，线上数据bytes为空，Qt占位内容/长度与来源单列metadata。ErrorFrame仲裁ID和标准/扩展格式不可用，后端原值与错误位继续保存。Qt不提供独立DLC，字段标不可用。
- 宿主观测UTC/单调时间与后端seconds/microseconds分列；不宣称硬件时钟、时基或精度。虚拟测试来源明确标记。

## 运行与范围

操作流程和样例命令见 [CAN观察说明](../../../../docs/can-observation.md)。样例独立、显式启动，无文件回放入口；工作区恢复不自动启动端点或样例。软件观察不保证物理静默模式。

14A不实现14B UPP重组/CRC/字段解析、旧15业务发送或TCP；旧13及CAN实际硬件、驱动、总线时序和负载验收继续后置。旧14整体仍保留 needs-info，不能把14A软件完成记为整项UPP接入完成。

## 无桌面验收约束与离屏环境诊断

2026-10-24 用户明确要求“不要操作我电脑来验收”。随后停止全部 Computer Use、原生窗口操作和真实鼠标输入；完整自动化采用独立 Debug/Release offscreen 构建及 QtQuick software。保留默认 windows 预设与四项原生菜单/拖动断言，实际平台非 windows 时仅这四项显式 QSKIP，不将跳过记为通过。

首轮 Debug 离屏21/23套件通过，窗口恢复与居中两项失败，见 debug-offscreen-ctest-initial.log、recovery-offscreen-initial.txt、workspace-offscreen-initial.txt。Qt offscreen 默认800×800虚拟屏不足以容纳测试1080×810窗口，恢复代码正确夹取到798×774；居中同样受屏幕边界限制。配置 tests/offscreen-platform.json 为1920×1080虚拟屏后，两项原断言与CAN专项全部通过（offscreen-config-targeted.log），没有修改生产代码或弱化测试。初次绝对Windows路径被QPA的冒号分隔导致启动失败（offscreen-config-drive-path-failed.log），改为构建目录相对配置路径。配置依据为 [Qt offscreen 实现](https://raw.githubusercontent.com/qt/qtbase/6.10/src/plugins/platforms/offscreen/qoffscreenintegration.cpp)，在本机Qt6.12实际运行确认；不改变真实显示器。

offscreen 使用 FreeType 且不会自动发现 Windows 字体，初始截图方框保留于 can-offscreen-before-font-directory.png。仅离屏测试设置 QT_QPA_FONTDIR 为已有 Windows Fonts 目录后中文可读；不安装字体或改变产品字体。主代理已只读复核最终离屏截图、实际CSV与JSONL，确认白底、四种实际接收帧、筛选单帧0x123及8字节、来源和sessionId一致。

历史桌面操作见 desktop/desktop-observations.md 和截图：主代理实际验证切CAN启动按钮可用、启动后独立样例12帧全部进入会话、ID123筛选3帧且底层13条记录保留、原生最大化。桌面CSV/JSONL导出未完成，后由真实Qt离屏控件触发导出并校验实际文件内容。工具缓存索引问题及窗口正常退出不记录为产品崩溃；用户限制后未继续桌面操作。

## 两轴代码审查

69c641b 的 Standards / Spec 初审均无finding。8bc1199 最终复审 Spec无finding，Standards发现1个P2：CAN寿命测试fixture的DLL复制绑定测试POST_BUILD，插件单独增量重链时fixture可能陈旧。f9f5f45 改为依赖两个DLL的独立同步target并使用copy_if_different；fixture-plugin-only-build.log证明未重链测试也执行复制，fixture-plugin-only-hashes.json证明两个源DLL与fixture SHA256一致。Standards复核关闭该P2、剩余0，Spec复核f9f5f45无新增finding。


f8a64b5 的最后离屏配置增量已由 Standards 复核，无新增finding。变更仅影响offscreen预设，原生断言和产品代码未改变。

## 最终回归结果

| 配置 | 串行CTest套件 | QtTest通过 | 失败 | 原生专用跳过 | 用时 |
| --- | --- | --- | --- | --- | --- |
| Debug offscreen | 23/23 | 368 | 0 | 4 | 86.23秒 |
| Release offscreen | 23/23 | 368 | 0 | 4 | 114.29秒 |

全部使用原有Qt/LLVM-MinGW工具链，配置和构建日志保留；最后虚拟屏/字体调整只重新配置CTest，无产品二进制变化。全量日志为 debug-offscreen-ctest.log、release-offscreen-ctest.log；每套QtTest原始结果与实际CSV/JSONL/PNG在 debug-offscreen/、release-offscreen/。offscreen-test-environment.json列出全部46个配置/测试组合的实际环境；offscreen-binary-sha256.json记录配套宿主、样例和CAN插件二进制。CAN专项每配置11项通过，未跳过；全量包含现有RTU、多会话、恢复、后台导出/DLL寿命及QML软件渲染。

4项跳过为 titleMenuDocksWithoutExtraRowOrRestartingSession、nativeWindowMoveDropsIntoEmptyWorkbench、nativeMoveCancellationAndOcclusionKeepFloating、nativeMoveCanTileOnEverySideOfAnotherSession，仅windows QPA可验证。不能据23/23套件通过宣称这些原生交互本轮通过。UI13既往原生验证与此分开记录。

修复后的样例帮助在offscreen环境输出stdout并正常exit0，见 sample-help-stdout.txt；sample-cmd-help.txt是之前被中断的失败尝试，保留供追溯，不作为成功证据。

本轮14A软件切片完成，旧14整体needs-info保留。合入集成由主代理执行并记录最终提交；14B、旧15、TCP与物理验收均未启动。
