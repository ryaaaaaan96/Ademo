#include "session_window.h"
#include <QAction>
#include <QCloseEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QCursor>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace debugger {
SessionWindow::SessionWindow(QWidget *content, QWidget *parent) : QMainWindow(parent)
{
    setCentralWidget(content);
    setMinimumSize(400, 320);
}
void SessionWindow::closeEvent(QCloseEvent *event)
{
    QPointer<SessionWindow> alive(this);
    const auto close = requestClose;
    if (!close || !close() || !alive) { event->ignore(); return; }
    event->accept();
}
#ifdef Q_OS_WIN
bool SessionWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result)
{
    const auto *msg = static_cast<MSG *>(message);
    constexpr UINT dockCommand = 0x1f10; // 自定义系统菜单命令，避开 Windows 保留范围。
    if (isWindow() && requestDock) {
        if (msg->message == WM_INITMENU) {
            const auto menu = GetSystemMenu(msg->hwnd, FALSE);
            if (menu && reinterpret_cast<HMENU>(msg->wParam) == menu
                && GetMenuState(menu, dockCommand, MF_BYCOMMAND) == UINT(-1)) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, dockCommand, L"停靠到工作台");
            }
        }
        if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xfff0) == dockCommand) {
            // 先退出原生菜单回调，再改变 QWidget 的窗口类型及父对象。
            QTimer::singleShot(0, this, [this] { if (isWindow() && requestDock) requestDock(); });
            *result = 0; return true;
        }
    }
    if (isWindow() && dockDrag) {
        if (msg->message == WM_ENTERSIZEMOVE) { captionMoving_ = false; dragStartGeometry_ = frameGeometry(); }
        if (msg->message == WM_MOVING) {
            captionMoving_ = true; dockDrag(QCursor::pos(), DockDragPhase::Preview);
        }
        if (msg->message == WM_EXITSIZEMOVE || msg->message == WM_CANCELMODE) {
            const bool drop = msg->message == WM_EXITSIZEMOVE && captionMoving_
                && frameGeometry() != dragStartGeometry_ && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000);
            captionMoving_ = false; const auto position = QCursor::pos();
            // 等 Windows 原生移动循环退出后再改变窗口归属。
            QTimer::singleShot(0, this, [this, position, drop] {
                if (dockDrag) dockDrag(position, drop ? DockDragPhase::Drop : DockDragPhase::Cancel);
            });
        }
    }
    return QMainWindow::nativeEvent(type, message, result);
}
#endif
SessionDock::SessionDock(QWidget *content, const QString &title, QMainWindow *owner)
    : QDockWidget(title, owner), content_(content), owner_(owner)
{
    container_ = new QWidget(this);
    layout_ = new QVBoxLayout(container_); layout_->setContentsMargins(0, 0, 0, 0);
    window_ = new SessionWindow(content, container_);
    window_->setWindowTitle(title); window_->setWindowFlags(Qt::Widget);
    layout_->addWidget(window_); setWidget(container_);
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable);
    window_->requestClose = [this] { const auto close = requestClose; return close && close(); };
    auto *floatAction = new QAction(QStringLiteral("浮动"), this);
    floatAction->setObjectName("floatSessionAction");
    addAction(floatAction); setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(floatAction, &QAction::triggered, this, [this] { detach(); centerFloating(); });
    dockAction_ = new QAction(QStringLiteral("停靠到工作台"), this);
    dockAction_->setObjectName("dockSessionAction"); dockAction_->setVisible(false);
    window_->addAction(dockAction_); window_->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(dockAction_, &QAction::triggered, this, [this] { attach(); });
    window_->requestDock = [this] { attach(); };
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating && !changing_) QTimer::singleShot(0, this, [this] { if (isFloating()) detach(); });
    });
}
SessionDock::~SessionDock() { delete window_; }
void SessionDock::present()
{
    open_ = true;
    if (detached_) {
        if (window_->isMinimized()) window_->showNormal(); else window_->show();
        constrainRestoredFrame();
        window_->raise(); window_->activateWindow();
    } else { window_->show(); show(); raise(); }
    if (presentationChanged) presentationChanged();
}
void SessionDock::hideSession()
{
    open_ = false; window_->hide(); hide();
    if (presentationChanged) presentationChanged();
}
void SessionDock::centerFloating(int offset)
{
    if (!detached_) return;
    const auto available = owner_->screen()->availableGeometry();
    const auto extra = window_->frameGeometry().size() - window_->size();
    window_->resize(qMin(780, available.width() - extra.width()), qMin(820, available.height() - extra.height()));
    const auto frame = window_->frameGeometry().size();
    auto position = owner_->frameGeometry().center() - QPoint(frame.width() / 2, frame.height() / 2) + QPoint(offset, offset);
    position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - frame.width() + 1)));
    position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - frame.height() + 1)));
    window_->move(position);
}
void SessionDock::restoreFloatingGeometry(const QByteArray &saved)
{
    if (!detached_ || saved.isEmpty()) return;
    if (window_->restoreGeometry(saved)) {
        restoredGeometryNeedsClamp_ = true;
        constrainRestoredFrame();
    }
}
void SessionDock::constrainRestoredFrame()
{
    if (!restoredGeometryNeedsClamp_ || !detached_ || !window_->isVisible()) return;
    restoredGeometryNeedsClamp_ = false;
    if (window_->isMaximized() || window_->isFullScreen()) return;
    const auto frame = window_->frameGeometry();
    auto *screen = window_->screen();
    if (!screen || !screen->availableGeometry().intersects(frame)) screen = owner_->screen();
    if (!screen) return;
    const auto available = screen->availableGeometry();
    const QPoint clamped(qBound(available.left(), frame.left(), qMax(available.left(), available.right() - frame.width() + 1)),
                         qBound(available.top(), frame.top(), qMax(available.top(), available.bottom() - frame.height() + 1)));
    if (clamped != frame.topLeft()) window_->move(window_->pos() + clamped - frame.topLeft());
}
void SessionDock::detach()
{
    if (detached_) return;
    changing_ = true; setFloating(false);
    layout_->removeWidget(window_);
    window_->setParent(owner_, Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    detached_ = true; dockAction_->setVisible(true); hide();
    if (open_ && owner_->isVisible()) { window_->show(); centerFloating(); }
    changing_ = false;
    if (presentationChanged) presentationChanged();
}
void SessionDock::attach()
{
    if (!detached_) return;
    window_->setWindowState(Qt::WindowNoState); window_->hide();
    window_->setParent(container_, Qt::Widget); layout_->addWidget(window_);
    detached_ = false; dockAction_->setVisible(false);
    if (open_) { window_->show(); show(); raise(); }
    if (presentationChanged) presentationChanged();
}
void SessionDock::closeEvent(QCloseEvent *event)
{
    QPointer<SessionDock> alive(this);
    const auto close = requestClose;
    if (!close || !close() || !alive) { event->ignore(); return; }
    event->accept();
}
}
