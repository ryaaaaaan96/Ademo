#include "debugger/file_editor.h"
#include "debugger/confirmation.h"
#include <QFileInfo>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

namespace debugger {
namespace {
QString chooseFile(QWidget *parent, const QString &title, const QString &path, bool saving)
{
    QPointer<QFileDialog> dialog = new QFileDialog(parent, title, path, "JSON (*.json)");
    dialog->setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog->setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog->setOption(QFileDialog::DontConfirmOverwrite, saving);
    const int result = dialog->exec();
    const auto selected = dialog && result == QDialog::Accepted ? dialog->selectedFiles().value(0) : QString();
    if (dialog) delete dialog;
    return selected;
}
}
FileEditorPage::FileEditorPage(SessionAccess access, FileDocumentEditor *editor, Apply apply, QWidget *parent)
    : FileEditorPage(std::move(access), editor, std::move(apply), Options{}, parent) {}
FileEditorPage::FileEditorPage(SessionAccess access, FileDocumentEditor *editor, Apply apply, Options options, QWidget *parent)
    : QWidget(parent), access_(std::move(access)), editor_(editor), apply_(std::move(apply)), options_(std::move(options))
{
    auto *layout = new QVBoxLayout(this);
    targetLabel_ = new QLabel(this); targetLabel_->setAccessibleName(QStringLiteral("文件目标设备"));
    layout->addWidget(targetLabel_);
    sourceLabel_ = new QLabel(this); sourceLabel_->setAccessibleName(QStringLiteral("当前文档来源"));
    sourceLabel_->setWordWrap(true); layout->addWidget(sourceLabel_);
    auto *files = new QHBoxLayout;
    create_ = new QPushButton(QStringLiteral("新建文件"), this);
    open_ = new QPushButton(QStringLiteral("打开文件"), this);
    saveAs_ = new QPushButton(QStringLiteral("另存为…"), this);
    files->addWidget(create_); files->addWidget(open_); files->addWidget(saveAs_); layout->addLayout(files);
    path_ = new QLineEdit(this); path_->setAccessibleName(QStringLiteral("编辑文件路径"));
    path_->setPlaceholderText(QStringLiteral("文件路径；保存不会应用或写入设备")); layout->addWidget(path_);
    layout->addWidget(editor_, 1);
    auto *actions = new QHBoxLayout;
    save_ = new QPushButton(QStringLiteral("保存文件"), this);
    applyButton_ = new QPushButton(QStringLiteral("应用到目标设备"), this);
    actions->addWidget(save_); actions->addWidget(applyButton_); layout->addLayout(actions);
    status_ = new QLabel(QStringLiteral("编辑内容来自当前参数声明；保存与应用是独立操作。"), this);
    status_->setAccessibleName(QStringLiteral("文件状态"));
    status_->setWordWrap(true); layout->addWidget(status_);
    editor_->changed = [this] { ++revision_; dirty_ = true; refresh(); };
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    connect(create_, &QPushButton::clicked, this, [this] { create(); });
    connect(open_, &QPushButton::clicked, this, [this] { open(); });
    connect(saveAs_, &QPushButton::clicked, this, [this] {
        const auto lifetime = access_.lifetime;
        QPointer<FileEditorPage> alive(this);
        const auto revision = revision_;
        busy_ = true; refresh();
        const auto path = chooseFile(this, QStringLiteral("另存编辑文件"), path_->text(), true);
        if (!alive) return;
        busy_ = false; refresh();
        if (revision == revision_ && !path.isEmpty()) save(path);
    });
    connect(applyButton_, &QPushButton::clicked, this, [this] {
        const auto lifetime = access_.lifetime;
        const auto document = editor_->document();
        auto error = editor_->validate(document);
        QPointer<FileEditorPage> alive(this);
        const auto apply = apply_;
        if (error.isEmpty() && apply) error = apply(target_, document, documentPath_);
        if (alive) status_->setText(error.isEmpty() ? QStringLiteral("已明确应用到目标设备，未发送设备命令。") : error);
    });
    if (access_.registerViewState) access_.registerViewState(this,
        [this] { return LocalState{false, dirty_, busy_, {}}; }, [this] { return resolveDraft(); });
    refresh();
}
bool FileEditorPage::bindTarget(const QString &id, const QString &label, const QJsonObject &document, const QString &path)
{
    const auto lifetime = access_.lifetime;
    if (id == target_) return true;
    QPointer<FileEditorPage> alive(this);
    if (!resolveDraft() || !alive) return false;
    const auto error = editor_->setDocument(document);
    if (!error.isEmpty()) { status_->setText(error); return false; }
    target_ = id; targetLabel_->setText(QStringLiteral("文件编辑目标：%1").arg(label));
    cleanDocument_ = document;
    path_->setText(path); documentPath_ = path; dirty_ = false; ++revision_; refresh(); return true;
}
bool FileEditorPage::resolveDraft()
{
    const auto lifetime = access_.lifetime;
    if (busy_) return false;
    if (!dirty_) return true;
    const auto revision = revision_;
    QPointer<FileEditorPage> alive(this);
    busy_ = true; refresh();
    const auto choice = showConfirmation(this, QMessageBox::Warning, QStringLiteral("未保存文件草稿"),
        QStringLiteral("文件编辑尚未保存。放弃只丢弃编辑草稿，不修改文件或运行参数。"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (!alive) return false;
    busy_ = false; refresh();
    if (revision_ != revision) return false;
    if (choice == QMessageBox::Save) { save(); return false; }
    if (choice != QMessageBox::Discard) return false;
    editor_->setDocument(cleanDocument_); path_->setText(documentPath_); ++revision_; dirty_ = false; refresh(); return true;
}
void FileEditorPage::create()
{
    const auto lifetime = access_.lifetime;
    QPointer<FileEditorPage> alive(this);
    if (!resolveDraft() || !alive) return;
    const auto document = editor_->emptyDocument();
    const auto error = editor_->setDocument(document);
    if (!error.isEmpty()) { status_->setText(error); return; }
    path_->clear(); dirty_ = true; ++revision_; refresh();
    status_->setText(QStringLiteral("新建编辑草稿，尚未保存或应用。"));
}
void FileEditorPage::open()
{
    const auto lifetime = access_.lifetime;
    if (busy_ || target_.isEmpty()) return;
    QPointer<FileEditorPage> alive(this);
    const auto revision = revision_;
    auto selected = path_->text();
    if (selected.isEmpty()) {
        busy_ = true; refresh();
        selected = chooseFile(this, QStringLiteral("打开编辑文件（不应用）"), {}, false);
        if (!alive) return;
        busy_ = false; refresh();
    }
    if (!alive || revision != revision_ || selected.isEmpty()) return;
    const auto path = QFileInfo(selected).absoluteFilePath();
    busy_ = true; refresh(); QPointer<FileEditorPage> self(this);
    const bool accepted = access_.background && access_.background([path, limit = options_.maximumBytes] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QJsonObject{{"error", file.errorString()}};
        const auto bytes = file.read(limit + 1);
        if (bytes.size() > limit || !file.atEnd() || file.error() != QFileDevice::NoError)
            return QJsonObject{{"error", QStringLiteral("文件读取失败或超过 %1 字节").arg(limit)}};
        QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return QJsonObject{{"error", QStringLiteral("JSON 无效：") + error.errorString()}};
        return QJsonObject{{"document", document.object()}};
    }, [self, path](QJsonObject result) {
        if (!self) return;
        self->busy_ = false;
        const auto document = result.value("document").toObject();
        auto error = result.contains("error") ? result["error"].toString() : self->editor_->validate(document);
        if (!error.isEmpty()) self->status_->setText(QStringLiteral("打开失败，原内容保留：") + error);
        else if (self->resolveDraft() && self) {
            error = self->editor_->setDocument(document);
            if (error.isEmpty()) {
                self->cleanDocument_ = document; self->dirty_ = false; ++self->revision_; self->path_->setText(path);
                self->documentPath_ = path;
                self->status_->setText(QStringLiteral("文件已打开到编辑页；未应用到设备。"));
            } else self->status_->setText(error);
        }
        if (self) self->refresh();
    });
    if (!accepted) { busy_ = false; status_->setText(QStringLiteral("后台文件任务繁忙，原内容保留。")); refresh(); }
}
void FileEditorPage::save(const QString &destination)
{
    const auto lifetime = access_.lifetime;
    if (busy_ || target_.isEmpty()) return;
    const auto document = editor_->document(); const auto error = editor_->validate(document);
    if (!error.isEmpty()) { status_->setText(error); return; }
    const auto selected = destination.isEmpty() ? path_->text() : destination;
    if (selected.trimmed().isEmpty()) { status_->setText(QStringLiteral("请指定文件路径。")); return; }
    const auto path = QFileInfo(selected).absoluteFilePath();
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (bytes.size() > options_.maximumBytes) { status_->setText(QStringLiteral("文件超过 %1 字节，未保存。").arg(options_.maximumBytes)); return; }
    if (options_.canSave) {
        QPointer<FileEditorPage> alive(this);
        const auto check = options_.canSave;
        const auto error = check(path);
        if (!alive) return;
        if (!error.isEmpty()) { status_->setText(error); return; }
    }
    const auto revision = revision_;
    if (QFileInfo::exists(path)) {
        QPointer<FileEditorPage> alive(this);
        busy_ = true; refresh();
        const auto choice = showConfirmation(this, QMessageBox::Question, QStringLiteral("覆盖文件"),
            QStringLiteral("确认用当前编辑快照覆盖以下文件？\n%1").arg(path), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (!alive) return;
        busy_ = false; refresh();
        if (choice != QMessageBox::Yes || revision != revision_) return;
        if (options_.canSave) {
            const auto check = options_.canSave;
            const auto error = check(path);
            if (!alive) return;
            if (!error.isEmpty()) { status_->setText(error); return; }
        }
    }
    busy_ = true; refresh();
    QPointer<FileEditorPage> self(this);
    const bool accepted = access_.background && access_.background([path, bytes] {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return QJsonObject{{"error", file.errorString()}};
        return QJsonObject{};
    }, [self, path, revision, document](QJsonObject result) {
        if (!self) return;
        self->busy_ = false;
        if (result.contains("error")) self->status_->setText(QStringLiteral("保存失败：") + result["error"].toString());
        else {
            self->path_->setText(path);
            self->documentPath_ = path;
            self->cleanDocument_ = document;
            if (self->revision_ == revision) self->dirty_ = false;
            self->status_->setText(self->dirty_ ? QStringLiteral("已保存开始时的快照；新编辑尚未保存。")
                                              : QStringLiteral("文件已保存；尚未应用，也未发送设备命令。"));
        }
        self->refresh();
    });
    if (!accepted) { busy_ = false; status_->setText(QStringLiteral("后台文件任务繁忙，草稿保留。")); refresh(); }
}
void FileEditorPage::refresh()
{
    sourceLabel_->setText(QStringLiteral("%1：%2").arg(dirty_ ? QStringLiteral("有未保存修改，最近确认来源") : QStringLiteral("当前文档来源"),
        documentPath_.isEmpty() ? QStringLiteral("当前参数声明，尚无文件") : documentPath_));
    save_->setEnabled(!target_.isEmpty() && !busy_);
    applyButton_->setEnabled(!target_.isEmpty() && !dirty_ && !busy_);
    path_->setEnabled(!busy_);
    create_->setEnabled(!target_.isEmpty() && !busy_); open_->setEnabled(!target_.isEmpty() && !busy_);
    saveAs_->setEnabled(!target_.isEmpty() && !busy_);
    targetLabel_->setToolTip(dirty_ ? QStringLiteral("未保存文件草稿") : QStringLiteral("没有未保存文件草稿"));
}
}
