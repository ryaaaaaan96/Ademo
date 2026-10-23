#include "debugger/trend_window.h"
#include <QLabel>
#include <QCheckBox>
#include <QColor>
#include <QPushButton>
#include <QScrollArea>
#include <QQuickWidget>
#include <QQmlContext>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
static void initializeTrends() {
    static const bool initialized = [] { Q_INIT_RESOURCE(trends); return true; }();
    Q_UNUSED(initialized)
}
namespace debugger {
namespace {
constexpr qsizetype sampleCapacity = 2048;
QString curveColor(int index) { return QColor::fromHsv((index * 137) % 360, 195, 180).name(); }
}
TrendWindow::TrendWindow(QList<TrendSeries> definitions, QWidget *owner)
    : QMainWindow(owner, Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint) {
    initializeTrends();
    setAttribute(Qt::WA_DeleteOnClose); setWindowTitle(QStringLiteral("参数趋势")); resize(900, 620);
    auto *body = new QWidget(this); auto *layout = new QVBoxLayout(body); setCentralWidget(body);
    for (auto &definition : definitions) series_.append({std::move(definition), {}});
    auto *controls = new QHBoxLayout; layout->addLayout(controls);
    auto *pause = new QPushButton(QStringLiteral("暂停显示"), body); controls->addWidget(pause);
    connect(pause, &QPushButton::clicked, this, [this, pause] {
        paused_ = !paused_; pause->setText(paused_ ? QStringLiteral("继续显示") : QStringLiteral("暂停显示"));
        if (!paused_) render();
    });
    auto *clear = new QPushButton(QStringLiteral("清空曲线"), body); controls->addWidget(clear);
    connect(clear, &QPushButton::clicked, this, [this] { emit cleared(); for (auto &series : series_) series.samples.clear(); render(); });
    auto *zoomIn = new QPushButton(QStringLiteral("放大时间轴"), body); controls->addWidget(zoomIn);
    auto *zoomOut = new QPushButton(QStringLiteral("缩小时间轴"), body); controls->addWidget(zoomOut);
    auto *range = new QLabel(QStringLiteral("最近 60 秒"), body); range->setAccessibleName(QStringLiteral("显示时间范围")); controls->addWidget(range);
    auto zoom = [this, range](double factor) { timeWindow_ = qBound(1.0, timeWindow_ * factor, 3600.0);
        range->setText(QStringLiteral("最近 %1 秒").arg(timeWindow_)); emit plotChanged(); };
    connect(zoomIn, &QPushButton::clicked, this, [zoom] { zoom(0.5); });
    connect(zoomOut, &QPushButton::clicked, this, [zoom] { zoom(2); });
    auto *details = new QWidget(body); auto *detailsLayout = new QHBoxLayout(details); auto *legend = new QVBoxLayout;
    detailsLayout->addLayout(legend);
    for (int i = 0; i < series_.size(); ++i) {
        const auto &definition = series_[i].definition;
        auto *check = new QCheckBox(definition.name + " [" + definition.unit + "]", details);
        check->setAccessibleName(QStringLiteral("显示曲线 ") + definition.name); check->setChecked(true);
        check->setStyleSheet("color: " + curveColor(i)); legend->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, i](bool visible) {
            series_[i].visible = visible;
            if (paused_) { auto entry = rendered_[i].toMap(); entry["visible"] = visible; rendered_[i] = entry; emit plotChanged(); }
            else render();
        });
    }
    summary_ = new QLabel(details); summary_->setAccessibleName(QStringLiteral("曲线数据概要")); summary_->setTextFormat(Qt::PlainText); summary_->setWordWrap(true);
    detailsLayout->addWidget(summary_, 1);
    auto *scroll = new QScrollArea(body); scroll->setWidgetResizable(true); scroll->setWidget(details); scroll->setMaximumHeight(170); layout->addWidget(scroll);
    layout->addWidget(new QLabel(QStringLiteral("每条曲线保留最近 %1 点；暂停显示不停止通信，完整数据请使用记录功能。 ").arg(sampleCapacity), body));
    auto *quick = new QQuickWidget(body); quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quick->setAccessibleName(QStringLiteral("设备参数趋势图"));
    quick->rootContext()->setContextProperty("trend", this);
    quick->setSource(QUrl("qrc:/trends/Trend.qml")); layout->addWidget(quick, 1);
    auto *timer = new QTimer(this); timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] { if (dirty_ && !paused_) render(); }); timer->start(); render();
}
void TrendWindow::append(const QString &id, qint64 timeUs, double value) {
    if (!std::isfinite(value)) return;
    for (auto &series : series_) if (series.definition.id == id) {
        series.samples.append(QPointF(double(timeUs) / 1000000, value));
        if (series.samples.size() > sampleCapacity) series.samples.removeFirst();
        dirty_ = true; return;
    }
}
void TrendWindow::render() {
    rendered_.clear(); QStringList descriptions;
    for (int i = 0; i < series_.size(); ++i) {
        const auto &series = series_[i]; QVariantList points;
        for (const auto &point : series.samples) points.append(QVariantMap{{"x", point.x()}, {"y", point.y()}});
        rendered_.append(QVariantMap{{"name", series.definition.name}, {"unit", series.definition.unit},
            {"color", curveColor(i)}, {"points", points}, {"visible", series.visible}});
        descriptions.append(QStringLiteral("%1 · %2 · %3 · %4 点").arg(series.definition.name + (series.visible ? QString() : QStringLiteral("（隐藏）")), series.definition.id,
            series.samples.isEmpty() ? QStringLiteral("等待读取") : QString::number(series.samples.last().y(), 'g', 10) + " " + series.definition.unit).arg(series.samples.size()));
    }
    summary_->setText(descriptions.join("\n")); dirty_ = false; emit plotChanged();
}
}
