# 05/06 SDK5 集成验收

日期：2026-10-22。合并分支为 `codex/rtu-master`，父提交分别为05的 `c77a9a6` 与含06的集成 `e0d3ce8`。本项只解决05/06共享接口冲突并回归，不重复实施01/02、不启动07/08/09或13–19。原05、06验收证据保持原状。

## 合并内容

SDK5显式合并05的协议调度 `submit`、配置发布 `publishConfiguration`、配置访问 `config/configure` 与06的 `sendPassive`、`snapshot`、`prepareInBackground`。保留 shared ProtocolSession、后台准备任务/插件租约及取消代次检查；主站仍同步参数校验，从站后台读取全部外部JSON后才打开端点。保留主从角色、主站观测/从站模拟数据页与完整从站实现。

主站的 `retry-wait` 仍为未完成状态；不透明重试令牌、每次发送metadata与旧尝试完成隔离保留。从站被动应答仍走统一记录、单项有界发送和1秒期限。高位令牌统一由宿主 `allocateSendToken()` 产生，删除独立被动ID计数，避免重试与被动应答跨角色/运行重用令牌。逻辑操作ID和传输令牌保持不同含义。

合并审阅发现被动发送完成若在定时poll之前到达，原代码可把超过1秒期限的结果记录为成功。现与主站一样按回调实际观测时刻判断截止：迟到完成为发送失败，不重新延长期限、不自动重发。

README保留05说明并补齐06外部数据格式、启动和应答边界；未修改实施索引或08文件。

## 复现与结果

沿用Windows x64、Qt6.12.0 LLVM-MinGW、Clang22.1.7、C++17、CMake3.30.5、Ninja1.12.1；宿主/协议/传输/夹具按SDK5和同配置配套重建。

```powershell
./scripts/verify.ps1 -EvidenceId integration05-06
```

| 配置 | 配置、构建与CTest | Qt Test汇总 |
| --- | --- | --- |
| Debug | [配置](qt-llvm-debug/configure.log)、[构建](qt-llvm-debug/build.log)、[6/6通过](qt-llvm-debug/ctest.log) | 生命周期13、四区读取43、写入36、轮询16、从站29、回溯10，共147 passed、0 failed、0 skipped |
| Release | [配置](qt-llvm-release/configure.log)、[构建](qt-llvm-release/build.log)、[6/6通过](qt-llvm-release/ctest.log) | 生命周期13、四区读取43、写入36、轮询16、从站29、回溯10，共147 passed、0 failed、0 skipped |

147包含6套件初始化/清理12项，行为检查行135项。每目录的 `*-results.txt` 保存逐项结果，`sha256.txt` 保存实际产物及证据哈希；截图随各套件生成。旧05/06证据未被覆盖，构建产物保持忽略。

新增公开会话回归 `retryAndPassiveTokensShareOneAllocatorAcrossRuns`：在主站发生读取重试后停止，同一宿主对象以外部JSON启动从站，接收固定03读取请求并验证正确响应、被动令牌与先前重试令牌不同；先前运行的本地结果不污染当前从站；当前被动完成正确记录为response-sent。随后不调用poll，直接让受控时钟超过被动发送期限再报告完成，结果必须response-send-failed且原因含超时。

最初该新增测试把从站消息状态误写成宿主trace kind `local-sent`；按06公开消息契约修正为 `response-sent`。最终日志是修正及期限回归后的完整双配置结果。冲突标记检查与非原始log差异空白检查通过。

## 审查与限制

实施者逐项核对共享SDK、工厂角色、异步准备、后台租约、快照、主站重试、队列上限、被动期限及旧代次保护。Standards/Spec独立整体审查仍由集成分支统一执行。模拟、固定向量和原生Widgets测试不构成真实串口/独立Modbus对端证据，13继续等待真实硬件条件。
