#include "trace_codec.h"
#include <QJsonArray>
namespace debugger::tracecodec {
namespace {
QJsonArray ids(const QList<quint64> &values) {
    QJsonArray result; for (auto id : values) result.append(QString::number(id)); return result;
}
QList<quint64> readIds(const QJsonValue &value, int limit) {
    QList<quint64> result;
    for (const auto &id : value.toArray()) {
        if (result.size() >= limit) break;
        result.append(id.toString().toULongLong());
    }
    return result;
}
}
QJsonObject encode(const TraceRecord &record) {
    QJsonObject object{{"type", "trace"}, {"id", QString::number(record.id)}, {"generation", QString::number(record.generation)},
        {"operationId", QString::number(record.operationId)}, {"monotonicUs", QString::number(record.monotonicUs)},
        {"observedUtc", record.observedUtc.toString(Qt::ISODateWithMs)}, {"kind", record.kind},
        {"bytesHex", QString::fromLatin1(record.bytes.toHex())}, {"detail", record.detail}, {"metadata", record.metadata}};
    if (record.idleBeforeUs) object["idleBeforeUs"] = QString::number(*record.idleBeforeUs);
    return object;
}
QJsonObject encode(const Message &message) {
    QJsonArray ranges;
    for (const auto &range : message.sourceRanges) ranges.append(QJsonObject{{"recordId", QString::number(range.recordId)},
        {"offset", range.offset}, {"length", range.length}});
    return {{"type", "message"}, {"id", QString::number(message.id)}, {"operationId", QString::number(message.operationId)},
        {"observedUtc", message.observedUtc.toString(Qt::ISODateWithMs)}, {"monotonicUs", QString::number(message.monotonicUs)},
        {"status", message.status}, {"detail", message.detail}, {"fields", message.fields},
        {"sources", ids(message.sources)}, {"sourceRanges", ranges}};
}
QJsonObject encode(const OperationSnapshot &operation) {
    return {{"type", "operation"}, {"id", QString::number(operation.request.id)},
        {"action", operation.request.action}, {"parameters", operation.request.parameters}, {"state", operation.update.state},
        {"detail", operation.update.detail}, {"result", operation.update.result}, {"sendAccepted", operation.sendAccepted},
        {"localSent", operation.localSent}, {"finished", operation.finished}, {"observedRefs", ids(operation.observedRefs)},
        {"omittedObservedRefCount", QString::number(operation.omittedObservedRefCount)}};
}
TraceRecord decodeTrace(const QJsonObject &object) {
    TraceRecord record;
    record.id = object.value("id").toString().toULongLong();
    record.generation = object.value("generation").toString().toULongLong();
    record.operationId = object.value("operationId").toString().toULongLong();
    record.monotonicUs = object.value("monotonicUs").toString().toLongLong();
    record.observedUtc = QDateTime::fromString(object.value("observedUtc").toString(), Qt::ISODateWithMs);
    record.kind = object.value("kind").toString(); record.detail = object.value("detail").toString();
    record.bytes = QByteArray::fromHex(object.value("bytesHex").toString().toLatin1());
    record.metadata = object.value("metadata").toObject();
    if (object.contains("idleBeforeUs")) record.idleBeforeUs = object.value("idleBeforeUs").toString().toLongLong();
    return record;
}
Message decodeMessage(const QJsonObject &object) {
    Message message;
    message.id = object.value("id").toString().toULongLong();
    message.operationId = object.value("operationId").toString().toULongLong();
    message.monotonicUs = object.value("monotonicUs").toString().toLongLong();
    message.observedUtc = QDateTime::fromString(object.value("observedUtc").toString(), Qt::ISODateWithMs);
    message.status = object.value("status").toString(); message.detail = object.value("detail").toString();
    message.fields = object.value("fields").toObject(); message.sources = readIds(object.value("sources"), 256);
    for (const auto &value : object.value("sourceRanges").toArray()) {
        if (message.sourceRanges.size() >= 256) break;
        const auto range = value.toObject();
        message.sourceRanges.append({range.value("recordId").toString().toULongLong(), range.value("offset").toInt(-1), range.value("length").toInt(-1)});
    }
    return message;
}
OperationSnapshot decodeOperation(const QJsonObject &object) {
    OperationSnapshot operation;
    const auto id = object.value("id").toString().toULongLong();
    operation.request = {id, object.value("action").toString(), object.value("parameters").toObject()};
    operation.update = {id, object.value("state").toString(), object.value("detail").toString(), object.value("result").toObject()};
    operation.sendAccepted = object.value("sendAccepted").toBool(); operation.localSent = object.value("localSent").toBool();
    operation.finished = object.value("finished").toBool(); operation.observedRefs = readIds(object.value("observedRefs"), 512);
    operation.omittedObservedRefCount = object.value("omittedObservedRefCount").toString().toULongLong();
    return operation;
}
}
