#pragma once
#include "sdk.h"

class QLabel;
class QLineEdit;
class QPushButton;
namespace debugger {
// 插件提供内容、格式及校验；框架只处理文件流程和明确的设备目标。
class FileDocumentEditor : public QWidget {
public:
    using QWidget::QWidget;
    virtual QJsonObject document() const = 0;
    virtual QString setDocument(const QJsonObject &) = 0;
    virtual QJsonObject emptyDocument() const = 0;
    virtual QString validate(const QJsonObject &) const = 0;
    std::function<void()> changed;
};
class FileEditorPage final : public QWidget {
public:
    using Apply = std::function<QString(QString target, const QJsonObject &, QString path)>;
    FileEditorPage(SessionAccess, FileDocumentEditor *, Apply, QWidget *parent = nullptr);
    bool bindTarget(const QString &id, const QString &label, const QJsonObject &, const QString &path);
    bool resolveDraft();
private:
    void refresh();
    void save(const QString &destination = {});
    void open();
    void create();
    SessionAccess access_;
    FileDocumentEditor *editor_;
    Apply apply_;
    QString target_;
    QString documentPath_;
    QJsonObject cleanDocument_;
    quint64 revision_ = 0;
    bool dirty_ = false, busy_ = false;
    QLabel *targetLabel_, *sourceLabel_, *status_;
    QLineEdit *path_;
    QPushButton *save_, *applyButton_;
    QPushButton *create_, *open_, *saveAs_;
};
}
