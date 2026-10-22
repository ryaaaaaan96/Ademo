#pragma once
#include "debugger/sdk.h"
#include "write_pdu.h"

namespace debugger {
class RtuSession final : public ProtocolSession {
public:
    explicit RtuSession(ProtocolServices services);
    QString prepare(const SessionConfig &) override;
    void activate() override;
    void stop() override;
    void submit(const OperationRequest &) override;
    void onInput(const TraceRecord &) override;
    void onSent(quint64, bool, const QString &, qint64 observedUs) override;
    void tick() override;
private:
    enum class Phase { Idle, WaitingIdle, Sending, AwaitingResponse };
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
};
}
