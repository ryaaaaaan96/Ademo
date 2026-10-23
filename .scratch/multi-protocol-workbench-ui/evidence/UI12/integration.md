# UI12 集成核验

日期：2026-10-23。最终实现提交 `e394d58f0734a060fef8aa03b314117b631b15ce` 已包含集成 `bf91de0`。核验双方工作树干净及祖先关系正确后，通过 `66ffa7e` 无冲突合入 `codex/workbench-ui-integration`，未纳入构建产物。

主工作树复用 Qt/LLVM-MinGW 预设 [配置Debug](merge-debug-configure.log)，构建 `plugin_ui_contract_tests` 及其依赖的宿主、配套插件和测试DLL，见 [构建日志](merge-debug-build.log)。保留加载器先显式 `load()` 再 `instance()` 的通用修复及其 [最小失败／诊断证据](catalog-lifetime-diagnosis.md)，未增加协议名称分支。

UI08 此时占用 Windows 桌面，本次仅设置测试进程的 Qt/LLVM DLL 搜索路径及 `QT_QPA_PLATFORM=offscreen`，运行 `build/qt-llvm-debug/bin/plugin_ui_contract_tests.exe`。结果 **9项QtTest通过，0失败／跳过，退出码0**，见 [原始结果](merge-plugin-contract-offscreen.txt)。覆盖独立DLL参数视图、专用控件操作、文件保存／明确应用、重启发现与错误契约拒绝、多目录共享加载引用、取消与对象销毁后的租约，以及专用诊断状态。

offscreen的 `raise()`／`propagateSizeHints()` 能力提示保留在日志，不称作新增原生桌面验收。实现者 Windows Debug／Release 各2/2 CTest加曲线生命周期、每配置26项的证据见 [acceptance.md](acceptance.md)；本次没有重复无关全量或曲线容量测试。

实查 `build/qt-llvm-debug/bin/plugins` 仅含 `libmodbus_plugin.dll` 和 `libserial_plugin.dll`；示例与错误契约夹具输出到 `bin/ui-contract-fixtures`，未进入正式产品插件目录。示例不宣称真实UPP或新的产品传输组合。

`manifest.sha256` 以本目录为基准，文本UTF-8/LF、PNG原字节核验 **32/32一致**；原证据不覆盖，新增集成日志及本文由本次提交固定。

UI12已完成，UI08实施状态未改。整体独立审查与用户正式产品验收留UI13，旧13硬件及旧14–19边界不变。
