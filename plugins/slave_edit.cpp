#include "rtu_slave.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace debugger {
namespace {
const QStringList localActions{"slave-edit-value", "slave-replace-area", "slave-import", "slave-save", "slave-discard"};
constexpr qint64 fileLimit = 4 * 1024 * 1024;
QByteArray encoded(const QJsonObject &document) { return QJsonDocument(document).toJson(QJsonDocument::Compact); }
QString writeFile(const QString &path, const QJsonObject &document) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    const auto bytes = encoded(document);
    if (file.write(bytes) != bytes.size() || !file.commit()) return file.errorString();
    return {};
}
QJsonObject fileTask(QString action, QString path, SlaveDevice captured, bool saveFirst, QStringList otherPaths) {
    QJsonObject result;
    const auto canonical = QFileInfo(path).canonicalFilePath();
    for (const auto &other : otherPaths)
        if (!canonical.isEmpty() && canonical.compare(QFileInfo(other).canonicalFilePath(), Qt::CaseInsensitive) == 0)
            return {{"error", QStringLiteral("此文件已被其他设备绑定或用于文件操作")}};
    if (saveFirst) {
        const auto error = writeFile(captured.path, captured.document);
        if (!error.isEmpty()) return {{"error", QStringLiteral("替换前保存失败：") + error}};
        result["savedFirst"] = true;
    }
    if (action == "slave-save") {
        const auto error = writeFile(path, captured.document);
        if (!error.isEmpty()) result["error"] = error;
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result["error"] = file.errorString(); return result; }
    const auto bytes = file.read(fileLimit + 1);
    if (bytes.size() > fileLimit || !file.atEnd() || file.error() != QFileDevice::NoError) {
        result["error"] = QStringLiteral("文件读取失败或超过 4 MiB"); return result;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    QString error = parse.error == QJsonParseError::NoError && document.isObject()
        ? validateSlaveDocument(document.object()) : QStringLiteral("JSON 无效：") + parse.errorString();
    if (!error.isEmpty()) result["error"] = error;
    else result["document"] = document.object();
    return result;
}
}
LocalState RtuSlave::localState() const {
    bool dirty = false;
    for (const auto &device : devices_) dirty = dirty || device.dirty;
    return {true, dirty, !fileJobs_.isEmpty(), localActions};
}
bool RtuSlave::validReplacement(int unit, const QJsonObject &document, QString &error) const {
    error = validateSlaveDocument(document);
    if (!error.isEmpty()) return false;
    const auto size = encoded(document).size();
    qint64 total = size;
    for (const auto &device : devices_) if (device.unit != unit) total += encoded(device.document).size();
    if (size > fileLimit || total > 16 * 1024 * 1024) {
        error = QStringLiteral("运行数据超过单设备 4 MiB / 会话 16 MiB 上限"); return false;
    }
    return true;
}
void RtuSlave::publishBindings() {
    if (!services_.publishConfiguration) return;
    QJsonArray bindings;
    for (const auto &device : devices_) bindings.append(QJsonObject{{"unit", device.unit}, {"path", device.path}});
    configuration_["devices"] = bindings;
    services_.publishConfiguration(configuration_);
}
void RtuSlave::submit(const OperationRequest &request) {
    const auto unit = request.parameters.value("unit").toInt(-1);
    auto report = [this, id = request.id](QString state, QString detail) {
        services_.publish({id, std::move(state), std::move(detail), {}});
    };
    if (!localActions.contains(request.action) || !devices_.contains(unit)) {
        report("invalid", QStringLiteral("需要有效的本地数据动作与已加载设备")); return;
    }
    auto &device = devices_[unit];
    if (request.action == "slave-edit-value" || request.action == "slave-replace-area") {
        auto document = device.document;
        auto areas = document.value("areas").toObject();
        const auto name = request.parameters.value("area").toString();
        if (!QStringList{"coils", "discrete", "holding", "input"}.contains(name)) {
            report("invalid", QStringLiteral("无效数据区")); return;
        }
        auto area = areas.value(name).toObject();
        if (request.action == "slave-edit-value") {
            auto values = area.value("values").toArray();
            const auto address = request.parameters.value("address").toInt(-1);
            const auto offset = address - area.value("start").toInt();
            if (address < 0 || offset < 0 || offset >= values.size()) {
                report("invalid", QStringLiteral("编辑地址不在当前范围内")); return;
            }
            values[offset] = request.parameters.value("value");
            area["values"] = values;
        } else {
            area["start"] = request.parameters.value("start");
            area["values"] = request.parameters.value("values");
        }
        areas[name] = area; document["areas"] = areas;
        QString error;
        if (!validReplacement(unit, document, error)) { report("invalid", error); return; }
        device.document = std::move(document); ++device.revision; device.dirty = true;
        report("success", QStringLiteral("运行数据已生效，尚未保存文件；没有设备写入")); return;
    }
    if (fileJobs_.contains(unit)) { report("invalid", QStringLiteral("此设备文件操作尚未结束")); return; }
    if (request.action == "slave-discard") {
        device.document = device.savedDocument; ++device.revision; device.dirty = false;
        report("success", QStringLiteral("已放弃未保存修改，恢复最近加载或保存的数据")); return;
    }
    const bool importing = request.action == "slave-import";
    const auto decision = request.parameters.value("decision").toString();
    if (importing && device.dirty && decision != "save" && decision != "discard") {
        report(decision == "cancel" ? "cancelled" : "needs-decision", QStringLiteral("存在未保存修改，请选择保存、放弃或取消")); return;
    }
    const auto path = request.parameters.value("path").toString(device.path);
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        report("invalid", QStringLiteral("文件路径必须为非空绝对路径")); return;
    }
    const auto normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList otherPaths;
    for (const auto &other : devices_) if (other.unit != unit) {
        otherPaths.append(other.path);
        if (fileTargets_.contains(other.unit)) otherPaths.append(fileTargets_[other.unit]);
    }
    for (const auto &other : otherPaths) {
        if (normalized.compare(QDir::cleanPath(other), Qt::CaseInsensitive) == 0) {
            report("invalid", QStringLiteral("每个设备必须绑定独立文件")); return;
        }
    }
    if (!services_.background) { report("invalid", QStringLiteral("宿主没有后台文件服务")); return; }
    const auto captured = device;
    fileJobs_.insert(unit);
    fileTargets_[unit] = normalized;
    const bool accepted = services_.background(
        [captured, action = request.action, normalized, otherPaths, saveFirst = importing && device.dirty && decision == "save"] {
            return fileTask(action, normalized, captured, saveFirst, otherPaths);
        },
        [this, request, captured, normalized, importing](QJsonObject result) {
            fileJobs_.remove(captured.unit);
            fileTargets_.remove(captured.unit);
            auto &current = devices_[captured.unit];
            if (result.value("savedFirst").toBool()) {
                current.savedDocument = captured.document;
                if (current.revision == captured.revision) current.dirty = false;
            }
            QString error = result.value("error").toString();
            if (error.isEmpty() && importing && current.revision != captured.revision)
                error = QStringLiteral("文件读取期间运行数据已变化，未替换，请重新选择导入");
            const auto replacement = result.value("document").toObject();
            if (error.isEmpty() && importing) validReplacement(captured.unit, replacement, error);
            if (!error.isEmpty()) {
                services_.publish({request.id, "file-error", QStringLiteral("设备 %1，文件 %2：%3").arg(captured.unit).arg(normalized, error), {}});
                return;
            }
            current.path = normalized;
            if (importing) {
                current.document = replacement; current.savedDocument = replacement;
                ++current.revision; current.dirty = false;
            } else {
                current.savedDocument = captured.document;
                current.dirty = current.revision != captured.revision;
            }
            publishBindings();
            services_.publish({request.id, "success", importing ? QStringLiteral("已校验并整体导入，仅替换目标设备；后续请求立即使用新数据")
                : current.dirty ? QStringLiteral("已保存捕获快照；保存期间的新修改仍未保存") : QStringLiteral("文件保存成功，没有设备写入"),
                {{"unit", captured.unit}, {"path", normalized}, {"dirty", current.dirty}}});
        });
    if (!accepted) { fileJobs_.remove(unit); fileTargets_.remove(unit); report("invalid", QStringLiteral("后台文件任务繁忙，请稍后重试")); return; }
    report("working", importing ? QStringLiteral("后台读取并完整校验 JSON，通信继续") : QStringLiteral("后台原子保存快照，通信继续"));
}
}
