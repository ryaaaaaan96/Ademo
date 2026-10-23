#pragma once
#include <QMainWindow>
#include <QVariantList>
#include <QPointF>
class QLabel;
namespace debugger {
struct TrendSeries { QString id, name, unit; };
// 只持有显示数据；通信与参数寻址留在协议适配层。
class TrendWindow final : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(QVariantList curves READ curves NOTIFY plotChanged)
    Q_PROPERTY(double timeWindow READ timeWindow NOTIFY plotChanged)
public:
    explicit TrendWindow(QList<TrendSeries> series, QWidget *owner);
    void append(const QString &id, qint64 timeUs, double value);
    QVariantList curves() const { return rendered_; }
    double timeWindow() const { return timeWindow_; }
signals:
    void plotChanged();
    void cleared();
private:
    struct Series { TrendSeries definition; QList<QPointF> samples; bool visible = true; };
    QList<Series> series_;
    QVariantList rendered_;
    QLabel *summary_;
    bool dirty_ = true;
    bool paused_ = false;
    double timeWindow_ = 60;
    void render();
};
}
