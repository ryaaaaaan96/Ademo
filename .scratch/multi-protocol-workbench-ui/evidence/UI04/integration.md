# UI04 集成核验

日期：2026-10-23。实施提交 `d35470f4241b4fcd87f108adf84459a3de42f8e7`，合并提交 `cf69274`，集成分支 `codex/workbench-ui-integration`。实现分支已包含合入前的集成基线 `5f3c8a8`；双方工作树干净，无冲突合入，未纳入构建产物。

主工作树使用原有 Qt/LLVM-MinGW 预设重新 [配置](integration-debug-configure.log) 和 [构建](integration-debug-build.log)，SDK10 的宿主、协议及传输插件配套重编。随后使用 Windows Qt 平台串行运行：

`ctest --preset qt-llvm-debug -R '^(parameter_definition_read|parameter_window_read)$' --parallel 1 --output-on-failure`

结果为 **2/2 CTest 通过，11 项 QtTest 通过，0 失败／跳过**。见 [CTest 日志](integration-debug-ctest.log)、[参数定义原始结果](integration-parameter-definition-results.txt)、[窗口首读原始结果](integration-parameter-window-results.txt)。实现分支完整 Debug／Release 13/13、249 项及最终缓存修正的两配置定向证据继续保留在 [acceptance.md](acceptance.md)，本次没有重复无关全量检查。

原 `SHA256SUMS.txt` 对应实施阶段采集字节，不重写历史清单；Git checkout 的换行转换说明沿用原验收记录。集成日志及本文由本次集成证据提交固定。

UI05 的直接前置已满足，可调度行内待提交值与明确设备写入。UI03 仍在独立工作树实施，后续合并需保留其宿主窗口变更与本项参数能力。该集成核验不代表 UI13 的用户正式产品验收，旧13硬件及旧14–19范围不变。
