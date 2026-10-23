# UI13 集成核验

日期：2026-10-23。实施提交 `36daabae9e8676d95ccb18569d43d9ce8d9b9096`（`codex/ui13-acceptance-migration`）已包含集成调度 `4a968da`。核验双方工作树干净、分支、祖先关系与变更范围后，通过 `0040f556025e9be9b295d6923404e55ca0a32b84` 无冲突 `--no-ff` 合入 `codex/workbench-ui-integration`。没有纳入构建产物。

此次保留 SDK12、协议/传输基线、正式参数/文件/曲线入口及旧插件 factory fallback。新主站读取重试与按功能码操作、白底记录、窄窗滚动和临时兼容页迁移详见 [实施验收](acceptance.md)。本记录仅证明上述合并提交的软件自动验收，不涵盖随后整体审查修复，也不代替正式产品用户确认。

主目录 `C:/Users/81966/Documents/Ademo` 复用 Qt 6.12.0 LLVM-MinGW、Clang 22.1.7 和现有 CMake 预设。两配置均重新配置并全目标构建，构建使用 `--parallel 3`；完整 CTest 使用 `--parallel 1 --output-on-failure`，全部测试由 CMake 指定 `QT_QPA_PLATFORM=windows`。此次使用原生 Windows/QML，未以 offscreen 或 software 后端替代。

| 配置 | 配置/全目标构建 | 完整 CTest | Qt Test 通过/失败/跳过 | 耗时 |
| --- | --- | --- | --- | --- |
| Debug | [配置](merge-debug-configure.log)、[构建](merge-debug-build.log) | [22/22](merge-debug-ctest.log) | 355/0/0 | 154.24秒 |
| Release | [配置](merge-release-configure.log)、[构建](merge-release-build.log) | [22/22](merge-release-ctest.log) | 355/0/0 | 130.10秒 |

各配置22份原始 Qt Test 结果分别在 `merge-debug/` 与 `merge-release/`，[逐项计数](merge-test-counts.md)包含初始化、清理和数据行。实施分支的两配置证据及首次失败、最小复现和失效中间测试记录全部原样保留。

Release 首次构建因用户旧实例加载 `bin/plugins/libserial_plugin.dll` 而发生链接 Permission denied，见 [原始失败日志](merge-release-build-locked-first.log)。没有终止用户实例。核验源与目标绝对路径均位于 `build/qt-llvm-release` 后，将旧 EXE、Modbus/Serial DLL 移入 `live-instance-backup-ui13/`，保留其原字节和运行中的进程，再在标准路径重新链接，见 [保留记录](merge-release-live-instance-preservation.txt)。既有 PID 28136 与 portable 实例 PID 22240 在测试后仍运行；本次未启动新的正式产品 GUI。该备份只在忽略的 build 内，不纳入 Git。

原实施清单 `sha256.txt` 的 **78/78** 项已在实施工作树核验一致：文本按 UTF-8/LF 规范化，`.log` 另去每行行尾空白，PNG 按原字节。该口径消除 Git autocrlf 的 CRLF 差异，不把工作树换行字节误称提交 blob 原字节。新增 [集成证据散列](merge-evidence-sha256.txt)以本目录为基准采用同一规范化口径；其不包含自身。[集成产物散列](merge-artifacts-sha256.txt)另记录本次主目录两配置70个实际 EXE/DLL 的原字节 SHA256，路径相对仓库根目录；它与实施分支的历史产物清单分别保存，没有复制二进制。

README 确认的本机入口为根目录 **`C:/Users/81966/Documents/Ademo/run-debugger.cmd`**，启动 `build/qt-llvm-release/bin/multi_protocol_debugger.exe` 并仅为该进程设置 Qt/LLVM、Qt插件及 QML 路径。它免于 PowerShell 脚本执行策略限制，但不是旧19的便携发行包。根协调者会在整体审查问题修复及最终复验后启动供用户操作。

UI13 当前为“软件迁移/自动验收完成，整体审查与用户正式操作确认待完成”。整体审查已发现需要另行修复的生命周期/busy问题，本次成功测试不能取消这些发现；根协调者负责修复、再次全套验收并更新审查记录。旧13真实硬件条件仍未满足，旧14–19未启动。
