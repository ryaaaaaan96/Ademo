#include "master_trends.h"
#include "debugger/trend_window.h"
#include <QJsonArray>
#include <QTimer>
namespace debugger {
QWidget *openParameterTrends(const SessionAccess &access, const QList<ModbusTrendParameter> &parameters, QWidget *view) {
    QList<TrendSeries> definitions;
    for (const auto &parameter : parameters) definitions.append({parameter.id, parameter.name, parameter.displayUnit});
    // SessionWindow 可浮动/停靠；QQuickWidget 的原生 owner 始终是外层工作台。
    auto *owner = view; while (owner->parentWidget()) owner = owner->parentWidget();
    auto *window = new TrendWindow(definitions, owner);
    window->setWindowTitle(QStringLiteral("设备 %1 · 参数趋势").arg(parameters.first().unit));
    QObject::connect(view, &QObject::destroyed, window, [window] { delete window; });
    auto cursor = std::make_shared<quint64>(0);
    QObject::connect(window, &TrendWindow::cleared, window, [access, cursor] {
        if (access.messages) for (const auto &message : access.messages(0)) *cursor = qMax(*cursor, message.id);
    });
    auto *timer = new QTimer(window); timer->setInterval(100);
    QObject::connect(timer, &QTimer::timeout, window, [window, access, parameters, cursor] {
        if (!access.messages) return;
        for (const auto &message : access.messages(0)) {
            if (message.id <= *cursor) continue;
            *cursor = message.id;
            if (message.status != "success" || !message.fields.value("operationResult").toBool()
                || message.fields.value("freshnessUncertain").toBool()) continue;
            const auto values = message.fields.value("values").toArray();
            const int start = message.fields.value("start").toInt(-1);
            const int unit = message.fields.value("unit").toInt();
            const auto area = message.fields.value("area").toString();
            for (const auto &parameter : parameters) {
                const int offset = parameter.address - start;
                if (parameter.unit != unit || parameter.area != area || offset < 0 || offset >= values.size()) continue;
                if (values[offset].isDouble()) window->append(parameter.id, message.monotonicUs, values[offset].toDouble());
            }
        }
    }); timer->start(); window->show(); return window;
}
}
