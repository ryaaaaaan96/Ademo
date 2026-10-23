# RTU 主从共享线路算法维护重构

日期：2026-10-23。独立 `rtu-master` worktree / `codex/rtu-master` 分支，基于最新集成 `023fcee`。关闭整体Standards审查中的P3重复算法问题。

## 变更与边界

新增插件内部 `plugins/rtu_wire.h`，集中CRC16和RTU帧时序计算。主站与从站调用同一算法；CRC仍使用初始值0xffff、多项式0xa001和逐字节低位优先迭代，CRC拼接的原低字节/高字节顺序不变。

帧时序返回微秒数：波特率大于19200时仍为750/1750，其他波特率仍按16500000/baud、38500000/baud向上取整。调用方继续先验证正波特率及11位字符配置（无校验2停止位、有校验1停止位），错误信息和验证顺序不变。没有新增SDK接口、修改状态语义或扩展框架；未改modbus.cpp、slave_edit.cpp、trace_query.cpp及任务索引。

## 必要验证

复用现有Qt6.12.0 LLVM-MinGW / Clang22.1.7 / CMake3.30.5 / Ninja1.12.1。没有安装工具、改全局环境或连接真实设备。

```powershell
C:/Qt/Tools/CMake_64/bin/cmake.exe --build --preset qt-llvm-debug
C:/Qt/Tools/CMake_64/bin/ctest.exe --preset qt-llvm-debug -R '^(rtu_first_read|rtu_polling|rtu_explicit_write|rtu_slave_json_read|rtu_slave_write)$' --verbose
```

Debug构建成功；[CTest](ctest.log) 5/5通过，30.74秒。QtTest读取43、轮询16、写入36、从站读取29、从站写入25，共149 passed、0 failed、0 skipped（含5套初始化/清理10项，行为检查139项）。原始结果与build.log保存在同目录，sha256.txt包含定向产物与证据哈希。

使用既有公开会话回归验证固定协议报文与CRC错误拒绝、9600/19200及38400/115200帧间隔边界、可信t1.5违规与恢复、主从四区读写和轮询调度；没有新增与算法实现镜像的测试。此次仅执行必要Debug套件，最终集成全量Debug/Release由根代理统一执行。

## 自审

两调用点均在baud正数校验后取得时序，保留11位字符约束。CRC按相同的quint8字节解释和quint16累计值计算。头文件函数inline，限定在debugger::rtuwire命名空间及插件内部路径，不引入公共兼容负担。git diff --check通过。
