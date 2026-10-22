#pragma once
#include "debugger/sdk.h"
#include <QMap>
#include <QSet>

namespace debugger {
struct SlaveDevice {
    int unit = 0;
    QString path;
    QJsonObject document;
    bool dirty = false;
    quint64 revision = 0;
    QJsonObject savedDocument;
};
QString validateSlaveDocument(const QJsonObject &document);
class RtuSlave final : public ProtocolSession {
public:
    explicit RtuSlave(ProtocolServices services) : services_(std::move(services)) {}
    QString prepare(const SessionConfig &) override;
    bool prepareInBackground(const SessionConfig &) const override { return true; }
    QJsonObject snapshot() const override;
    LocalState localState() const override;
    void activate() override;
    void stop() override;
    void submit(const OperationRequest &) override;
    void onInput(const TraceRecord &) override;
    void onSent(quint64, bool, const QString &, qint64) override;
    void tick() override;
private:
    void finishFrame();
    void publishBindings();
    bool validReplacement(int unit, const QJsonObject &, QString &error) const;
    ProtocolServices services_;
    QMap<int, SlaveDevice> devices_;
    QSet<int> fileJobs_;
    QMap<int, QString> fileTargets_;
    QByteArray buffer_;
    QList<quint64> sources_;
    QList<TraceRange> sourceRanges_;
    bool active_ = false, broken_ = false, oversized_ = false;
    qint64 lastInput_ = 0, lastActivity_ = 0, t15_ = 0, t35_ = 0;
    QByteArray response_;
    quint64 pendingSend_ = 0;
    QList<TraceRange> responseSources_, sentSources_;
};
}
