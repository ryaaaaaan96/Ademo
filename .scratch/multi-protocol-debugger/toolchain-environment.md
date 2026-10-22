# 本机开发环境选型与验证

Status: ready-for-agent

核验日期：2026-10-22。按用户指定的 `C:\Qt` 安装选择环境，开发使用 VS Code。当前采用 **Qt 6.12.0 / LLVM-MinGW Clang 22.1.7 / CMake 3.30.5 / Ninja 1.12.1 / Windows x64 / C++17**。

## 选定组合

| 项目 | 本机选择 | 路径或依据 |
| --- | --- | --- |
| Qt | 6.12.0，llvm-mingw_64 | `C:\Qt\6.12.0\llvm-mingw_64` |
| C++ 编译器 | LLVM-MinGW Clang 22.1.7 | `C:\Qt\Tools\llvm-mingw2217_64\bin\clang++.exe` |
| 编译目标 | x86_64-w64-windows-gnu | 编译器实际输出，64 位 Windows GNU/MinGW 目标 |
| C++ 标准 | C++17 | 验证程序实际输出 `201703`，后续有明确需要再升级 |
| CMake | 3.30.5 | `C:\Qt\Tools\CMake_64\bin\cmake.exe` |
| 构建执行器 | Ninja 1.12.1 | `C:\Qt\Tools\Ninja\ninja.exe` |
| 开发环境 | VS Code | 用户已有安装；不依赖 Qt Creator |
| 代码分析 | 同套 LLVM 的 clangd | `C:\Qt\Tools\llvm-mingw2217_64\bin\clangd.exe` |
| 调试工具 | 同套 LLVM 的 LLDB / lldb-dap | `C:\Qt\Tools\llvm-mingw2217_64\bin` |

本机只发现这一套 Qt 桌面 Kit。其 qconfig 声明 Qt 6.12.0、x86_64 及 Clang 22.1.7，mkspec 为 `win32-clang-g++`，与已安装编译器匹配。因此沿用该 LLVM-MinGW 组合，不将 MSVC、clang-cl 或另一套 GCC 的产物混入当前构建。应用和项目插件使用同一套工具链、Qt 安装及对应构建配置；项目 SDK 版本与实际插件 ABI/生命周期由当前 [issue 01](issues/01-rtu-session-lifecycle.md) 验证。

依据来自本机 `mkspecs/qconfig.pri`、`mkspecs/win32-clang-g++/qmake.conf`、Qt CMake 包、工具版本命令和实际编译运行。这里的版本表示已安装并核验的版本，不推断其发布渠道或长期支持状态。

## 已有模块与 VS Code 扩展

CMake 成功发现 Core、Widgets、SerialPort、SerialBus、Network、Test 和 QuickWidgets。QuickWidgets 为以后嵌入 QML 保留，未在本轮实现绘图；Qt Test 已可发现，产品测试框架尚未搭建。

读取默认 VS Code 扩展目录，发现 CMake Tools 1.24.42、clangd 0.6.0、LLDB DAP 0.4.1、C/C++ 1.34.4、CodeLLDB 1.12.3 和 Qt 扩展。目录存在只证明已安装，不代表当前窗口已启用或已经验证 IDE 调试。

[工作区设置](../../.vscode/settings.json) 固定 CMake、clangd、lldb-dap 的路径。正式产品工程尚未创建，关闭打开工作区时的自动 CMake 配置；后续工程落地再提供产品预设与启动配置。没有修改系统 PATH 或用户级设置，也没有安装新工具。

VS Code CLI 枚举扩展时尝试创建用户配置目录，受到当前文件权限限制；已改为读取扩展目录完成核验，无需更改用户配置权限。

## 可复现验证

独立验证工程位于 [toolchain-check](toolchain-check/CMakeLists.txt)，不作为产品实现。运行：

```powershell
& '.\.scratch\multi-protocol-debugger\toolchain-check\verify.ps1'
```

脚本分别运行 Debug、Release 的 CMake 配置、Ninja 构建与 CTest。工具、Qt 前缀及运行 DLL 搜索路径写在 [CMakePresets.json](toolchain-check/CMakePresets.json) 中，PATH 仅在对应构建/测试子进程生效，不要求全局安装配置。

| 检查 | 结果 |
| --- | --- |
| Debug 配置与构建 | 通过，CMake 识别 Clang 22.1.7，MOC 与链接成功 |
| Debug 运行 | 1/1 通过，offscreen QApplication 和 QWidget 创建成功 |
| Release 配置与构建 | 通过，MOC 与链接成功 |
| Release 运行 | 1/1 通过，报告 Qt 6.12.0、C++17、64 位、MocProbe 元对象 |
| LLDB 命令行调试 | Debug 程序在 main 源码断点停住，继续执行后退出码 0；VS Code 调试界面尚未实测 |
| 基础通信模块 | QSerialPort、QTcpSocket 对象和 QCanBus 实例可创建；不打开实际端口或连接 |
| CAN 后端发现 | passthrucan、peakcan、systeccan、tinycan、vectorcan、virtualcan；发现名称不代表已连接硬件或驱动互通 |

验证日志：[Debug 配置](toolchain-check/qt-llvm-debug-configure.log)、[Debug 构建](toolchain-check/qt-llvm-debug-build.log)、[Debug 运行](toolchain-check/qt-llvm-debug-test.log)、[Release 配置](toolchain-check/qt-llvm-release-configure.log)、[Release 构建](toolchain-check/qt-llvm-release-build.log)、[Release 运行](toolchain-check/qt-llvm-release-test.log)。构建产物留在验证目录并由该目录的忽略规则排除。

另以同套 `lldb.exe --batch` 对 Debug 验证程序设置 main 断点、启动并继续执行，[调试日志](toolchain-check/lldb-check.log) 确认能够定位源码并正常结束。调试进程仅通过临时 PATH 查找 Qt/LLVM DLL。

CMake 提示未找到可选 Vulkan headers，但本次配置、编译和运行均成功；当前 Widgets/串口阶段不因此安装 Vulkan。未来 QML 绘图的实际渲染路径需单独验证。

## 后续边界

- 本次固定了本机工具链，尚未完成 Modbus 实现组件选型和协议验证；旧 01 的剩余选型工作由当前 [issue 02](issues/02-rtu-first-register-read.md) 承接，见 [迁移表](ticket-migration.md)。
- 未生成产品主工程，没有验证项目协议插件的二进制接口、RTU 时序、CAN 硬件或 UPP 消息。
- Debug 验证程序构建和运行成功，不代表 Qt 本身的调试库及所有依赖调试符号已经齐备。
- 当前机器路径进入本地配置与验证预设；后续 CI 或其他开发机应提供对应环境配置，不承诺固定安装路径跨机器可用。

## Comments

### 2026-10-22

用户指定按 `C:\Qt` 的实际安装选择环境。核验版本和模块后选择已配套的 LLVM-MinGW，并完成 Debug/Release 编译运行验证，更新规格和工具链 issue。
