# 14A 集成与最终审查

日期：2026-10-24。集成分支：`codex/workbench-ui-integration`。

实施分支 `codex/can14a-observation` 的最终提交 `e2a06d4a96070eee25ebabf9bfefddb4c8b2e1fe` 已通过 no-ff 提交 `e9a69f3601f2acaccaddc92ab15a996893d1c62e` 合入。合并文件树与已验证实施提交完全一致。UI13 的修复与审查见 [UI13 集成记录](../../../window-screen-recovery/evidence/integration-review.md)。

## Standards

独立审查以 `9704eba` 为固定基线，先检查 `69c641b`，再复核至 `8bc1199`、`f9f5f45`、`f8a64b5`。初审无问题；复审发现一个 P2：测试程序没有重链时，CAN 生命周期测试可能沿用旧插件 DLL。`f9f5f45` 改为依赖两个插件的独立 fixture 同步目标，增量构建日志及两个 DLL 的 SHA256 证明修复有效。最后离屏配置增量不改产品逻辑、真实显示设置或原有断言。最终未解决 finding 为 0。

## Spec

独立检查同一实施范围并核验最终任务和证据，finding 为 0。正式 Qt CAN 接收链路、标准/扩展及特殊帧信息、筛选和实际 CSV/JSONL 导出符合14A；虚拟后端无法保真提供的错误帧使用受控输入验证，未混写来源。旧14整体仍为 `needs-info`，14A软件完成不代表UPP接入或物理验收完成。

## 验证与限制

实施树 Debug/Release 离屏完整回归各 CTest 23/23、QtTest 368 通过、0 失败、4 项原生菜单/拖动测试跳过；CAN 专项各11项全部通过。原始日志、二进制哈希、图像、CSV/JSONL及首轮失败诊断见 [验收记录](acceptance.md)。主代理只读复核可读离屏图像及导出文件；评审者只读审查，没有操作桌面。

根目录合入后复用原工具链完成 Release 配置与全部174个构建步骤，见 [配置日志](integration-release-configure.log)、[构建日志](integration-release-build.log)。未在根目录重跑原生CTest或自动启动应用；本机启动入口对应的Release程序与配套插件已经更新。

用户要求“不要操作我电脑来验收”后停止全部桌面操作和可见窗口测试。历史原生接收/筛选证据与后续离屏控件导出分开记录，未声称完成桌面导出或本轮原生拖动复验。14B、旧15、TCP未启动，物理验收继续后置。
