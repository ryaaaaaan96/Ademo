# 操作缓存淘汰界面修复验收

整体 Spec 审查发现 P2：有界操作缓存淘汰后 `operation(id).request.id == 0`，其默认 finished=false 不能解释为在途。现 UI 明确识别缺失快照，显示过期提示、允许继续读写，并允许选择后续轮询结果；真正未完成快照仍禁止重复手动读写。

新增公开 Widgets 回归 `expiredManualResultDoesNotDisableWidgetsOrPolling`：operations=1，完成初始轮询和手动读取，下一轮轮询淘汰该手动 ID 后断言 request.id=0、过期说明与按钮可用；轮询成功后界面追踪新 ID；再通过按钮完成手动读取和设备写入，过程中两按钮禁用；stop/restart 后仍可继续。无真实硬件，注入字节流和可控时钟。界面 50ms 刷新采用 QTRY 等待，避免把定时器调度时间视作固定 60ms 保证；首轮该等待检查失败后已修正测试等待。

沿用 Qt/LLVM-MinGW Debug 环境，只重编必要 rtu_poll_tests 并执行 `ctest --preset qt-llvm-debug -R '^rtu_polling$' --verbose`，1/1 CTest、17 项 Qt 检查通过。日志及 SHA256 已保存；本工作树同时含从站绑定未知字段修复，最终根分支将统一执行双配置。未修改索引或 trace_query。
