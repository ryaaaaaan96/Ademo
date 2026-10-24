#pragma once
#include <QDockWidget>
#include <QMainWindow>
#include <functional>

class QVBoxLayout;
class QAction;
namespace debugger {
enum class DockDragPhase { Preview, Drop, Cancel };
// 停靠壳与独立窗口共享同一内容；呈现变化不创建或停止调试会话。
class SessionWindow final : public QMainWindow {
public:
    explicit SessionWindow(QWidget *content, QWidget *parent);
    std::function<bool()> requestClose;
    std::function<void()> requestDock;
    std::function<void(QPoint, DockDragPhase)> dockDrag;
protected:
    void closeEvent(QCloseEvent *) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &, void *, qintptr *) override;
private:
    bool captionMoving_ = false;
    QRect dragStartGeometry_;
#endif
};
class SessionDock final : public QDockWidget {
public:
    SessionDock(QWidget *content, const QString &title, QMainWindow *owner);
    ~SessionDock() override;
    SessionWindow *window() const { return window_; }
    QWidget *content() const { return content_; }
    bool detached() const { return detached_; }
    bool open() const { return open_; }
    void present();
    void hideSession();
    void detach();
    void attach();
    void centerFloating(int offset = 0);
    void restoreFloatingGeometry(const QByteArray &);
    std::function<bool()> requestClose;
    std::function<void()> presentationChanged;
protected:
    void closeEvent(QCloseEvent *) override;
private:
    QWidget *content_;
    SessionWindow *window_;
    QWidget *container_;
    QVBoxLayout *layout_;
    QMainWindow *owner_;
    QAction *dockAction_;
    bool detached_ = false;
    bool changing_ = false;
    bool open_ = true;
    bool restoredGeometryNeedsClamp_ = false;
    void constrainRestoredFrame();
};
}
