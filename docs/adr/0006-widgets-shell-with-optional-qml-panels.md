# 主界面采用 Qt Widgets，绘图面板可按需嵌入 QML

首版主要界面是寄存器编辑、报文列表和协议字段树，选择 Qt Widgets 作为工作台和插件视图的界面基线。后续绘图面板允许通过 QQuickWidget 在 QWidget 中嵌入 QML，协议、传输和记录逻辑继续留在独立 C++ 模块。

视图扩展对宿主提供 QWidget 容器；QQuickWidget 继承 QWidget，因此插件内部可选择 QML 绘图而不改变主体的视图挂载方式。相比全面采用 Qt Quick，这保留了 Widgets 桌面组件的直接使用方式，同时为局部定制绘图提供路径。

QQuickWidget 带来额外离屏渲染并影响 Qt Quick 的渲染循环，未来绘图面板需按目标点数和刷新率验证；本决策不将绘图功能加入当前首版范围。相关依据见 [Qt 官方文档](https://doc.qt.io/qt-6/qquickwidget.html)。
