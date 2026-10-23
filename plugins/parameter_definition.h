#pragma once
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace debugger {
// 单个设备可复用的参数声明；设备地址由应用动作指定，不从文件偷偷改绑。
struct ParameterDefinition {
    QString area;
    int address = 0;
    QString name;
    QString type;
    QString unit;
    QString description;
    QString group;
};
struct ParameterDefinitionResult {
    QList<ParameterDefinition> parameters;
    QString error;
};
inline const QStringList parameterAreas{"coils", "discrete", "holding", "input"};
inline constexpr int maximumDeviceParameters = 4096;
ParameterDefinitionResult parseParameterDefinition(const QJsonObject &);
ParameterDefinitionResult readParameterDefinition(const QString &path);
QJsonObject parameterDefinitionJson(const ParameterDefinition &);
}
