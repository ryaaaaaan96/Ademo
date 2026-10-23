#include "archive_format.h"
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>
#include <QSet>
namespace debugger {
QString validateArchiveIndex(const QJsonObject &index)
{
    static const QRegularExpression integer("^(0|[1-9][0-9]*)$");
    static const QRegularExpression name("^segment-[0-9]{20}-[0-9a-f-]{36}\\.jsonl$");
    auto number = [](const QJsonValue &value, quint64 &result) {
        if (!value.isString() || !integer.match(value.toString()).hasMatch()) return false;
        bool ok = false; result = value.toString().toULongLong(&ok); return ok;
    };
    auto interval = [](const QJsonValue &first, const QJsonValue &last) {
        if (!first.isString() || !last.isString()) return false;
        const auto start = QDateTime::fromString(first.toString(), Qt::ISODateWithMs);
        const auto end = QDateTime::fromString(last.toString(), Qt::ISODateWithMs);
        return start.isValid() && end.isValid() && first.toString().endsWith('Z') && last.toString().endsWith('Z') && start <= end;
    };
    if (!index.value("schemaVersion").isDouble() || index.value("schemaVersion").toDouble() != 1)
        return QStringLiteral("不支持的归档索引版本");
    quint64 disk = 0, segmentLimit = 0, dropped = 0, rotated = 0;
    if (!number(index.value("diskBytes"), disk) || disk < 262144 || disk > 512ULL * 1024 * 1024
        || !number(index.value("segmentBytes"), segmentLimit) || segmentLimit < 1024 || segmentLimit > 8 * 1024 * 1024
        || segmentLimit > (disk - 131072) / 2 || !number(index.value("droppedEvents"), dropped)
        || !number(index.value("rotatedEvents"), rotated)) return QStringLiteral("归档容量或统计数值无效");
    if ((dropped && !interval(index.value("lossFromUtc"), index.value("lossToUtc")))
        || (rotated && !interval(index.value("rotationFromUtc"), index.value("rotationToUtc"))))
        return QStringLiteral("归档丢失或轮换时间范围无效");
    if (!index.value("segments").isArray() || index.value("segments").toArray().size() > 64)
        return QStringLiteral("归档分段列表无效");
    QSet<QString> names;
    for (const auto &value : index.value("segments").toArray()) {
        if (!value.isObject()) return QStringLiteral("归档分段条目无效");
        const auto segment = value.toObject(); const auto file = segment.value("name").toString();
        quint64 size = 0, events = 0;
        if (!name.match(file).hasMatch() || names.contains(file) || !number(segment.value("size"), size)
            || size > 8 * 1024 * 1024 || !number(segment.value("events"), events)) return QStringLiteral("归档分段路径或数值无效");
        names.insert(file);
        if (!segment.value("orphan").toBool() && (!size || !events || QUuid(segment.value("sessionId").toString()).isNull()
            || !interval(segment.value("firstUtc"), segment.value("lastUtc")))) return QStringLiteral("归档分段身份或时间无效");
    }
    for (const auto &key : {QStringLiteral("lossRanges"), QStringLiteral("rotationRanges")}) {
        if (!index.value(key).isArray() || index.value(key).toArray().size() > (key == "lossRanges" ? 64 : 32))
            return QStringLiteral("归档范围列表无效");
        for (const auto &value : index.value(key).toArray()) {
            if (!value.isObject()) return QStringLiteral("归档范围条目无效");
            const auto range = value.toObject(); quint64 first = 0, last = 0;
            const bool hasIds = range.contains("firstId") || range.contains("lastId");
            if ((key == "lossRanges" || hasIds) && (!number(range.value("firstId"), first) || !number(range.value("lastId"), last)
                || !first || last < first)) return QStringLiteral("归档记录 ID 范围无效");
            if (QUuid(range.value("sessionId").toString()).isNull() || !interval(range.value("fromUtc"), range.value("toUtc")))
                return QStringLiteral("归档范围身份或时间无效");
        }
    }
    for (const auto &key : {QStringLiteral("lossRangesTruncated"), QStringLiteral("rotationRangesTruncated"), QStringLiteral("archiveGap")})
        if (!index.value(key).isBool()) return QStringLiteral("归档完整性标记无效");
    return {};
}
}
