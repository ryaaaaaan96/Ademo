# 重叠插件目录的根对象生命周期

使用 diagnosing-bugs 流程。UI12 单独执行真实多协议窗口测试通过；先创建并销毁一个带文件页的插件视图，再创建第二目录和工作台时，Debug 稳定触发 0xc0000005。原两用例循环为：

```powershell
plugin_ui_contract_tests.exe independentPluginProvidesItsOwnColumnsAndDevices specializedControlSendsOnlyItsWindowOperation -platform offscreen
```

[原失败](red-catalog-lifetime.txt)、[LLDB 栈](probe-catalog-lldb.txt) 将失效访问定位到 `PluginCatalog::protocol` 的 `p->describe()`，调用来自正常会话已打开后的界面刷新。没有访问旧控件或发送更多请求。

排查前提出三项假设：共享 DLL 的加载引用计数不平衡；旧回调仍位于已释放 DLL；测试缓存了失效控件。LLDB 指向第二目录的插件根对象；只增加显式 load 后原循环通过，不改回调或控件，支持第一项。

最初“两个目录→释放第一目录→调用 describe”的缩小探针恰好通过：已释放内存未被重用时，继续调用虚函数不能可靠证明对象存活，见 [探针](probe-catalog-without-view.txt)。最终最小公开回归用 `QPointer<QObject>` 观察已加载的 Qt 插件根对象：第一目录的视图租约完全释放后，第二目录仍持有时 root 必须存活。原实现明确返回 false，见 [最小 red](red-overlapping-catalog.txt)，不再以随机堆布局决定是否失败。

Qt 的 `instance()` 在共享库已加载时可跳过 `load()`；每个 loader 的显式 `load()` 则记录自身加载状态。源码参见 [Qt 官方仓库](https://raw.githubusercontent.com/qt/qtbase/dev/src/corelib/plugin/qpluginloader.cpp)，本机回归独立证实当前 Qt6.12.0 下的结果。修复是宿主在获取 instance 前显式 load；加载失败继续原 errorString 诊断。不增加等待、永久禁止卸载或热加载能力。

同一最小回归继续创建真实参数视图；原两用例和完整 UI12 组合也已通过。最终两配置结果见 acceptance.md。独立重启验证把临时安装目录限制在子进程中，进程退出后删除文件；不用父进程保留 DLL 锁来冒充重启。
