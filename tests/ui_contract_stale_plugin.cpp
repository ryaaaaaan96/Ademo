#include <QObject>
// 模拟旧 SDK 元数据；不声明当前接口，工厂不应被宿主调用。
class StaleUiContractPlugin final : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.ademo.debugger.Protocol/11.0")
};
#include "ui_contract_stale_plugin.moc"
