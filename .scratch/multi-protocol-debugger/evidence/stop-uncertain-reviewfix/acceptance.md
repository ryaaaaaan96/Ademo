# 04 / 08 / 12 停止操作终态审查修复

日期：2026-10-23。独立 `rtu-master` worktree / `codex/rtu-master` 分支，先快进至集成 `a160710`。只修复集成审查发现的停止终态丢失，不修改任务索引，不启动其他功能。

## 问题与修复

宿主原先先把未完成操作设置为finished，再调用协议stop。RTU已提交但未确认写入发布的cancelled/uncertain被宿主publish忽略，公开operation与磁盘归档缺少不确定标志，界面自行推导不能修复公开事实。

retire先使连接回调代次失效、撤销恢复及定时，再在非preparing状态调用协议stop，允许协议发布自身取消消息及终态；对仍未结束的操作执行宿主fallback，已交给后端的操作明确标记uncertain及“不会自动补发”。两条路径均通过recordOperation持久化。保留keep且localOnly的后台文件操作，不提前结束；保留preparing不调用协议stop的保护。

## 公开行为回归

- `stoppedWritesKeepUncertaintyInOperationAndArchive` 两数据行：实际RTU DLL写单寄存器已被通道接受，分别在尚未本地完成、已本地完成但未设备确认时停止。公开operation终态为cancelled、uncertain=true、保留“结果不确定/不自动重试”协议说明；排队未发送写入不标uncertain。重启等待后发送数不增长，当前及排队写入均不补发。归档query与JSONL导出保留相同结果及说明。
- `hostStopFallbackKeepsSubmittedOperationUncertain`：使用既有trace.fixture（stop不发布终态），验证宿主fallback取消、uncertain及说明可公开查询，归档结果一致。
- 原04写入全套回归、原01启动中停止/销毁回归、09重连回归及08后台保存跨停止/保留离线数据回归继续通过，后者明确验证localOnly文件操作不被取消。

## 执行与结果

复用现有Qt6.12.0 LLVM-MinGW / Clang22.1.7 / CMake3.30.5 / Ninja1.12.1，未安装工具或修改全局环境。仅注入可控传输与时钟，继续加载产品协议和传输DLL；没有真实设备写入。

```powershell
C:/Qt/Tools/CMake_64/bin/cmake.exe --preset qt-llvm-debug
C:/Qt/Tools/CMake_64/bin/cmake.exe --build --preset qt-llvm-debug
C:/Qt/Tools/CMake_64/bin/ctest.exe --preset qt-llvm-debug -R 'rtu_explicit_write|bounded_disk_recording|slave_edit_import_save|session_lifecycle|parallel_sessions_recovery' --verbose
```

配置/构建成功；[CTest](ctest.log) 5/5通过、31.26秒。QtTest：恢复14、从站编辑9、生命周期13、显式写入36、磁盘记录26，合计98 passed、0 failed、0 skipped（含5套初始化/清理10项，行为检查88项）。结果保存在同目录各results文件，sha256.txt记录日志、结果及定向测试产物哈希。

本次仅执行定向Debug；最终集成全量Debug/Release由根代理统一执行，不能把本次定向结果当成双配置最终验收。真实硬件边界仍留待13。

## 自审

确认协议stop调用在fallback之前且preparing受保护；协议publish形成的终态不会再被fallback覆盖，取消Message仍由原记录入口持久化。宿主fallback显式recordOperation，保留此前result字段并补uncertain。keep/localOnly语义和后台作业租约不变，未新增停止时等待磁盘的路径。新回归通过公开控制/查询验证，不查看私有队列或实现调用次数。
