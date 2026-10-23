# UI13 整体审查修复验收

2026-10-23。独立分支 `codex/ui-review-fixes`，起点为集成源码 `0040f556`，提交前同步集成文档 `17dcd7e`。复用本机 Qt 6.12.0、LLVM-MinGW Clang 22.1.7、CMake 3.30.5、C++17、SDK12；未改工具链或 SDK。

## 最初 Standards 两项 finding

1. P1：参数文件后台完成回调调用 `applyDefinition`，其中修改确认可能销毁视图；返回后仍使用空 `QPointer` 刷新。通过公开 `DebugSession`、真实参数表编辑和加载按钮，在确认出现时销毁视图复现 `0xc0000005`（进程退出 `-1073741819`）。`p1-red.txt` 为崩溃前 Qt Test 输出；首轮使用 offscreen，不占当时集成原生桌面测试。修复在应用返回后重新检查存活，并在 `applyDefinition` 进入可能销毁视图的配置回调前依序复制 lifetime 和 configure。`p1-green.txt` 通过；最终 Windows Debug/Release 都包含同一回归，验证原配置保留、未发设备命令、无残留 busy。
2. P2：来源文件选择器未登记 busy。通过 Workbench 的公开后台执行入口控制此前工作区读取在选择器内交付；`p2-red.txt` 记录实际 `success` 替换而期望 `cancelled`。为文件选择登记模态深度，返回且视图存活后解除。绿测还确认取消后下一次明确打开工作区可以成功，未遗留 busy。

## 修复时发现的直接关联缺口

`resolvePending` 的待提交确认同样没有登记 busy；在设备切换确认中到达新的配置动作，会再打开第二个确认。此项是修复时对关联模态返回路径的审计结果，不追溯改写最初两轴报告。`confirmation-red.txt` 记录实际出现嵌套确认。修复为该确认持有局部 lifetime 并共用模态深度；`confirmation-green.txt` 验证重入配置被拒绝、取消保持旧目标及值、之后明确放弃可以完成切换。保持现有修改 revision 校验。

## 红绿与最终定向回归

三个回归均位于 `tests/parameter_definition_tests.cpp`，通过已批准的公开会话／工作区入口和真实 Qt 控件观察结果，不调用私有实现。

| 配置 | CTest 套件 | Qt Test 通过 | 失败／跳过 |
| --- | --- | --- | --- |
| Debug / Windows | 4/4 | 54（11+15+13+15） | 0/0 |
| Release / Windows | 4/4 | 54（11+15+13+15） | 0/0 |

覆盖参数定义、文件编辑保存应用、待提交写入、工作区窗口布局。Debug 的前三套见 `debug-ctest.log`，工作区一套见 `debug-workspace-ctest.log`；最初筛选误写工作区测试名，实际仅三套，发现后单独执行正确的 `workspace_window_layout`，没有将未执行套件计入结果。Release 四套一次执行见 `release-ctest.log`。两配置实际 Qt 输出分别保存在 `debug/` 和 `release/`。源代码和新增测试通过 `git diff --check`。

P2 首次测试准备误将默认展开的来源面板折叠，造成空按钮断言，保留为 `p2-fixture-error.txt`；随后改为明确展开并验证按钮存在，再获得有效红测。此准备错误不作为产品缺陷或红绿证据。

完整串行 CTest、修复后两轴独立复审由主集成分支继续完成；本记录只证明这次修复的定向验收。UI13 正式用户操作确认仍待用户完成；旧13真实硬件及旧14–19没有启动。原 UI13 历史验收及散列文件不改写，本目录独立散列见 `sha256.txt`。
