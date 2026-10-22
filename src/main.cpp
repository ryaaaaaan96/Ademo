#include "debugger/host.h"
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    auto catalog = std::make_shared<debugger::PluginCatalog>();
    catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
    debugger::Workbench window(catalog);
    window.show();
    return app.exec();
}
