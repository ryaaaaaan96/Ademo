#pragma once
#include "debugger/host.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
using namespace debugger;
namespace {
QJsonArray bits(const char *text) { QJsonArray values; for (; *text; ++text) values.append(*text == '1'); return values; }
QJsonObject document(int holdingValue = 555) {
    return {{"version", 1}, {"note", "保留未知字段"}, {"areas", QJsonObject{
        {"coils", QJsonObject{{"start", 19}, {"values", bits("1011001111010110010011010111000011011")}}},
        {"discrete", QJsonObject{{"start", 196}, {"values", bits("0011010111011011101011")}}},
        {"holding", QJsonObject{{"start", 107}, {"values", QJsonArray{holdingValue, 0, 100}}}},
        {"input", QJsonObject{{"start", 8}, {"values", QJsonArray{10}}}}
    }}};
}
bool save(const QString &path, const QJsonObject &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(data).toJson()) > 0;
}
SessionConfig config(QJsonArray devices = {}, const QString &port = "COM9999") {
    return {"modbus.rtu", "serial", "slave", {{"port", port}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {{"devices", devices}}};
}
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    int opens = 0;
    bool accept = true;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { ++wire_.opens; wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override {
            if (!wire_.accept) return false;
            wire_.writes.append({id, bytes}); return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
};
struct Harness {
    QTemporaryDir directory;
    qint64 time = 0;
    Wire wire;
    DebugSession session;
    explicit Harness(std::shared_ptr<PluginCatalog> catalog)
        : session(std::move(catalog), nullptr, wire.factory(), [this] { return time; }) {}
    SessionConfig makeConfig() {
        const auto first = directory.filePath("device17.json"), second = directory.filePath("device18.json");
        save(first, document()); save(second, document(111));
        return config({QJsonObject{{"unit", 17}, {"path", first}}, QJsonObject{{"unit", 18}, {"path", second}}});
    }
    void advance(qint64 us) {
        time += us; QCoreApplication::processEvents(); session.poll(); QCoreApplication::processEvents();
    }
    void receive(const QByteArray &bytes, std::optional<qint64> idle = {}) {
        wire.events.received(ReceiveChunk(bytes, idle)); QCoreApplication::processEvents();
    }
    void complete() {
        wire.events.sent(wire.writes.last().first, true, {}); QCoreApplication::processEvents();
    }
};
}
