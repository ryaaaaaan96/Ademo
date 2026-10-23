# UI13 审查修复后的最终集成验收

2026-10-23。最终修复分支 `codex/ui-review-fixes` 的提交 `ce2e65f1efc39b352357a2ffab261b53d59ea1e8` 已包含前轮集成记录 `17dcd7e`。核验双方工作树干净、分支、祖先关系与范围后，无冲突 `--no-ff` 合入 `codex/workbench-ui-integration`，最终受测源码提交为 **`4ae8f321a62a79dfa04bafd886c0b9e19935b117`**。

修复覆盖参数文件异步应用返回后的存活检查与 DLL lifetime、文件选择器 busy、待提交确认的关联重入保护。三个公开行为回归新增于参数定义套件，保留修改 revision 校验。修复分支的29项证据按 UTF-8/LF 规范化核验全部匹配，见 [独立红绿与定向验收](review-fixes/acceptance.md)及 [散列](review-fixes/sha256.txt)。历史首次失败、准备错误和崩溃诊断均保持；本轮没有改产品代码或重新生成历史证据。

在主目录 `C:/Users/81966/Documents/Ademo` 复用 Qt 6.12.0 LLVM-MinGW、Clang 22.1.7、CMake 3.30.5 与既有预设，两配置均重新配置并执行完整默认目标构建 `cmake --build --preset <preset> --parallel 3`。随后独占测试桌面，串行执行 `ctest --preset <preset> --parallel 1 --output-on-failure`。全部22套由 CMake 指定 `QT_QPA_PLATFORM=windows`，包括实际 QML 曲线和原生窗口布局，未切换 offscreen/software 后端。

| 配置 | 全目标配套构建 | 完整 CTest | Qt Test 通过/失败/跳过 | 时间 |
| --- | --- | --- | --- | --- |
| Debug | [配置](final-debug-configure.log)、[构建](final-debug-build.log)通过 | [22/22](final-debug-ctest.log) | 358/0/0 | 103.82秒 |
| Release | [配置](final-release-configure.log)、[构建](final-release-build.log)通过 | [22/22](final-release-ctest.log) | 358/0/0 | 88.37秒 |

各配置22份原始 Qt Test 输出保存在 `final-debug/` 和 `final-release/`，[逐项计数](final-test-counts.md)包含初始化、清理及数据行。相较前轮355项，新增3项参数定义回归，两配置参数定义均11项通过。新增文件使用 `final-*`，没有覆盖对应旧源码 `0040f556` 的 `merge-*` 历史。

本轮构建未再遇到产物占用。上一轮为保留用户旧实例而移动的旧产物仍在忽略目录 `build/qt-llvm-release/live-instance-backup-ui13/`，失败和恢复原始记录仍见 [前轮集成核验](integration.md)。既有用户 PID 28136 与 22240 保留；未启动新的正式产品 GUI。测试结束已释放 Windows 桌面锁。

[最终产物散列](final-artifacts-sha256.txt)记录主目录两配置70个实际 EXE/DLL 的原字节 SHA256，路径相对仓库根目录；只提交清单，不提交二进制。[最终证据散列](final-evidence-sha256.txt)以本目录为基准，包含本文、最终日志、逐项结果、计数与产物清单，不包含自身。文本按 UTF-8/LF 规范化，`.log` 额外去行尾空白；该口径明确消除 Git autocrlf 换行差异，不将规范化文本散列称为工作树原始字节散列。代码未变的全目标构建允许 Ninja 增量复用已匹配的对象，本记录不声称执行 clean rebuild。

两轴复审与完成项由根协调者单独落盘；本记录仅固定最终源码的集成构建和软件自动验收结果。用户正式产品操作确认仍待用户完成。根目录 `run-debugger.cmd` 将启动标准 Release 路径下本轮配套产物，正式 GUI 由根协调者在检查最终记录后启动。旧13真实硬件及旧14–19状态未变。
