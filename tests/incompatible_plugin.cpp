#include "debugger/sdk.h"
#include <QObject>
using namespace debugger;
// 仅测试夹具，不进入产品插件目录。
class IncompatiblePlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"incompatible", "incompatible", "sdk0-wrong-build", {"master"}, {"serial"}, {}, {}};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices) override { qFatal("不兼容插件不得创建会话"); }
    QWidget *createView(SessionAccess, QWidget *) override { qFatal("不兼容插件不得创建视图"); }
};
#include "incompatible_plugin.moc"
