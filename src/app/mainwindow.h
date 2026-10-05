#pragma once
// The window: the bookshelf or the open book, the menus that stay out of the
// writing room, and the toast at the foot.

#include <QMainWindow>
#include <QTimer>

class QStackedWidget;
class QActionGroup;

namespace neosea {

class App;
class ShelfView;
class EditorView;
class Toast;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(App *app, QWidget *parent = nullptr);

public slots:
    void openBook(const QString &bookId);
    void backToShelf();
    void exportBook(const QString &format, const QString &chId = {});
    void exportShelf(const QString &shelfId, bool bound);
    void openGoals();
    void importBooks();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void buildMenus();
    void firstRun();
    void applyTheme();
    void showHelp();
    void showAbout();
    void chooseLibraryFolder();
    void reshelve();
    void setLibraryValue(const QString &key, const QJsonValue &v);
    QString saveTarget(const QString &defaultName, const QString &format);

    App *m_app;
    QStackedWidget *m_views;
    ShelfView *m_shelf;
    EditorView *m_editor;
    Toast *m_toast;
    QTimer m_libraryLook;
};

} // namespace neosea
