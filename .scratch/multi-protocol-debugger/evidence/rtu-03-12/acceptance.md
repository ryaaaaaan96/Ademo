# RTU 03–12 最终软件集成验收

日期：2026-10-23，Asia/Shanghai。本轮完成当前实施索引中的 03–12；续行先关闭 09 修复，再依赖实施 10、整合 12，逐项合入 `codex/rtu-03-12-integration`。01/02 原验收与提交已核验，不重复实施。13 等待真实硬件条件，14–19 未启动。

## 基线与独立实施

共享基线 `fb14cb8d0bd82c29e16b41191d36eeba736abd7d` 已将规格、当前任务、设计、AGENTS、工具链和必要配置纳入 Git，构建产物排除。`/.worktrees/` 也已忽略。独立实施 worktree/分支为 rtu-master / codex/rtu-master、rtu-slave / codex/rtu-slave、rtu-records / codex/rtu-records、rtu-data-edit / codex/rtu-data-edit；依赖按验收证据和提交解除，不依据 ready-for-agent 标签判断完成。

| 任务 | 主要实施提交 | 分项证据 |
| --- | --- | --- |
| 01/02，仅核验与回归 | `f5b962d`、`1fc90db` | [01](../01/acceptance.md)、[02](../02/acceptance.md) |
| 03 四区读取 | `dccb047` | [03](../03/acceptance.md) |
| 04 显式写入 | `ee4a71c`、`176e1bb` | [04](../04/acceptance.md) |
| 05 读取重试与轮询 | `c77a9a6` | [05](../05/acceptance.md)、[05/06 组合](../integration05-06/acceptance.md) |
| 06 外部 JSON 从站读取 | `dd39760`、`0943bc2` | [06](../06/acceptance.md) |
| 07 从站写响应 | `48df1a6` | [07](../07/acceptance.md) |
| 08 编辑、导入与保存 | `0559225` | [08](../08/acceptance.md) |
| 09 并行会话与恢复 | `77fca54`、`95ff916` | [09](../09/acceptance.md) |
| 10 工作区与插件容错 | `b97d660`、`963b78c`、`6797b25` | [10](../10/acceptance.md) |
| 11 查询与导出 | `a4dec16` | [11](../11/acceptance.md) |
| 12 有界磁盘记录 | `522336e`、`8be3807`、`8bd8c60`、`4c7d651` | [12](../12/acceptance.md)、[09/12 组合](../integration09-12/acceptance.md) |

整体审查后的最终运行源码为 `762d85d4351d90b9bb3765e00f967d51a95481e2`，包含停止写入终态修复 `6d69845`、缩容清理 `1dd3748`、公共 RTU 函数 `6b12125`、视图过期恢复 `2433ecd` 与绑定扩展保留 `9b062ec`。后续收尾提交只回写文档、任务与本目录证据。

## 环境与复现

复用 Windows x64、Qt 6.12.0 llvm-mingw_64、LLVM-MinGW Clang 22.1.7、C++17、CMake 3.30.5、Ninja 1.12.1；没有安装新工具或修改全局 PATH。Qt 自身为 Release，产品、插件及测试分别配套构建 Debug/Release。SDK8 使用版本化 IID 与构建指纹，旧插件需配套重编译。

```powershell
./scripts/verify.ps1 -EvidenceId rtu-03-12
```

| 检查 | Debug | Release |
| --- | --- | --- |
| 配置/构建 | [configure.log](qt-llvm-debug/configure.log)、[build.log](qt-llvm-debug/build.log) | [configure.log](qt-llvm-release/configure.log)、[build.log](qt-llvm-release/build.log) |
| CTest | [11/11 通过](qt-llvm-debug/ctest.log) | [11/11 通过](qt-llvm-release/ctest.log) |
| QtTest 合计 | 238 passed、0 failed、0 skipped | 238 passed、0 failed、0 skipped |
| 证据/产物 SHA256 | [清单](qt-llvm-debug/sha256.txt)，57 项逐条重算一致 | [清单](qt-llvm-release/sha256.txt)，57 项逐条重算一致 |

每配置 238 项包含 11 个套件的初始化/清理 22 项和参数化行，行为检查合计 216 项，不声称 238 个独立测试函数。套件计数依次为恢复 14、从站编辑 10、生命周期 13、读取 43、轮询 17、写入 36、从站读取 29、从站写入 25、回溯 12、工作区 13、磁盘记录 26；原始 `*-results.txt` 均保存于各配置目录。

## 审查、组合修复与证据

[整体 Standards / Spec 报告](review.md) 固定基线执行并独立复核最终源码，两轴均无剩余问题。分项与最终回归同时覆盖：

- 09：多会话未保存确认结束后不泵事件复查 dirty/busy，保留后续确认期间再次修改的数据。
- 12：初始化失败自动结束后台任务，避免空转占用容量；归档与实时查询统一多对多观察关联；连接断开操作终态、不确定结果及 recovery 配置落盘。
- 停止：先由协议发布终态，再由宿主兜底，公开结果、归档和 JSONL 均保留已提交未确认写入的 uncertain；本地后台文件操作保留。
- 10：未知串口枚举不变成默认值；从 16 页恢复空工作区后可新增；工作区替换与协议切换确认期间销毁窗口均重新检查 QPointer。
- 整体：缩容同时淘汰发送状态，过期操作不锁死视图；从站保存/导入保留设备绑定扩展字段及另一设备，工作区往返不丢失。

独立修复记录：[停止终态](../stop-uncertain-reviewfix/acceptance.md)、[缩容清理与失败对照](../trace-eviction-reviewfix/acceptance.md)、[RTU 共用函数](../rtu-wire-refactor/acceptance.md)、[过期界面](../operation-cache-reviewfix/acceptance.md)、[绑定扩展](../binding-extensions-reviewfix/acceptance.md)。各任务 Comments 保留原验收历史，并追加本轮修复与最终集成证据。

本目录的原生 Qt Widgets 截图已目视检查：工作区缺插件提示与禁用范围、主从会话和恢复状态、数据编辑/保存、写入不确定提示、回溯来源及磁盘归档导出均可读。输入来自可控传输及固定向量，不是硬件联调截图。

## 实际边界

未连接独立 Modbus 对端或真实串口设备；没有新增 CAN/UPP/TCP 产品能力。真实拓扑、驱动拔插、线路时序、独立互通、持续吞吐和清洁 Windows 交付仍由后续任务验收。慢记录后端由受控 seam 阻塞，不等同物理磁盘写满、断电或硬件持久性验证。

13 保持 needs-info，等待真实硬件和对端条件确认；14–19 未启动。03–12 的软件验收不能替代 13，也不构成任意负载无损、所有串口拓扑或长期吞吐保证。
