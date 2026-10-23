# UI13 软件迁移与正式界面验收

日期：2026-10-23。集成基线 `df3a8a4`（UI02–UI12 已验收）并同步调度提交 `4a968da`；实施分支 `codex/ui13-acceptance-migration`。本记录涵盖软件实现、自检和真实 Qt 客户区视觉检查；用户对正式产品的操作确认及集成后的两路独立审查仍待完成。

## 实现

- 宿主只展示插件的新参数页；可选工厂返回空时仍保留 SDK12 `createView` fallback。Modbus 协议字段没有进入宿主。
- 新主站独立读取重试配置保留超时、额外重试、间隔及原有完整轮询任务；按功能码窗口保留 01/02/03/04 读取和 05/06/0F/10 写入，打开不自动通信，不再建立重复观测或记录面板。
- 按功能码窗口的手工输入纳入 SDK12 本地状态保护。窗口关闭、Escape 和会话关闭可取消；放弃不撤回通信，确认后的新输入不被旧成功结果清除。模态路径持有 SessionAccess lifetime，使用堆对话框和 QPointer。
- 正式程序与组合验收共用 `applyApplicationStyle`：Fusion、Microsoft YaHei、13px 中文和白底记录。复用已接受原型的配色和布局意图，不引入模拟数据。压缩选择/工具行、默认折叠已有参数的来源配置、限制轮询摘要高度，表格操作列按内容留宽，其余列可调整并横向滚动。
- `run-debugger.cmd` 双击启动本仓库 Release 程序，过程级 Qt/LLVM PATH、Qt plugins 和 QML imports；缺少程序/运行库明确提示。它不是旧19便携交付，不修改 PowerShell 执行策略。
- README 改为当前正式流程，链接参数、轮询、从站、文件、趋势、插件与工作区文档。SDK12 对齐；历史验收未修改。

## 验证覆盖与测试迁移

新 `rtu_ui_acceptance` 通过真实 PluginCatalog / Workbench / ParameterView 与受控串口替身，组合创建多设备、同一会话四区能力、真实 QML 打开/采样、参数读取与轮询、文件编辑保存/明确应用、多会话平铺、工作区保存/恢复及关闭。新测例另覆盖读取重试保留轮询配置、打开高级入口不通信、0x10 精确 RTU 帧，以及高级草稿取消/放弃保护。

既有测试复用而非复制：UI05 待提交值、UI07 文件草稿、UI08 从站模拟数据；UI11 多窗口确认中新增数据与工作区保护；UI09 查询/导出归属；UI10 真正 QML 和 2,200 次请求容量/负载；UI12 外部 DLL/模态生命周期与 fallback。全部纳入完整 CTest。

旧 host 用例迁移到正式入口：生命周期检查参数页；四区观测通过当前设备/参数值观察；范围读取与四类写入通过“按功能码操作…”；多设备轮询通过参数页。保留原始 RTU 帧、异常、超时、不自动重试、断线及停止断言。独立旧 factory 合约用例仍保留。

### 红绿与问题定位

1. `red-retry-entry.txt` → `green-retry-entry.txt`：新页原先没有读取重试入口。
2. `red-advanced-entry.txt` → `green-advanced-entry.txt`：新页原先没有批量功能码入口，新增真实0x10帧断言。
3. `red-advanced-draft.txt` → 最终 UI13 结果：新增高级入口后输入尚未纳入修改保护；注册独立 owner 和带 revision 的关闭保护。
4. `red-advanced-target-revision.txt`：新增高级写入等待确认时更改设备、PDU地址、写入功能的三个数据行均能复现草稿被旧确认清除，数值编辑控制行通过。目标变更也推进草稿revision；最终五数据行验证新目标/新数值保留、未变更输入在成功后清除。
5. `first-migration-ctest.log` 记录首次全套迁移遗留：文件页测例在折叠后查找可见字段返回空、UI02用例仍找兼容按钮、旧写入用例未打开新高级入口。按实际用户展开/打开路径调整，未削弱原协议断言；`targeted-migration-green.log` 4/4通过。
6. 四区迁移需等待 UI08 异步设备选择完成，轮询测试配置补全合法串口帧格式；对应完整协议用例通过。
7. `trace-refresh-red.txt` / `trace-refresh-first-ctest.log` 保留全套回归中的记录停止时序失败：后台已 running，200ms界面刷新尚未启用停止按钮。假设顺序为按钮状态滞后、滚动遮挡、后台停止请求失败；最小复现先等初始按钮禁用再启动，`trace-refresh-minimal-red.txt` 实测点击前 backend="running"、enabled=false，随后同一 stopped 断言失败。最终用例等待实际按钮 enabled 后点击，保留原 stopped / 排空 / 历史内容断言；临时探针已移除，记录产品逻辑未改。

