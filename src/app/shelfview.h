#pragma once
// The bookshelf: shelves of covers, because a novelist writes books. Labeled
// shelves the writer arranges; drag books anywhere, drag shelves by their grip;
// drop manuscripts on a shelf to import them, an image on a book for its cover.
// A bound shelf is one book: its cover stands taller, the pages a published
// book carries stand beside it as thin leaves, and hovering shows each page it
// could still have.

#include <QJsonObject>
#include <QScrollArea>
#include <QWidget>

class QLabel;
class QLineEdit;
class QVBoxLayout;
class QPushButton;

namespace neosea {

class App;
class FlowLayout;
class ShelfRow;

class BookTile : public QWidget {
    Q_OBJECT
public:
    enum Kind { Book, Cover, Page, GhostPage, NewBook, PartSeam };
    BookTile(App *app, Kind kind, const QJsonObject &meta, const QString &shelfId, QWidget *parent);
    Kind kind() const { return m_kind; }
    QString bookId() const { return m_meta.value("id").toString(); }
    void setLabel(const QString &label) { m_label = label; update(); }
    void setPageKind(const QString &k) { m_pageKind = k; }
    QString pageKind() const { return m_pageKind; }
    void setBeforeId(const QString &id) { m_beforeId = id; }
    QString beforeId() const { return m_beforeId; }
    QString label() const { return m_label; }
    QSize sizeHint() const override;

signals:
    void activated(BookTile *tile);
    void menuRequested(BookTile *tile, QPoint globalPos);
    void fileDropped(BookTile *tile, const QString &path);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void paintBook(QPainter &p, QRectF r);
    void paintScript(QPainter &p, QRectF r);
    QImage coverImage();

    App *m_app;
    Kind m_kind;
    QJsonObject m_meta;
    QString m_shelfId, m_label, m_pageKind, m_beforeId;
    bool m_hover = false;
    QPoint m_pressPos;
    bool m_pressed = false;
};

class ShelfView : public QScrollArea {
    Q_OBJECT
public:
    explicit ShelfView(App *app, QWidget *parent = nullptr);
    void rebuild();

signals:
    void openBook(const QString &bookId);
    void importRequested();

private:
    friend class ShelfRow;
    void tileActivated(BookTile *tile);
    void tileMenu(BookTile *tile, QPoint globalPos);
    void newBookMenu(const QString &shelfId, QPoint globalPos);
    void shelfMenu(const QString &shelfId);
    void boundMenu(const QString &shelfId);
    void authorMenu();
    void addShelf();
    void importOnto(const QString &shelfId, const QStringList &paths);
    void setCoverFrom(const QString &bookId, const QString &path);
    void openPage(const QString &shelfId, const QString &bookId, const QString &label);
    void openTitlePage(const QString &shelfId, const QString &coverId);
    void addPage(const QString &shelfId, const QString &kind, const QString &beforeId = {});
    void bindShelf(const QString &shelfId);

    App *m_app;
    QWidget *m_content;
    QVBoxLayout *m_shelves;
    QPushButton *m_author;
};

} // namespace neosea
