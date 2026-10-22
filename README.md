# 多协议调试工具

当前交付 issue 01：Windows x64 Widgets 工作台，通过实际协议与串口 DLL 创建单个 Modbus RTU 主站会话，配置、打开、停止并释放串口。尚未提供 Modbus 报文读写，从站、轮询、工作区和回溯由后续 issue 实施。

## 构建和运行

复用本机 `C:\Qt` 的 Qt 6.12.0、LLVM-MinGW Clang 22.1.7、CMake 3.30.5、Ninja 1.12.1，使用 C++17。无需修改系统 PATH 或安装工具。

```powershell
./scripts/verify.ps1
./scripts/run.ps1
```

验证脚本运行 Debug/Release 配置、构建和 CTest，证据写入 `.scratch/multi-protocol-debugger/evidence/01/`。也可用 `C:\Qt\Tools\CMake_64\bin\cmake.exe --preset qt-llvm-debug` 和同路径 `ctest.exe --preset qt-llvm-debug` 分步运行。预设仅对子进程设置运行库路径。

界面选择协议、传输和角色，输入 `COM3` 等实际串口名称与参数后启动。状态显示“运行中”只表示串口打开，不代表设备应答。停止按钮或关闭窗口释放端点。不存在的端口、系统占用、权限和设备错误显示在状态区。串口默认 9600/8/even/1，无流控。

## 模块与生命周期

`include/debugger/sdk.h` 是当前最小 SDK：声明插件身份、支持组合、配置字段、协议生命周期及端点启停事件。宿主只解释通用声明，协议专用视图在 Modbus 插件中；串口插件独占 QSerialPort。SDK 当前不承诺发送、计时或读写能力，02 需要显式扩展契约。

`bin/plugins` 中 DLL 在启动时发现。配套交付要求 SDK 1、产品 0.1、相同 Qt 完整版本、编译器版本、目标架构及 Debug/Release 配置；描述中的构建指纹必须匹配。Qt 加载器先检查自身二进制兼容性，宿主再校验描述；这不支持任意第三方 ABI，也不提供插件崩溃隔离。坏插件与重复身份给出诊断，其他插件仍可用。插件更新后重启。

会话和插件视图持有注册表租约；先停止、销毁实例和视图，再释放加载器。端点占用以同一注册表管理，Windows COM 名称规范化后检查冲突。运行代次与 QObject 弱引用同时过滤停止后、重启后及销毁后的回调。所有接口在所属 Qt 事件循环线程串行调用，端点事件在同一线程发出；宿主延迟处理以避免回调中销毁端点。

测试从 `DebugSession` 公开启停/状态/配置查询入口验证行为。`EndpointFactory` 是通道边界的测试注入点，替换端点 I/O，不注册测试协议或产品支持组合。实际串口 DLL 的失败路径单独验证；模拟成功不构成硬件打开、Modbus 互通或 RTU 时序证据。
