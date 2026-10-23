#include "debugger/host.h"
#include <QApplication>
#include "debugger/application_style.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    debugger::applyApplicationStyle(app);
    auto catalog = std::make_shared<debugger::PluginCatalog>();
    catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
    debugger::Workbench window(catalog);
    window.show();
    return app.exec();
}
