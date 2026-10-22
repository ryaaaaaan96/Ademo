# Qt Widgets 与 Qt Quick/QML 对比

Status: needs-triage

用途：记录设计访谈 Q17 的比较依据。用户现已选择首版 Qt Widgets，并提出后续绘图面板局部嵌入 QML；该方案可通过 QQuickWidget 实现。以下能力依据 Qt 6 官方文档，开发成本和推荐属于针对本项目的工程判断，未进行性能基准测试。

## 比较对象

Qt Widgets 是传统桌面控件框架，QWidget 是其控件基类；QML 是声明式语言，通常搭配 Qt Quick 与 Qt Quick Controls 构建界面。本文件比较的是 Qt Widgets 与 Qt Quick/Controls。

## 与本项目需求的关系

| 维度 | Qt Widgets | Qt Quick / QML |
| --- | --- | --- |
| 界面开发 | C++ 控件、布局与 Qt Designer 表单 | QML 描述界面，使用 C++ 对象与模型提供数据和操作 |
| 寄存器表与报文列表 | QTableView、表头、delegate、选择/编辑机制可组合 | TableView 支持 C++ 模型、表头、编辑 delegate、键盘导航及可见项复用 |
| 解析字段树 | QTreeView 与树形模型 | TreeView 支持 QAbstractItemModel 与现成 TreeViewDelegate |
| 多面板布局 | QMainWindow / QDockWidget 提供停靠、移动和浮动 | SplitView、TabBar、StackView 可组织页面；若要求同等级拖拽停靠工作台，需额外实现或选组件 |
| 外观 | 支持 QStyle、样式表及自定义控件，可做统一主题 | 状态、动画与过渡是框架能力，适合高度定制视觉与动态交互 |
| 桌面风格 | 可使用平台风格与自定义主题 | 有 Windows、macOS、Fusion 等样式，并非只能采用移动端外观 |
| 协议插件专用视图 | 可约定视图工厂创建 QWidget，关联 C++ 模型与操作入口 | 可提供 QML 组件/模块以及 C++ 扩展类型，同样由宿主建立视图接入约定 |
| 协议逻辑归属 | 可保留为独立 C++ 核心 | 同样可全部保留在 C++；QML 不要求协议实现使用 JavaScript |
| 当前项目整合成本（判断） | 更贴近以表格、字段树、参数编辑为主的桌面调试工具 | 若视觉定制、动画或触控成为首要目标，收益更明显；需明确前后端绑定与组件接口 |

Qt Quick 的 TableView/TreeView 等能力随 Qt 6 的 minor 版本演进，确定具体版本时应检查目标功能。不能用旧版本经验判断当前 QML 没有表格、树或编辑能力。

## 性能与混合使用

不能直接判定 Widgets 总比 QML 快，或 QML 总比 Widgets 快。报文规模、模型更新批次、delegate 复杂度、界面刷新频率、缓存上限和目标硬件都影响结果；通信处理和记录应与界面刷新分离。

QQuickWidget 继承 QWidget，可以在 Widgets 中嵌入 QML 面板，插件可通过 QWidget 视图接口返回它或包含它的容器。官方文档说明它增加离屏渲染步骤，并禁用 threaded render loop。用户已选择将其作为后续绘图面板的可用扩展方式；实际绘图功能、刷新率和点数后续确定。

## 已选方案与取舍

首版采用 Qt Widgets：当前界面集中在寄存器编辑、报文列表、字段树及协议插件专用面板，Widgets 的现成桌面组件与 C++ 接入方式比较直接。未来绘图需要 QML 时，在对应面板内通过 QQuickWidget 集成，见 [ADR 0006](../../docs/adr/0006-widgets-shell-with-optional-qml-panels.md)。

两条路线都能实现“新增协议插件，主体不改”。这一目标取决于插件描述、视图工厂/组件、会话操作和模型接口的设计，不能仅由界面技术保证。

协议交互、传输、记录和重试逻辑保持在独立 C++ 模块中。绘图视图通过展示模型获取数据；如果未来全面更换界面，仍需要适配视图和插件 UI 接口。

## 官方依据

- [QTableView](https://doc.qt.io/qt-6/qtableview.html)：模型、表头、表格交互。
- [QAbstractItemView](https://doc.qt.io/qt-6/qabstractitemview.html)：选择、编辑、键盘导航和 delegate。
- [Qt Quick TableView](https://doc.qt.io/qt-6/qml-qtquick-tableview.html)：模型、编辑 delegate、表头与可见项复用。
- [Qt Quick TreeView](https://doc.qt.io/qt-6/qml-qtquick-treeview.html)：树模型与树形展示。
- [QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html)：可移动、停靠和浮动面板。
- [Qt Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html)：容器与控件。
- [Qt Quick Controls 样式](https://doc.qt.io/qt-6/qtquickcontrols-styles.html)、[Qt Widgets 样式](https://doc.qt.io/qt-6/qtwidgets-index.html#styles)：平台风格与主题。
- [C++ 数据与 QML 集成](https://doc.qt.io/qt-6/qtqml-cppintegration-data.html)、[QML C++ 扩展插件](https://doc.qt.io/qt-6/qtqml-modules-cppplugins.html)：C++ 对象、模型与插件模块。
- [Qt Quick 性能建议](https://doc.qt.io/qt-6/qtquick-performance.html)、[QQuickWidget 性能说明](https://doc.qt.io/qt-6/qquickwidget.html#performance-considerations)：性能优化与混合渲染代价。
