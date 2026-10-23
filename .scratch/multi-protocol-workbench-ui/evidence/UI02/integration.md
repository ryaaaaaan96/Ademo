# UI02 集成核验

日期：2026-10-23。实施提交 `58fa24c4aad14965cf83df6b59e0f2c82e5a6ed2`，已通过合并提交 `85619ae` 无冲突合入 `codex/workbench-ui-integration`。合并前实现分支已包含集成基线 `f418da5`，两棵工作树均干净；提交中没有构建产物。

主工作树复用 Qt/LLVM-MinGW 预设重新配置并完成 Debug 构建，运行 `ctest --preset qt-llvm-debug -R parameter_window_read --output-on-failure`，1/1 CTest 通过；[原始 QtTest](integration-parameter-window-results.txt) 为 3 项通过、0 失败／跳过。[CTest 日志](integration-debug-ctest.log)。实现分支的完整 Debug／Release 12/12、241 项证据见 [acceptance.md](acceptance.md)。

`SHA256SUMS.txt` 保留实施者生成时的原工作树字节散列，不假设 Git checkout 后字节完全相同。本次主工作树核验 46 项，其中 39 项逐字节一致；其余 7 项为 Git 自动 LF→CRLF 转换，统一换行后内容逐项相同：`acceptance.md`、`release-configure.log`、`include/debugger/parameter_table.h`、`plugins/master_parameters.cpp`、`plugins/master_parameters.h`、`src/parameter_table.cpp`、`tests/parameter_window_tests.cpp`。代码内容由实施提交和合并提交固定；不重写原始证据清单冒充原始采集字节。

UI03 与 UI04 的直接前置现已满足，可独立调度。该软件核验不代表 UI13 的用户正式产品验收，也不解除旧13的硬件条件。