中间验证流程曾用保留时间戳的复制恢复临时探针源码，Ninja未重编译该测试；`invalidated-intermediate-ctest.log` 对应的中间全套作废，不作为验收。已重写恢复源码并在构建日志确认重新编译 `window_trace_tests`，重新跑无探针的定向5/5，再按最终二进制串行全套。该中间运行还记录了CTest墙钟超时（写入测试自身日志36/36），不据此修改协议或削弱断言；最终全套需要完整通过。

## 最终构建与测试

两配置均复用既有预设，完成 **全目标构建与完整串行 CTest，22/22 通过**。Debug 90.99秒，Release 122.20秒；每配置 Qt Test 合计 **355通过、0失败、0跳过**（包含初始化/清理与数据行），逐套计数见 [test-counts.md](test-counts.md)。

- `debug-configure.log` / `debug-build.log` / `debug-ctest.log` 与 `debug/*-results.txt`。
- `release-configure.log` / `release-build.log` / `release-ctest.log` 与 `release/*-results.txt`。
- 最终 UI13 10/10、记录归属与历史流程5/5；原协议读取43/43、轮询17/17、显式写入36/36。
- 最终 Release 独立重跑组合截图场景3/3，见 `formal-capture-release.txt`；图像来自这次运行。
- `launcher-missing.txt` 在含空格路径的缺失构建目录返回1且提示正确路径；`launcher-smoke.txt` 记录双击入口实际启动本worktree Release、创建工作台窗口、正常关闭验证进程，调用者PATH不变，已有两个用户实例保留。

构建日志保留初始完整构建及最终增量重编译；最终测试发生在新高级草稿目标修复及trace测试重编译之后。

## 正式客户区视觉检查

所有 `formal-*.png` 来自 windows 平台的正式 Workbench/浮窗/真实 QQuickWidget，使用与 `src/main.cpp` 相同的样式入口；未使用原型图替代。桌面级 `QScreen::grabWindow` 在当前执行会话返回黑图，因此改用 QWidget 客户区 grab；这些图不证明系统标题栏的像素外观。系统三按钮、居中和原生拖动/停靠语义继续由 UI03/UI11 原生用例回归。

- `formal-workbench.png`：会话独立浮动时的工作台空态、导航和停靠提示；实际完整停靠内容见 `formal-two-sessions.png`。
- `formal-floating-master.png`：780×820 浮窗，设备/组、读取/轮询、值与行内动作、成功状态及白底记录可见。
- `formal-narrow-master.png`：650×550，折叠记录后常用控制、参数值与动作可达；时间列横向滚动，剩余内容纵向滚动。
- `formal-narrow-file.png` / `formal-narrow-file-actions.png`：650×550 文件页折叠记录，表格及滚动后的保存/应用入口；不承诺窄窗口无滚动。
- `formal-two-sessions.png`：两个独立会话并排，各自目标/页面和记录归属。
- `formal-qml-trends.png`：真实读取/轮询产生的两个寄存器序列，图例、数值与单位，无硬件输入。

实施者已逐张目视中文、行内动作和文件保存/应用入口。集成人已复核780浮窗和650文件动作截图，未提出新增视觉修改。

## 尚未覆盖与待确认

- **用户正式产品操作确认待完成**：UI01 原型已接受，不等于正式产品已被用户接受。集成整体审查后由主代理启动最终 Release 供操作。
- **整体独立 Standards / Spec 审查待集成后执行**：本记录不宣称已经完成。
- 本机 Windows 11 / x64、Qt 6.12.0 / LLVM-MinGW Clang 22.1.7；CMake3.30.5、Ninja1.12.1、parallel3。原生 CTest 串行执行；Qt 库本身为 release，产品/插件各自配套 Debug/Release。
- 受控端点不证明真实串口、设备确认、物理总线时序、串口驱动/断线行为或所有 DPI/多屏组合；旧13等待真实硬件，旧14–19未启动。
- 保留用户已有旧产品/原型实例；CMD冒烟只关闭本 worktree 新启动的验证进程。

`sha256.txt` 对文本按 UTF-8/LF 标准化（日志仅去除行尾空白）、图像按原始字节计算；`artifacts-sha256.txt` 记录本机测试 EXE 与产品/插件的原始 SHA-256。build产物不入Git。
