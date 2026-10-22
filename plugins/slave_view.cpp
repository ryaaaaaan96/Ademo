#include "slave_view.h"
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace debugger {
QWidget *createSlaveView(SessionAccess access, QWidget *parent)
{
    auto *view = new QWidget(parent);
    view->setObjectName("slaveView");
    auto *layout = new QVBoxLayout(view);
    auto *description = new QLabel(QStringLiteral("从站模拟数据：一设备一外部 JSON。配置后选择 slave 角色再启动；全部文件校验成功后才打开端点。运行中外部文件变化不自动加载。"), view);
    description->setWordWrap(true); layout->addWidget(description);
    auto *form = new QFormLayout;
    auto *unit = new QSpinBox(view); unit->setRange(1, 247); unit->setObjectName("slaveUnit");
    auto *path = new QLineEdit(view); path->setObjectName("slavePath");
    auto *browse = new QPushButton(QStringLiteral("选择设备 JSON"), view);
    auto *add = new QPushButton(QStringLiteral("添加设备绑定"), view); add->setObjectName("addSlaveDevice");
    form->addRow(QStringLiteral("模拟设备地址"), unit); form->addRow(QStringLiteral("外部文件绝对路径"), path);
    form->addRow(browse); form->addRow(add); layout->addLayout(form);
    auto *bindings = new QTableWidget(0, 2, view); bindings->setObjectName("slaveBindings");
    bindings->setHorizontalHeaderLabels({QStringLiteral("Unit ID"), QStringLiteral("启动文件")});
    bindings->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); layout->addWidget(bindings);
    auto *remove = new QPushButton(QStringLiteral("删除选中绑定"), view);
    auto *apply = new QPushButton(QStringLiteral("应用启动文件绑定"), view); apply->setObjectName("applySlaveBindings");
    layout->addWidget(remove); layout->addWidget(apply);
    auto *status = new QLabel(view); status->setObjectName("slaveStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto *data = new QTableWidget(0, 5, view); data->setObjectName("slaveData");
    data->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("数据区"), QStringLiteral("PDU 范围"), QStringLiteral("模拟值（前 16 项）"), QStringLiteral("启动文件")});
    data->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); data->setEditTriggers(QAbstractItemView::NoEditTriggers);
    data->setMinimumHeight(180); layout->addWidget(data);
    QObject::connect(browse, &QPushButton::clicked, view, [view, path] {
        const auto selected = QFileDialog::getOpenFileName(view, QStringLiteral("选择单个设备 JSON"), {}, "JSON (*.json)");
        if (!selected.isEmpty()) path->setText(selected);
    });
    QObject::connect(add, &QPushButton::clicked, view, [bindings, unit, path] {
        if (bindings->rowCount() >= 32) return;
        const int row = bindings->rowCount(); bindings->insertRow(row);
        bindings->setItem(row, 0, new QTableWidgetItem(QString::number(unit->value())));
        bindings->setItem(row, 1, new QTableWidgetItem(path->text()));
    });
    QObject::connect(remove, &QPushButton::clicked, view, [bindings] { if (bindings->currentRow() >= 0) bindings->removeRow(bindings->currentRow()); });
    QObject::connect(apply, &QPushButton::clicked, view, [access, bindings, status] {
        QJsonArray devices;
        for (int row = 0; row < bindings->rowCount(); ++row) {
            const auto *u = bindings->item(row, 0), *p = bindings->item(row, 1);
            devices.append(QJsonObject{{"unit", u ? u->text().toInt() : 0}, {"path", p ? p->text() : QString()}});
        }
        auto config = access.config ? access.config().protocol : QJsonObject{};
        config["devices"] = devices;
        const bool ok = access.configure && access.configure(config);
        status->setText(ok ? QStringLiteral("绑定已保存到会话配置；启动时后台校验文件") : QStringLiteral("运行或准备期间不可修改启动绑定"));
    });
    if (access.config) for (const auto &binding : access.config().protocol.value("devices").toArray()) {
        const auto device = binding.toObject(); const int row = bindings->rowCount(); bindings->insertRow(row);
        bindings->setItem(row, 0, new QTableWidgetItem(QString::number(device.value("unit").toInt())));
        bindings->setItem(row, 1, new QTableWidgetItem(device.value("path").toString()));
    }
    auto refresh = [access, bindings, unit, path, browse, add, remove, apply, data] {
        const bool busy = access.running && access.running();
        for (auto *widget : QList<QWidget *>{bindings, unit, path, browse, add, remove, apply}) widget->setEnabled(!busy);
        if (!access.snapshot) return;
        const auto devices = access.snapshot().value("devices").toArray();
        data->setRowCount(int(devices.size()) * 4);
        int row = 0;
        for (const auto &entry : devices) {
            const auto device = entry.toObject(); const auto areas = device.value("data").toObject().value("areas").toObject();
            for (const auto &name : {"coils", "discrete", "holding", "input"}) {
                const auto area = areas.value(name).toObject(); const auto values = area.value("values").toArray();
                QStringList summary;
                for (int i = 0; i < qMin(16, int(values.size())); ++i) summary.append(values[i].isBool() ? (values[i].toBool() ? "true" : "false") : QString::number(values[i].toInt()));
                const QStringList cells{QString::number(device.value("unit").toInt()), name,
                    QStringLiteral("%1 / %2 项").arg(area.value("start").toInt()).arg(values.size()), summary.join(','), device.value("path").toString()};
                for (int col = 0; col < cells.size(); ++col) {
                    auto *item = data->item(row, col);
                    if (!item) data->setItem(row, col, new QTableWidgetItem(cells[col]));
                    else if (item->text() != cells[col]) item->setText(cells[col]);
                }
                ++row;
            }
        }
    };
    auto *timer = new QTimer(view); timer->setInterval(200);
    QObject::connect(timer, &QTimer::timeout, view, refresh); timer->start(); refresh();
    return view;
}
}
