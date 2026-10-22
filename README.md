# 多协议调试工具

当前交付至 issue 03：Windows x64 Widgets 工作台，通过实际协议与串口 DLL 创建单个 Modbus RTU 主站会话，读取线圈（01）、离散输入（02）、保持寄存器（03）和输入寄存器（04）。主站视图按设备与数据区保留最近成功观测，显示布尔/16 位寄存器、异常、超时及原始记录。写入、从站、轮询、自动重试、工作区和完整回溯查询由后续 issue 实施。

## 构建和运行

复用本机 `C:\Qt` 的 Qt 6.12.0、LLVM-MinGW Clang 22.1.7、CMake 3.30.5、Ninja 1.12.1，使用 C++17。无需修改系统 PATH 或安装工具。

```powershell
./scripts/verify.ps1 -EvidenceId 03
./scripts/run.ps1
```

验证脚本运行 Debug/Release 配置、构建和 CTest，包含 01 生命周期回归与 02/03 读取测试，当前证据写入 `.scratch/multi-protocol-debugger/evidence/03/`，不覆盖既有 01/02 证据。也可用 `C:\Qt\Tools\CMake_64\bin\cmake.exe --preset qt-llvm-debug` 和同路径 `ctest.exe --preset qt-llvm-debug` 分步运行。预设仅对子进程设置运行库路径。

界面选择协议、传输和角色，输入 `COM3` 等实际串口名称与参数后启动。状态显示“运行中”只表示串口打开，不代表设备应答。停止按钮或关闭窗口释放端点。不存在的端口、系统占用、权限和设备错误显示在状态区。串口默认 9600/8/even/1，无流控。

启动后选择数据区，填写从站地址 1..247、PDU 起始地址 0..65535、数量（位区 1..2000，寄存器区 1..125），范围不能越过 65535。点击读取后，界面逐项显示布尔值或原始 16 位寄存器值；异常与超时分别显示。表格中的最近成功值是历史观测，失败不覆盖为伪造数值。公开动作依次为 `read-coils`、`read-discrete`、`read-holding`、`read-input`，参数均为 `unit/start/count`；成功结果包含 `unit/area/function/start/values`。明细中的 `send-request` 是发送请求、`send-accepted` 是通道接纳、`local-sent` 是本地后端完成；只有验证通过的设备响应才能使读取 `success`。未匹配流量仅属于观察窗口，不声称是响应。串口需使用 11 位 RTU 字符：有校验时 1 停止位，无校验时 2 停止位。

本项一次读取只发送一次。应答超时默认 1000 ms；公开 SessionConfig.protocol 可设置整数 `responseTimeoutMs`（1..60000），本项界面使用默认值。等待总线空闲最多等待 `max(应答超时, 1000 ms)`，本地发送也有超时。已发送但未确认时不直接允许再次读取，提示停止并重启会话，避免把迟到响应强关联到新操作；本项不提供自动恢复或重试。RTU 没有事务编号，跨人工重启后同地址/长度的物理迟到响应仍需在硬件联调中控制。

## 模块与生命周期

`include/debugger/sdk.h` 的 SDK 2 增加通道收发、可注入单调时钟、协议操作/结果/消息、原始记录及视图访问入口。宿主只解释通用声明和结果，Modbus 参数与寄存器显示在协议插件中；串口插件独占 QSerialPort，并处理部分写入。实现路径核验见 [.scratch/multi-protocol-debugger/modbus-implementation-path.md](.scratch/multi-protocol-debugger/modbus-implementation-path.md)：自有无 I/O 内核，不新增 Modbus 第三方库依赖。

`bin/plugins` 中 DLL 在启动时发现。配套交付要求 SDK 2、产品 0.2、相同 Qt 完整版本、编译器版本、目标架构及 Debug/Release 配置；接口 IID 与构建指纹同步升级，01 的 SDK 1 DLL 必须重新构建。Qt 加载器先检查自身二进制兼容性，宿主再校验描述；这不支持任意第三方 ABI，也不提供插件崩溃隔离。坏插件与重复身份给出诊断，其他插件仍可用。插件更新后重启。

会话和插件视图持有注册表租约；先停止、销毁实例和视图，再释放加载器。端点占用以同一注册表管理，Windows COM 名称规范化后检查冲突。运行代次与 QObject 弱引用同时过滤停止后、重启后及销毁后的回调。所有接口在所属 Qt 事件循环线程串行调用，端点事件在同一线程发出；宿主延迟处理以避免回调中销毁端点。

测试从 `DebugSession` 公开启停、submit、operation、traces、messages、trace 和 poll 入口验证行为。`EndpointFactory` 与可控时钟替换 I/O 和时间，不注册测试协议或产品支持组合。生产时钟由 QElapsedTimer 提供，Qt 精确定时器驱动协议，界面按 33/50 ms 合并刷新。原始记录先入缓存再解析：最多 256 条，每条最多 4096 字节；消息最多 256 条，操作最多 128 项，操作观察引用最多 512 项。ID 不复用，过期来源通过 trace 的空值和界面标记表达；截断的观察引用另有计数，不冒充完整历史。

RTU 内核处理 t1.5/t3.5、CRC、数量和地址校验；测试传输可显式提供可信线路静默 `ReceiveChunk.idleBeforeUs`。QSerialPort 不提供逐字节硬件时间：实际块源留空此元数据，内核按长度保守组装候选完整帧后等待主机观测静默，不把分块交付间隔直接判为帧内违规；未确认完整的接收候选保留至应答截止。Windows 定时调度和 USB 聚合可能隐藏块内物理间隙，无法仅凭这些 API 保证严格线路时序，真实串口互通和精度仍须硬件验收。实际串口 DLL 的失败路径单独验证，模拟成功不构成硬件连接或互通证据。
