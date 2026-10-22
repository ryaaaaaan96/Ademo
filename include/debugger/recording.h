#pragma once
#include "trace_query.h"
namespace debugger {
struct RecordingOptions {
    QString directory;
    int queueItems = 1024;
    qint64 queueBytes = 8 * 1024 * 1024;
    qint64 diskBytes = 64 * 1024 * 1024;
    qint64 segmentBytes = 1024 * 1024;
    int batchItems = 64;
};
struct RecordingStats {
    QString state = "stopped";
    QString error;
    QString sessionId;
    QString recordingId;
    quint64 writtenEvents = 0;
    quint64 droppedEvents = 0;
    QDateTime lossFromUtc;
    QDateTime lossToUtc;
    quint64 rotatedEvents = 0;
    QDateTime rotationFromUtc;
    QDateTime rotationToUtc;
    int queuedItems = 0;
    qint64 queuedBytes = 0;
    qint64 retainedBytes = 0;
};
class DiskRecorder {
public:
    explicit DiskRecorder(RecordingOptions, QString sessionId, std::function<void()> beforeBatch = {});
    ~DiskRecorder();
    static QString validate(const RecordingOptions &);
    void append(QJsonObject); // 无磁盘 I/O；队列满时独立统计丢失。
    RecordingStats stats() const;
    std::shared_future<RecordingStats> stop(); // 异步排空，通信上下文不等待文件。
private:
    struct State;
    std::shared_ptr<State> state_;
};
struct ArchiveResult {
    bool ok = false;
    QString error;
    QStringList sessions;
    TraceSnapshot snapshot;
};
// 扫描和读取均在后台，最多两个并发任务，无无界等待队列。
std::shared_future<ArchiveResult> queryArchive(QString directory, QString sessionId, TraceFilter = {});
class DebugSession;
class RecordingPanel : public QWidget {
public:
    explicit RecordingPanel(DebugSession &, QWidget *parent = nullptr);
};
}
