# 15 暂定 UPP/CAN 发送软件验收

范围按用户授权的[暂定交互规则](../../15-provisional-exchange-profile.md)，沿用14B与既有Qt/LLVM-MinGW；本轮只做后台offscreen/software验证。没有操作用户桌面、没有原生窗口人工验收或硬件验收，没有启动TCP。

## 提交与环境

基线e834d60；独立分支codex/upp15-exchange，实施8cb9d6d，审查修复fc61595，补充公开隔离/工作区回归92a9c95。最终产品代码为fc61595，最终测试代码92a9c95；后续提交仅文档及证据。合入集成分支由主代理另记。

Windows x64、Qt6.12.0、LLVM-MinGW Clang22.1.7、CMake3.30.5、Ninja沿用已确认环境。配置仅qt-llvm-debug-offscreen及qt-llvm-release-offscreen；Qt图形平台offscreen、QT_QUICK_BACKEND=software，字体C:/Windows/Fonts。本机virtualcan及文件任务在后台正常权限下运行，未更改系统设置。

## 最终回归

Debug完整构建成功；`ctest --preset qt-llvm-debug-offscreen --output-on-failure` 25/25通过，139.54秒。QtTest合计435通过、0失败、4跳过（既有原生窗口用例在offscreen明确跳过）；UPP发送专项32通过、0失败。见[CTest日志](debug-ctest.log)、[逐套结果目录](debug/)、[汇总](debug-summary.json)。未把跳过计为成功。

Release完整构建成功；`ctest --preset qt-llvm-release-offscreen --output-on-failure` 25/25通过，119.02秒，QtTest同为435通过、0失败、4原生跳过，UPP专项32通过。见[Release日志](release-ctest.log)、[逐套结果](release/)、[汇总](release-summary.json)，以及[离屏截图](release/upp-exchange-view.png)、[JSONL](release/upp-exchange.jsonl)、[独立对端日志](release/peer-output.jsonl)。两配置串行运行软件对端，未并行争用virtualcan测试频道。

`git diff --check`通过；92a9c95是最终运行测试源码，产品仍为fc61595。集成分支为本分支祖先，待主代理合入。[sha256清单](sha256.txt)覆盖本验收文件以外的日志、截图和导出，可核验证据文件一致性。

## 软件覆盖

- 所有7功能20/21/22/23/26/27/28主动请求精确字节、正常回复及异常回复；Modbus CRC、报文256上限及预期应答大小、单位/设备/原始hex校验。
- 逐片等待本地完成、帧间隔、末片后应答期限、早到响应、回显与错误ID/格式过滤、PDU目标匹配、每设备序号回绕及隔离。
- 发送接受不代表完成；多片中间localSent仍false，最后完成才true；部分写入后拒绝/停止/断线/超时标不确定，不重试或重连补发。旧代回调不影响新代。
- 通用SDK15 SendReceipt/sendUnit及finalUnit，不按协议名硬编码宿主；全部宿主和插件DLL配套重编。原SDK兼容拒绝fixture与always-run同步继续在session_lifecycle通过。
- CAN端点同步/异步完成、32项容量与拒绝、第一个最终结果和关闭一次结算；999ms到达的本地完成即使1001ms才派发仍按实际观察时间处理。
- 新master工作区保存/恢复role、对象路由、间隔30ms及超时1500ms；恢复保持Stopped且不创建端点。UPP握手与RTU读取2寄存器在同进程各自完成、来源隔离，停止UPP不影响RTU。
- 独立upp_virtual_peer通过正式Qt virtualcan后端响应所有7功能和范围异常，使用不同收发扩展ID0x321/0x456。首个握手由离屏插件按钮触发，其他请求经公开会话API完成；有真实后端帧、消息、操作和JSONL来源链。软件对端模型有限，28采用先读旧值再写的测试选择。

## 界面证据

[Debug截图](debug/upp-exchange-view.png)与[Debug导出](debug/upp-exchange.jsonl)、[独立对端日志](debug/peer-output.jsonl)来自offscreen实际控件和正式接收链。白底表格、原始hex参数、单次发送/不自动重试提示、最近面板提交状态、协议异常字段及来源帧均可读。左侧长参数说明已换行，最近提交状态不与选中消息混淆。截图不等同于用户桌面或原生窗口验收。

## 两轴代码审查

Standards与Spec子代理并行审查e834d60..8cb9d6d；fc61595复审均通过，无遗留finding。

- Standards P2：中间片误标整个Operation.localSent；以通用finalUnit修复，保留RTU默认单帧语义。
- Standards P2：CAN额外事件队列延迟完成观察时间且完成结果可被覆盖；改为接受后即时通知、首结果固定、同步signal暂存至writeFrame返回。
- Spec P2：6字节异常未校验请求目标；补模型/偏移所属读写单元验证，2字节通用异常保持身份关联。

[review-red](review-red.txt)四项断言旧版失败；[review-green](review-green.txt)对应四项及初始化收尾全部通过。最终完整回归再次覆盖。

## 过程证据解释

初始master不支持、真实CAN未发送、缺少面板按钮、缺独立peer分别有initial-red/slice2-red/view-red/peer-red及对应green。现有向量扩展是第一切片后补充，不声称每条均先红后绿。

peer-green-build保留过一次QCommandLineOption初始化编译错误；peer-fixed及final-extra-tests保留受限沙箱本机virtualcan/文件目录失败，正常后台权限重跑通过。debug-first-full-ctest保留新增序号测试没有完成本地发送触发32项限流的夹具错误；修正为每次本地完成后等待应答超时。workspace-red/workspace-test-diagnosis记录新增测试误把closeSession当删除而取错会话索引，以及RTU状态期望写错；改用addSession实际索引和RTU既有success状态。没有将这些测试错误伪装成产品修复；临时探索的宿主改动已撤销。

## 后置边界

暂定序号/响应/超时/28顺序等仍待目标设备规则确认。真实CAN适配器、总线仲裁、物理ACK、实机时序、设备写后读取及硬件RTU并行未验证；旧15整体保留needs-info，13物理验收后置。TCP未启动。
