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
QScrollArea#sessionSidebar, QWidget#sessionSidebarBody { background: #edf2f6; border: none; }
QFrame#sidebarSection { background: white; border: 1px solid #dce5ec; border-radius: 6px; }
QLabel#sidebarHeading { color: #577087; font-size: 12px; font-weight: 600; padding-bottom: 2px; }
QLabel#sidebarSummary { color: #64788c; font-size: 12px; padding-top: 3px; }
QWidget#sessionSidebarBody QPushButton { padding: 3px 6px; font-size: 12px; }
QWidget#sessionSidebarBody QPushButton#start:enabled { background: #087f75; color: white; border-color: #087f75; }
QWidget#sessionSidebarBody QPushButton#start:hover:enabled { background: #066c64; }
QWidget#sessionSidebarBody QLabel#status { color: #526b80; font-size: 12px; }
QPushButton#sidebarToggle { background: #edf2f6; border: none; color: #577087; padding: 4px 0; }
QPushButton#sidebarToggle:hover { background: #dcebe9; color: #087d73; }
QSplitter::handle { background: #edf2f6; }
)");
}
} // namespace debugger
