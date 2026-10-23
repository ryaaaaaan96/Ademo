# UI10 集成核验

日期：2026-10-23。最终实施提交 `c0083994eb6fe3fd36fcacc8473f7d5cee2c2751` 已包含当前集成 `1a96a38` 及 SDK12。核验双方工作树干净、祖先关系正确后，通过 `640ce33` 无冲突合入 `codex/workbench-ui-integration`，未纳入构建产物。

主工作树复用既有 Qt/LLVM-MinGW 预设完成 [Debug配置](merge-debug-configure.log) 和 [全目标重编](merge-debug-build.log)，包括 Qt QuickWidgets、QML资源、宿主与配套 Modbus DLL。

设置仅测试进程的 Qt/LLVM DLL 搜索路径、`QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software`，直接运行 `build/qt-llvm-debug/bin/parameter_trend_tests.exe`。结果 **12项QtTest通过，0失败／跳过，退出码0**，用时47.286秒，见 [原始结果](merge-trends-offscreen-software.txt)。覆盖真实 QQuickWidget/QML、固定参数身份、暂停／清空／图例／缩放、成功且新鲜的数值过滤、有界容量、保持通信以及销毁后的最后插件租约释放。

本次为离屏软件渲染的集成冒烟，日志保留平台能力警告，不将其称为 Windows 原生 GPU、标题栏或多显示器验收。实现者最终 SDK12 Windows Debug／Release 各4/4 CTest、各39项，以及 Intel Iris Xe Graphics 的原生渲染、容量与截图证据见 [acceptance.md](acceptance.md)，本次不重复全套。

`manifest.sha256` 以本目录为基准，按文本 UTF-8/LF、PNG原字节核验 **44/44一致**；原证据和清单不覆盖，新增集成日志及本文由本次提交固定。配套二进制散列保留为实施者当时构建的证据，不冒称与重新构建的集成二进制逐字节一致。

UI10 已完成，UI08/UI12 保持各自实施状态。[参数趋势部署提示](../../../../docs/parameter-trends.md)明确 Qt Quick/QML依赖需随配套DLL交付，正式旧19打包任务未启动。UI13用户正式产品确认、旧13硬件和旧14–19边界保持。
