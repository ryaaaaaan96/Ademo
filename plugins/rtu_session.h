#pragma once
#include "debugger/sdk.h"
#include "write_pdu.h"
#include "rtu_slave.h"

namespace debugger {
class RtuSession final : public ProtocolSession {
public:
    explicit RtuSession(ProtocolServices services);
    QString prepare(const SessionConfig &) override;
    bool prepareInBackground(const SessionConfig &config) const override { return config.role == "slave"; }
    QJsonObject snapshot() const override { return slave_ ? slave_->snapshot() : QJsonObject{{"role", "master"}}; }
    LocalState localState() const override { return slave_ ? slave_->localState() : LocalState{}; }
    void activate() override;
    void stop() override;
    void connectionLost(const QString &) override;
    void connectionRestored(bool resumePolling) override;
    void submit(const OperationRequest &) override;
    void onInput(const TraceRecord &) override;
    void onSent(quint64, bool, const QString &, qint64 observedUs) override;
    void tick() override;
private:
    std::unique_ptr<RtuSlave> slave_;
    enum class Phase { Idle, WaitingIdle, Sending, AwaitingResponse, RetryWait, Quarantine };
    struct PollTask { QString action; QJsonObject parameters; qint64 intervalUs; qint64 dueUs = 0; };
    QString configureSettings(const QJsonObject &);
    void begin(const OperationRequest &);
    void readTimeout();
    void finish(const QString &state, const QString &detail, QJsonObject result = {});
    void finishFrame();
    bool candidateComplete() const;
    ProtocolServices services_;
    OperationRequest request_;
    Phase phase_ = Phase::Idle;
    bool active_ = false;
    bool uncertain_ = false;
    bool brokenFrame_ = false;
    bool oversizedFrame_ = false;
    bool haveInput_ = false;
    bool trustedTiming_ = false;
    QByteArray buffer_;
    QList<quint64> sources_;
    QList<TraceRange> sourceRanges_;
    qint64 lastInputUs_ = 0;
    qint64 lastActivityUs_ = 0;
    qint64 deadlineUs_ = 0;
    qint64 interCharacterUs_ = 0;
    qint64 interFrameUs_ = 0;
    qint64 timeoutUs_ = 1000000;
    int unit_ = 0;
    int start_ = 0;
    int count_ = 0;
    int function_ = 3;
    QString area_ = "holding";
    WritePdu write_;
    bool writeSubmitted_ = false;
    QList<OperationRequest> pending_;
    QList<PollTask> polls_;
    qsizetype nextPoll_ = 0;
    int extraRetries_ = 2;
    int attempt_ = 1;
    qint64 retryIntervalUs_ = 100000;
    qint64 retryReadyUs_ = 0;
    QMap<QString, quint64> uncertainReads_; // 有界：记录曾耗尽的响应形状，不假定隔离能排除任意迟到。
    bool freshnessUncertain_ = false;
    bool allReadsUncertain_ = false;
    bool pollingPaused_ = false;
    QMap<quint64, qsizetype> pollOperations_;
    QString responseShape() const;
};
}
