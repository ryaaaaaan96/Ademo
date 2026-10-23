#include "debugger/application_style.h"
#include <QApplication>
#include <QFont>

namespace debugger {
void applyApplicationStyle(QApplication &app) {
    app.setFont(QFont("Microsoft YaHei", 9));
    app.setStyle("Fusion");
    app.setStyleSheet(R"(
QWidget { font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 13px; color: #22344a; }
QMainWindow { background: #e8eef4; }
QDockWidget::title { background: #d7e4ef; padding: 5px; }
QTabWidget::pane { border: 1px solid #d7e1eb; background: white; }
QTabBar::tab { padding: 6px 12px; background: #e9eff5; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { background: white; color: #087d73; border-bottom: 2px solid #139d90; }
QPushButton, QToolButton { padding: 4px 7px; border: 1px solid #cbd7e3; border-radius: 3px; background: white; }
QPushButton:hover, QToolButton:hover { background: #e6f3f1; border-color: #65b4aa; }
QPushButton:disabled, QToolButton:disabled { color: #a3afbc; background: #f1f4f7; }
QComboBox, QSpinBox, QLineEdit { padding: 3px; border: 1px solid #cbd7e3; border-radius: 3px; background: white; }
QTableWidget { background: white; alternate-background-color: #f5f8fb; gridline-color: #e8edf2; selection-background-color: #d5e9f8; selection-color: #17364e; }
QHeaderView::section { background: #edf3f8; padding: 5px 4px; border: 0; border-bottom: 1px solid #d6e0ea; color: #4e657d; }
QTreeWidget { background: #f5f8fb; border: none; }
QTreeWidget::item { padding: 5px 3px; }
QTreeWidget::item:selected { color: #096d66; background: #deefec; }
QPlainTextEdit { background: white; color: #22344a; border: 1px solid #d7e1eb; }
)");
}
} // namespace debugger
