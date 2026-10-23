#include "parameter_file_editor.h"
#include "parameter_definition.h"
#include <QComboBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace debugger {
namespace {
class ParameterFileEditor final : public FileDocumentEditor {
public:
    explicit ParameterFileEditor(QWidget *parent) : FileDocumentEditor(parent) {
        auto *layout = new QVBoxLayout(this);
        table_ = new QTableWidget(this); table_->setAccessibleName(QStringLiteral("参数文件内容"));
        table_->setColumnCount(7);
        table_->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("数据区"), QStringLiteral("PDU 地址"),
            QStringLiteral("类型"), QStringLiteral("单位"), QStringLiteral("说明"), QStringLiteral("分组")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table_->horizontalHeader()->setMinimumSectionSize(80);
        table_->setColumnWidth(1, 110); table_->setColumnWidth(2, 100);
        table_->setMinimumHeight(200);
        layout->addWidget(table_);
        auto *actions = new QHBoxLayout;
        auto *add = new QPushButton(QStringLiteral("添加文件参数"), this);
        auto *remove = new QPushButton(QStringLiteral("删除选中文件参数"), this);
        actions->addWidget(add); actions->addWidget(remove); layout->addLayout(actions);
        connect(add, &QPushButton::clicked, this, [this] {
            auto value = document(); auto rows = value["parameters"].toArray();
            if (rows.size() >= maximumDeviceParameters) return;
            int address = 0;
            while (std::any_of(rows.begin(), rows.end(), [address](const QJsonValue &item) {
                return item.toObject()["area"] == "holding" && item.toObject()["address"].toInt() == address;
            })) ++address;
            rows.append(QJsonObject{{"area", "holding"}, {"address", address}, {"type", "uint16"}});
            value["parameters"] = rows; setDocument(value); if (changed) changed();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int row = table_->currentRow(); if (row < 0) return;
            auto value = document(); auto rows = value["parameters"].toArray(); rows.removeAt(row);
            value["parameters"] = rows; setDocument(value); if (changed) changed();
        });
        connect(table_, &QTableWidget::cellChanged, this, [this] { if (!loading_ && changed) changed(); });
    }
    QJsonObject emptyDocument() const override {
        return {{"kind", "modbus-parameter-definition"}, {"version", 1}, {"parameters", QJsonArray{}}};
    }
    QString validate(const QJsonObject &document) const override { return parseParameterDefinition(document).error; }
    QJsonObject document() const override {
        auto result = source_; QJsonArray parameters;
        for (int row = 0; row < table_->rowCount(); ++row) {
            auto item = table_->item(row, 0)->data(Qt::UserRole).toJsonObject();
            const auto area = qobject_cast<QComboBox *>(table_->cellWidget(row, 1))->currentData().toString();
            item["name"] = table_->item(row, 0)->text(); item["area"] = area;
            item["address"] = qobject_cast<QSpinBox *>(table_->cellWidget(row, 2))->value();
            item["type"] = parameterAreas.indexOf(area) < 2 ? "bool" : "uint16";
            item["unit"] = table_->item(row, 4)->text(); item["description"] = table_->item(row, 5)->text();
            item["group"] = table_->item(row, 6)->text(); parameters.append(item);
        }
        result["parameters"] = parameters; return result;
    }
    QString setDocument(const QJsonObject &document) override {
        const auto parsed = parseParameterDefinition(document);
        if (!parsed.error.isEmpty()) return parsed.error;
        loading_ = true; source_ = document; table_->setRowCount(0);
        const auto raw = document["parameters"].toArray();
        for (int row = 0; row < parsed.parameters.size(); ++row) {
            const auto &p = parsed.parameters[row]; table_->insertRow(row);
            const QStringList text{p.name, {}, {}, p.type, p.unit, p.description, p.group};
            for (int col = 0; col < text.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(text[col]));
            table_->item(row, 0)->setData(Qt::UserRole, raw[row].toObject());
            table_->item(row, 3)->setFlags(table_->item(row, 3)->flags() & ~Qt::ItemIsEditable);
            auto *area = new QComboBox(table_); area->setAccessibleName(QStringLiteral("文件参数数据区"));
            for (const auto &name : parameterAreas) area->addItem(name, name);
            area->setCurrentIndex(area->findData(p.area)); table_->setCellWidget(row, 1, area);
            auto *address = new QSpinBox(table_); address->setAccessibleName(QStringLiteral("文件 PDU 地址"));
            address->setRange(0, 65535); address->setValue(p.address); table_->setCellWidget(row, 2, address);
            connect(area, &QComboBox::currentIndexChanged, this, [this, area, row] {
                const QSignalBlocker blocker(table_);
                table_->item(row, 3)->setText(area->currentIndex() < 2 ? "bool" : "uint16");
                if (!loading_ && changed) changed();
            });
            connect(address, &QSpinBox::valueChanged, this, [this] { if (!loading_ && changed) changed(); });
        }
        loading_ = false; return {};
    }
private:
    QTableWidget *table_;
    QJsonObject source_;
    bool loading_ = false;
};
}
FileDocumentEditor *createParameterFileEditor(QWidget *parent) { return new ParameterFileEditor(parent); }
}
