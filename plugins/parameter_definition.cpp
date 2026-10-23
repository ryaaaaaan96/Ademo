#include "parameter_definition.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace debugger {
ParameterDefinitionResult parseParameterDefinition(const QJsonObject &document)
{
    auto fail = [](const QString &error) { return ParameterDefinitionResult{{}, error}; };
    if (document.value("kind") != "modbus-parameter-definition" || document.value("version") != QJsonValue(1))
        return fail(QStringLiteral("参数文件须为 kind=modbus-parameter-definition、version=1；不能使用从站数据文件或原型文件"));
    if (!document.value("parameters").isArray() || document.value("parameters").toArray().size() > maximumDeviceParameters)
        return fail(QStringLiteral("parameters 须为数组，每个设备最多 4096 个参数"));
    ParameterDefinitionResult result;
    QSet<QString> addresses;
    for (const auto &entry : document.value("parameters").toArray()) {
        if (!entry.isObject()) return fail(QStringLiteral("每个参数须为对象"));
        const auto item = entry.toObject();
        const auto area = item.value("area").toString();
        const int address = item.value("address").toInt(-1);
        if (!parameterAreas.contains(area) || address < 0 || address > 65535)
            return fail(QStringLiteral("参数 area 或 PDU address 无效（地址须为 0..65535 整数）"));
        for (const auto &field : {"name", "type", "unit", "description", "group"})
            if (item.contains(field) && (!item.value(field).isString() || item.value(field).toString().size() > 1024))
                return fail(QStringLiteral("参数 %1 须为至多 1024 字符的字符串").arg(field));
        const auto expectedType = parameterAreas.indexOf(area) < 2 ? QString("bool") : QString("uint16");
        const auto type = item.value("type").toString(expectedType);
        if (type != expectedType) return fail(QStringLiteral("数据区 %1 的类型须为 %2").arg(area, expectedType));
        const auto key = area + '/' + QString::number(address);
        if (addresses.contains(key)) return fail(QStringLiteral("参数地址重复：%1").arg(key));
        addresses.insert(key);
        result.parameters.append({area, address, item.value("name").toString(), type,
            item.value("unit").toString(), item.value("description").toString(), item.value("group").toString()});
    }
    return result;
}
ParameterDefinitionResult readParameterDefinition(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, file.errorString()};
    constexpr qint64 maximumBytes = 1024 * 1024;
    const auto bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || !file.atEnd() || file.error() != QFileDevice::NoError)
        return {{}, QStringLiteral("读取失败或参数文件超过 1 MiB")};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {{}, QStringLiteral("参数 JSON 无效：") + error.errorString()};
    return parseParameterDefinition(document.object());
}
QJsonObject parameterDefinitionJson(const ParameterDefinition &parameter)
{
    return {{"area", parameter.area}, {"address", parameter.address}, {"name", parameter.name},
        {"type", parameter.type}, {"unit", parameter.unit}, {"description", parameter.description}, {"group", parameter.group}};
}
}
