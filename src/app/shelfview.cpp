#include "app/shelfview.h"

#include "app/app.h"
#include "app/dialogs.h"
#include "app/flowlayout.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/covers.h"
#include "core/exporter.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QScrollBar>
#include <QDialog>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace neosea {

namespace {

constexpr int kTileW = 104, kTileH = 150, kLift = 5;
const char *kBookMime = "application/x-neo-book";
const char *kShelfMime = "application/x-neo-shelf";

bool isManuscript(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return QStringList{"docx", "txt", "md", "fountain", "fdx"}.contains(ext);
}

bool isImage(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return QStringList{"png", "jpg", "jpeg", "webp"}.contains(ext);
}

QStringList localPaths(const QMimeData *m)
{
    QStringList out;
    for (const QUrl &u : m->urls())
        if (u.isLocalFile()) out << u.toLocalFile();
    return out;
}

QString coverMode(const QJsonObject &meta)
{
    const QString m = meta.value("coverMode").toString();
    const bool hasImage = !meta.value("coverImage").toString().isEmpty();
    if (m == "image" && hasImage) return "image";
    if (m == "abstract") return "abstract";
    return hasImage ? "image" : "abstract";
}

// a page's name runs up its spine; a long one is set smaller
int spineSize(const QString &label) { return label.size() > 17 ? 8 : label.size() > 12 ? 9 : 10; }

} // namespace

// ---------------------------------------------------------------------------
// a tile on the shelf

BookTile::BookTile(App *app, Kind kind, const QJsonObject &meta, const QString &shelfId, QWidget *parent)
    : QWidget(parent), m_app(app), m_kind(kind), m_meta(meta), m_shelfId(shelfId)
{
    setCursor(Qt::PointingHandCursor);
    setAcceptDrops(kind == Book || kind == Cover);
    setAttribute(Qt::WA_Hover);
    if (kind == Book || kind == Cover) {
        const int goal = meta.value("wordGoal").toInt();
        const QString title = meta.value("title").toString();
        setToolTip(goal > 0 ? t("{title} — {count} / {goal} words", {{"title", title}, {"count", meta.value("wordCount").toInt()}, {"goal", goal}})
                            : title);
    }
}

QSize BookTile::sizeHint() const
{
    switch (m_kind) {
    case Page:
    case GhostPage: return {34, kTileH + kLift};
    case PartSeam: return {14, kTileH + kLift};
    case Cover: return {int(kTileW * 1.12), int(kTileH * 1.12) + kLift};
    default: return {kTileW, kTileH + kLift};
    }
}

QImage BookTile::coverImage()
{
    if (coverMode(m_meta) != "image") return {};
    const QString path = m_app->lib().coverPath(bookId(), m_meta.value("coverImage").toString());
    static QHash<QString, QImage> cache;
    if (path.isEmpty()) return {};
    if (!cache.contains(path)) {
        QImageReader r(path);
        r.setAutoTransform(true);
        QImage img = r.read();
        if (!img.isNull()) img = img.scaled(416, 600, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        cache.insert(path, img);
    }
    return cache.value(path);
}

void BookTile::paintBook(QPainter &p, QRectF r)
{
    QPainterPath shape;
    shape.addRoundedRect(r, 4, 4);
    // the shadow it casts on the shelf
    p.fillPath(shape.translated(2, 3), QColor(0, 0, 0, 90));
    p.save();
    p.setClipPath(shape);
    QJsonObject meta = m_meta;
    if (meta.value("author").toString().isEmpty()) meta.insert("author", QString());
    covers::paint(p, r, meta, coverImage());
    // the spine's shade along the left edge
    QLinearGradient spine(r.left(), 0, r.left() + 6, 0);
    spine.setColorAt(0, QColor(0, 0, 0, 115));
    spine.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(r.left(), r.top(), 6, r.height()), spine);
    // the word goal: a subtle bar along the foot
    const int goal = m_meta.value("wordGoal").toInt();
    if (goal > 0) {
        const double pct = std::min(1.0, m_meta.value("wordCount").toDouble() / goal);
        p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width(), 3), QColor(0, 0, 0, 30));
        p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width() * pct, 3), theme().accent);
    }
    p.restore();
    if (m_hover && m_kind == Book) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 170));
        p.drawText(QRectF(r.right() - 18, r.top() + 2, 16, 16), Qt::AlignCenter, QStringLiteral("↻"));
    }
}

void BookTile::paintScript(QPainter &p, QRectF r)
{
    QPainterPath shape;
    shape.addRoundedRect(r, 2, 2);
    p.fillPath(shape.translated(2, 3), QColor(0, 0, 0, 90));
    p.fillPath(shape, QColor("#f6f4ee"));
    // two brass brads down the spine side
    p.setPen(Qt::NoPen);
    for (double y : {r.top() + r.height() * 0.18, r.top() + r.height() * 0.82}) {
        QRadialGradient g(QPointF(r.left() + 9, y), 4);
        g.setColorAt(0, QColor("#e8d18f"));
        g.setColorAt(1, QColor("#9c7c35"));
        p.setBrush(g);
        p.drawEllipse(QPointF(r.left() + 9, y), 3.5, 3.5);
    }
    p.setBrush(QColor("#d8d3c6"));
    p.drawEllipse(QPointF(r.left() + 9, r.center().y()), 2.5, 2.5);
    const QString title = isUntitled(m_meta.value("title").toString()) ? t("Untitled") : m_meta.value("title").toString();
    QFont f(kScriptFamily);
    f.setPixelSize(title.size() > 36 ? 9 : 11);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#1c1c1c"));
    p.drawText(r.adjusted(18, 30, -8, -40), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title.toUpper());
    f.setPixelSize(8);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#555555"));
    p.drawText(r.adjusted(18, 0, -8, -16), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, m_meta.value("author").toString());
}

void BookTile::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double lift = (m_hover && m_kind != GhostPage && m_kind != PartSeam) ? 0 : kLift;
    QRectF r(0, lift, width(), height() - kLift);
    switch (m_kind) {
    case NewBook: {
        r.adjust(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(r, 4, 4);
        p.fillPath(path, QColor(255, 255, 255, 5));
        const QColor c = m_hover ? theme().accent : QColor("#4a4a4a");
        p.setPen(QPen(c, 1, Qt::DashLine));
        p.drawPath(path);
        QFont f = font();
        f.setPixelSize(34);
        f.setWeight(QFont::ExtraLight);
        p.setFont(f);
        p.setPen(c);
        p.drawText(r, Qt::AlignCenter, "+");
        break;
    }
    case Page: {
        QPainterPath path;
        path.addRoundedRect(r, 2, 2);
        p.fillPath(path.translated(1, 2), QColor(0, 0, 0, 100));
        p.fillPath(path, m_pageKind == "part" ? QColor("#e6dfcf") : QColor("#efe9dc"));
        p.save();
        p.translate(r.center());
        p.rotate(-90);
        QFont f = font();
        f.setPixelSize(spineSize(m_label));
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        p.setPen(QColor("#6f675b"));
        p.drawText(QRectF(-r.height() / 2 + 8, -r.width() / 2, r.height() - 16, r.width()), Qt::AlignCenter, m_label);
        p.restore();
        break;
    }
    case GhostPage: {
        if (!parentWidget()->underMouse() && !m_hover) break;
        QPainterPath path;
        path.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        const QColor c = m_hover ? theme().accent : QColor(255, 255, 255, 110);
        p.setPen(QPen(c, 1, Qt::DashLine));
        p.drawPath(path);
        p.save();
        p.translate(r.center() + QPointF(0, 10));
        p.rotate(-90);
        QFont f = font();
        f.setPixelSize(spineSize(m_label));
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(-r.height() / 2 + 20, -r.width() / 2, r.height() - 30, r.width()), Qt::AlignCenter, m_label);
        p.restore();
        f = font();
        f.setPixelSize(15);
        p.setFont(f);
        p.drawText(QRectF(r.left(), r.top() + 4, r.width(), 18), Qt::AlignCenter, "+");
        break;
    }
    case PartSeam: {
        if (!parentWidget()->underMouse() && !m_hover) break;
        const QColor c = m_hover ? theme().accent : QColor(255, 255, 255, 100);
        p.setPen(QPen(c, 1, Qt::DashLine));
        p.drawLine(QPointF(width() / 2.0, r.top() + 24), QPointF(width() / 2.0, r.bottom()));
        p.setPen(QPen(c, 1));
        p.drawEllipse(QPointF(width() / 2.0, r.top() + 9), 7, 7);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(QRectF(0, r.top() + 2, width(), 14), Qt::AlignCenter, "+");
        break;
    }
    case Cover:
    case Book:
        if (isScript(m_meta)) paintScript(p, r);
        else paintBook(p, r);
        break;
    }
}

void BookTile::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
    if (parentWidget()) parentWidget()->update(); // ghost pages wake with the shelf
}

void BookTile::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
    if (parentWidget()) parentWidget()->update();
}

void BookTile::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_pressPos = e->pos();
    m_pressed = true;
}

void BookTile::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_pressed || m_kind != Book) return;
    if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
    m_pressed = false;
    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    mime->setData(kBookMime, bookId().toUtf8());
    drag->setMimeData(mime);
    // a faded, smaller cover rides under the pointer, held by its corner
    QPixmap pm = grab().scaled(width() * 0.7, height() * 0.7, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap faded(pm.size());
    faded.fill(Qt::transparent);
    {
        QPainter fp(&faded);
        fp.setOpacity(0.6);
        fp.drawPixmap(0, 0, pm);
    }
    drag->setPixmap(faded);
    drag->setHotSpot(QPoint(12, 12));
    setVisible(false);
    drag->exec(Qt::MoveAction);
    if (!parent()) return;
    setVisible(true);
}

void BookTile::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_pressed || e->button() != Qt::LeftButton) return;
    m_pressed = false;
    if (!rect().contains(e->pos())) return;
    // the ↻ in the corner: a new cover
    if (m_kind == Book && !isScript(m_meta) && e->pos().x() > width() - 20 && e->pos().y() < 22) {
        emit menuRequested(this, QPoint(-1, -1));
        return;
    }
    emit activated(this);
}

void BookTile::contextMenuEvent(QContextMenuEvent *e) { emit menuRequested(this, e->globalPos()); }

void BookTile::dragEnterEvent(QDragEnterEvent *e)
{
    const QStringList paths = localPaths(e->mimeData());
    if (!paths.isEmpty() && (isImage(paths.first()) || isManuscript(paths.first()))) e->acceptProposedAction();
}

void BookTile::dropEvent(QDropEvent *e)
{
    const QStringList paths = localPaths(e->mimeData());
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    emit fileDropped(this, paths.first());
}

// ---------------------------------------------------------------------------
// a shelf

class ShelfRow : public QWidget {
public:
    ShelfRow(ShelfView *view, App *app, const QJsonObject &shelf, QWidget *parent)
        : QWidget(parent), m_view(view), m_app(app), m_shelf(shelf), m_id(shelf.value("id").toString())
    {
        setAcceptDrops(true);
        setMouseTracking(true);
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(14);
        auto *head = new QHBoxLayout;
        head->setSpacing(10);
        m_grip = new QLabel(QStringLiteral("⠿"), this);
        m_grip->setCursor(Qt::OpenHandCursor);
        m_grip->setStyleSheet("color: transparent; font-size: 13px;");
        m_grip->setToolTip(t("Drag to reorder shelves"));
        m_grip->installEventFilter(this);
        m_name = new QLineEdit(shelf.value("name").toString(), this);
        m_name->setFrame(false);
        m_name->setToolTip(t("Click to rename · right-click to export or delete"));
        m_name->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; border-bottom: 1px solid transparent; color: %1; font-size: 12px; letter-spacing: 2px; padding: 0; }"
                                             "QLineEdit:focus { color: %2; border-bottom-color: %3; }")
                                  .arg(theme().muted.name(), theme().text.name(), theme().accent.name()));
        QFont nf = m_name->font();
        nf.setCapitalization(QFont::AllUppercase);
        nf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        m_name->setFont(nf);
        m_name->setMinimumWidth(60);
        m_name->setFixedWidth(std::max(80, QFontMetrics(nf).horizontalAdvance(m_name->text()) + 24));
        m_name->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(m_name, &QLineEdit::textEdited, this, [this, nf] {
            m_name->setFixedWidth(std::max(80, QFontMetrics(nf).horizontalAdvance(m_name->text()) + 24));
        });
        QObject::connect(m_name, &QLineEdit::editingFinished, this, [this] { rename(); });
        QObject::connect(m_name, &QLineEdit::customContextMenuRequested, this, [this] {
            if (shelves::isBound(m_shelf)) m_view->boundMenu(m_id);
            else m_view->shelfMenu(m_id);
        });
        head->addWidget(m_grip);
        head->addWidget(m_name);
        if (shelves::isBound(shelf)) {
            auto *mark = new QLabel(QStringLiteral("· ") + t("one book").toUpper(), this);
            mark->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; letter-spacing: 2px;").arg(theme().accent.name()));
            head->addWidget(mark);
        }
        head->addStretch();
        col->addLayout(head);
        m_books = new QWidget(this);
        m_books->setMouseTracking(true);
        m_flow = new FlowLayout(m_books, shelves::isBound(shelf) ? 6 : 22, 22);
        m_flow->setContentsMargins(0, 0, 0, 14);
        col->addWidget(m_books);
        fill();
    }

    QString id() const { return m_id; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // the plank under the books
        const QRect g = m_books->geometry();
        const int y = g.bottom() - 9;
        p.fillRect(QRect(0, y, width(), 4), QColor("#2e2a24"));
        QLinearGradient sh(0, y + 4, 0, y + 16);
        sh.setColorAt(0, QColor(0, 0, 0, 120));
        sh.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(QRect(0, y + 4, width(), 12), sh);
        if (shelves::isBound(m_shelf)) {
            // the gold thread that stitches a bound book together
            QPen pen(theme().accent, 2, Qt::CustomDashLine);
            pen.setDashPattern({3.5, 2.5});
            p.setOpacity(0.55);
            p.setPen(pen);
            int right = 0;
            for (int i = 0; i < m_flow->count(); ++i)
                if (QWidget *w = m_flow->itemAt(i)->widget(); w && w->isVisible()) right = std::max(right, w->geometry().right());
            p.drawLine(QPoint(0, y - 4), QPoint(right, y - 4));
        }
        if (m_dropIndex >= 0) {
            QRect ref;
            if (m_dropIndex < m_flow->count()) ref = m_flow->itemAt(m_dropIndex)->geometry();
            else if (m_flow->count()) ref = m_flow->itemAt(m_flow->count() - 1)->geometry().translated(kTileW + 20, 0);
            const QPoint at = m_books->mapTo(this, ref.topLeft());
            p.fillRect(QRect(at.x() - 12, at.y() + kLift, 3, kTileH - 10), theme().accent);
        }
        if (m_shelfDrop) p.fillRect(QRect(20, 0, width() - 40, 3), theme().accent);
    }

    void enterEvent(QEnterEvent *) override
    {
        m_grip->setStyleSheet("color: #3a3a3a; font-size: 13px;");
        m_books->update();
        for (QWidget *w : m_books->findChildren<QWidget *>()) w->update();
    }
    void leaveEvent(QEvent *) override
    {
        m_grip->setStyleSheet("color: transparent; font-size: 13px;");
        for (QWidget *w : m_books->findChildren<QWidget *>()) w->update();
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_grip && e->type() == QEvent::MouseButtonPress) {
            auto *drag = new QDrag(this);
            auto *mime = new QMimeData;
            mime->setData(kShelfMime, m_id.toUtf8());
            drag->setMimeData(mime);
            drag->setPixmap(grab().scaledToWidth(std::min(width(), 400), Qt::SmoothTransformation));
            drag->exec(Qt::MoveAction);
            return true;
        }
        return false;
    }

    void dragEnterEvent(QDragEnterEvent *e) override
    {
        const QMimeData *m = e->mimeData();
        if (m->hasFormat(kBookMime) || m->hasFormat(kShelfMime) || (m->hasUrls() && !localPaths(m).isEmpty())) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasFormat(kBookMime)) {
            m_dropIndex = m_flow->indexAt(m_books->mapFrom(this, e->position().toPoint()));
            // never past the + tile
            m_dropIndex = std::min(m_dropIndex, tileCount());
        } else {
            m_dropIndex = -1;
        }
        m_shelfDrop = e->mimeData()->hasFormat(kShelfMime);
        update();
        e->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        m_dropIndex = -1;
        m_shelfDrop = false;
        update();
    }
    void dropEvent(QDropEvent *e) override
    {
        const QMimeData *m = e->mimeData();
        const int at = m_dropIndex;
        m_dropIndex = -1;
        m_shelfDrop = false;
        update();
        if (m->hasFormat(kShelfMime)) {
            const QString moving = QString::fromUtf8(m->data(kShelfMime));
            e->acceptProposedAction();
            QMetaObject::invokeMethod(m_view, [view = m_view, app = m_app, moving, target = m_id] {
                shelves::moveShelf(app->library(), moving, shelves::indexOfShelf(app->library(), target));
                app->writeLibrary();
                view->rebuild();
            }, Qt::QueuedConnection);
            return;
        }
        if (m->hasFormat(kBookMime)) {
            const QString bookId = QString::fromUtf8(m->data(kBookMime));
            e->acceptProposedAction();
            // the index among the shelf's own books, counting only real titles before the indicator
            QStringList ids = shelves::bookIds(m_shelf);
            int index = 0;
            for (int i = 0; i < std::max(0, at) && i < m_flow->count(); ++i) {
                auto *tile = qobject_cast<BookTile *>(m_flow->itemAt(i)->widget());
                if (tile && (tile->kind() == BookTile::Book || tile->kind() == BookTile::Page || tile->kind() == BookTile::Cover)
                    && tile->bookId() != bookId)
                    index++;
            }
            QMetaObject::invokeMethod(m_view, [view = m_view, app = m_app, bookId, shelf = m_id, index] {
                shelves::moveBook(app->library(), bookId, shelf, index, app->kindOf());
                app->writeLibrary();
                view->rebuild();
            }, Qt::QueuedConnection);
            return;
        }
        const QStringList paths = localPaths(m);
        QStringList manuscripts;
        for (const QString &p : paths)
            if (isManuscript(p)) manuscripts << p;
        if (manuscripts.isEmpty()) return;
        e->acceptProposedAction();
        QMetaObject::invokeMethod(m_view, [view = m_view, manuscripts, shelf = m_id] { view->importOnto(shelf, manuscripts); },
                                  Qt::QueuedConnection);
    }

private:
    int tileCount() const
    {
        int n = 0;
        for (int i = 0; i < m_flow->count(); ++i)
            if (auto *tile = qobject_cast<BookTile *>(m_flow->itemAt(i)->widget()); tile && tile->kind() == BookTile::NewBook) return n;
            else n++;
        return n;
    }

    BookTile *addTile(BookTile::Kind kind, const QJsonObject &meta)
    {
        auto *tile = new BookTile(m_app, kind, meta, m_id, m_books);
        QObject::connect(tile, &BookTile::activated, m_view, [this](BookTile *t) { m_view->tileActivated(t); });
        QObject::connect(tile, &BookTile::menuRequested, m_view, [this](BookTile *t, QPoint g) { m_view->tileMenu(t, g); });
        QObject::connect(tile, &BookTile::fileDropped, m_view, [this](BookTile *t, const QString &path) {
            // an image on a book is its cover; a manuscript imports onto its shelf
            if (isImage(path)) {
                auto meta = m_app->meta(t->bookId());
                if (meta && !isScript(*meta)) QMetaObject::invokeMethod(m_view, [v = m_view, id = t->bookId(), path] { v->setCoverFrom(id, path); }, Qt::QueuedConnection);
            } else if (isManuscript(path)) {
                QMetaObject::invokeMethod(m_view, [v = m_view, sid = m_id, path] { v->importOnto(sid, {path}); }, Qt::QueuedConnection);
            }
        });
        m_flow->addWidget(tile);
        return tile;
    }

    void fill()
    {
        const QStringList ids = shelves::bookIds(m_shelf);
        if (!shelves::isBound(m_shelf)) {
            for (const QString &id : ids) {
                auto meta = m_app->meta(id);
                if (!meta || kPageKinds.contains(meta->value("kind").toString())) continue;
                addTile(BookTile::Book, *meta);
            }
            auto *plus = addTile(BookTile::NewBook, {});
            plus->setToolTip(t("Start a new book"));
            return;
        }
        // a bound shelf: cover and front pages, the titles with seams for
        // parts, the + for a new title, the back pages
        QList<QJsonObject> items;
        for (const QString &id : ids)
            if (auto m = m_app->meta(id)) items << *m;
        int f = 0;
        while (f < items.size() && kPageLead.contains(items[f].value("kind").toString())) f++;
        int b = int(items.size());
        while (b > f && kPageTail.contains(items[b - 1].value("kind").toString())) b--;
        const QList<QJsonObject> front = items.mid(0, f), body = items.mid(f, b - f), back = items.mid(b);
        for (const auto &m : front)
            if (m.value("kind").toString() == "cover") {
                QJsonObject shown = m;
                shown.insert("title", m_shelf.value("name").toString());
                addTile(BookTile::Cover, shown);
            }
        // a lone title whose first chapter is its prologue isn't offered another
        QList<QJsonObject> titles;
        for (const auto &m : body)
            if (!kPageKinds.contains(m.value("kind").toString())) titles << m;
        auto offered = [&](const QString &kind) {
            if (!kPageWritten.contains(kind) || titles.size() != 1) return true;
            for (const QString &c : chapterOrder(titles.first()))
                if (chapterRole(c, titles.first()) == kind) return false;
            return true;
        };
        auto zone = [&](QList<QJsonObject> have, const QStringList &kinds) {
            for (const QString &k : kinds) {
                const auto it = std::find_if(have.begin(), have.end(), [&](const QJsonObject &m) { return m.value("kind").toString() == k; });
                if (it != have.end()) {
                    auto *tile = addTile(BookTile::Page, *it);
                    tile->setLabel(pageKindName(k));
                    tile->setPageKind(k);
                    tile->setToolTip(pageKindName(k));
                    have.erase(it);
                } else if (offered(k)) {
                    auto *ghost = addTile(BookTile::GhostPage, {});
                    ghost->setLabel(pageKindName(k));
                    ghost->setPageKind(k);
                    ghost->setToolTip(pageKindName(k));
                }
            }
        };
        QList<QJsonObject> frontPages;
        for (const auto &m : front)
            if (m.value("kind").toString() != "cover") frontPages << m;
        zone(frontPages, {"copyright", "dedication", "epigraph", "prologue"});
        int parts = 0;
        for (int i = 0; i < body.size(); ++i) {
            const QJsonObject &m = body[i];
            const QString k = m.value("kind").toString();
            if (k == "part") {
                auto *tile = addTile(BookTile::Page, m);
                tile->setLabel(partLabel(++parts));
                tile->setPageKind("part");
                continue;
            }
            if (kPageKinds.contains(k)) {
                auto *tile = addTile(BookTile::Page, m);
                tile->setLabel(pageKindName(k));
                tile->setPageKind(k);
                continue;
            }
            const bool afterPart = i > 0 && body[i - 1].value("kind").toString() == "part";
            if (!afterPart) {
                auto *seam = addTile(BookTile::PartSeam, {});
                seam->setBeforeId(m.value("id").toString());
                seam->setToolTip(t("Start a part here"));
            }
            addTile(BookTile::Book, m);
        }
        addTile(BookTile::NewBook, {})->setToolTip(t("Start a new book"));
        zone(back, {"epilogue", "acknowledgments", "about"});
    }

    void rename()
    {
        const QString before = m_shelf.value("name").toString();
        QString name = m_name->text().trimmed();
        if (name.isEmpty()) name = before;
        m_name->setText(name);
        if (name == before) return;
        QJsonObject s = shelves::shelf(m_app->library(), m_id);
        s.insert("name", name);
        shelves::putShelf(m_app->library(), s);
        m_shelf = s;
        m_app->writeLibrary();
        // a bound shelf's name is its book's title: the cover follows
        if (shelves::isBound(s))
            for (const QString &id : shelves::bookIds(s))
                if (auto m = m_app->meta(id); m && m->value("kind").toString() == "cover") {
                    QJsonObject c = *m;
                    c.insert("title", name);
                    m_app->writeMeta(id, c);
                }
    }

    ShelfView *m_view;
    App *m_app;
    QJsonObject m_shelf;
    QString m_id;
    QLabel *m_grip;
    QLineEdit *m_name;
    QWidget *m_books;
    FlowLayout *m_flow;
    int m_dropIndex = -1;
    bool m_shelfDrop = false;
};

// ---------------------------------------------------------------------------

ShelfView::ShelfView(App *app, QWidget *parent) : QScrollArea(parent), m_app(app)
{
    setObjectName("shelfView");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_content->setObjectName("room");
    auto *col = new QVBoxLayout(m_content);
    col->setContentsMargins(60, 44, 60, 80);
    col->setSpacing(0);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("NEOSEA", m_content);
    QFont tf = title->font();
    tf.setPixelSize(22);
    tf.setWeight(QFont::Light);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 10);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: %1;").arg(theme().accent.name()));
    header->addWidget(title);
    header->addStretch();
    m_author = new QPushButton(m_content);
    m_author->setToolTip(t("Click to change your author name"));
    m_author->setStyleSheet(QStringLiteral("QPushButton { color: %1; font-size: 13px; } QPushButton:hover { color: %2; }")
                                .arg(theme().muted.name(), theme().text.name()));
    connect(m_author, &QPushButton::clicked, this, &ShelfView::authorMenu);
    auto *importBtn = new QPushButton(QStringLiteral("⇩ ") + t("Import"), m_content);
    importBtn->setToolTip(t("Import .docx, .txt, or .md manuscripts"));
    importBtn->setObjectName("outline");
    connect(importBtn, &QPushButton::clicked, this, &ShelfView::importRequested);
    auto *addShelfBtn = new QPushButton(QStringLiteral("+ ") + t("Shelf"), m_content);
    addShelfBtn->setToolTip(t("Add a shelf"));
    addShelfBtn->setObjectName("outline");
    connect(addShelfBtn, &QPushButton::clicked, this, &ShelfView::addShelf);
    header->addWidget(m_author);
    header->addSpacing(8);
    header->addWidget(importBtn);
    header->addSpacing(8);
    header->addWidget(addShelfBtn);
    col->addLayout(header);
    col->addSpacing(36);
    m_shelves = new QVBoxLayout;
    m_shelves->setSpacing(44);
    col->addLayout(m_shelves);
    col->addStretch();
    setWidget(m_content);
    connect(app, &App::libraryChanged, this, &ShelfView::rebuild);
    rebuild();
}

void ShelfView::rebuild()
{
    const int keep = verticalScrollBar()->value();
    while (QLayoutItem *item = m_shelves->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_author->setText(m_app->authorName());
    for (const QJsonObject &s : m_app->myShelves()) m_shelves->addWidget(new ShelfRow(this, m_app, s, m_content));
    QMetaObject::invokeMethod(this, [this, keep] { verticalScrollBar()->setValue(keep); }, Qt::QueuedConnection);
}

void ShelfView::tileActivated(BookTile *tile)
{
    auto *row = dynamic_cast<ShelfRow *>(tile->parentWidget()->parentWidget());
    const QString sid = row ? row->id() : QString();
    switch (tile->kind()) {
    case BookTile::NewBook: {
        const QString id = m_app->createBook(sid);
        emit openBook(id);
        break;
    }
    case BookTile::Book: emit openBook(tile->bookId()); break;
    case BookTile::Cover: openTitlePage(sid, tile->bookId()); break;
    case BookTile::Page: {
        auto meta = m_app->meta(tile->bookId());
        if (meta) openPage(sid, tile->bookId(), tile->label().isEmpty() ? pageKindName(tile->pageKind()) : tile->label());
        break;
    }
    case BookTile::GhostPage: addPage(sid, tile->pageKind()); break;
    case BookTile::PartSeam: addPage(sid, "part", tile->beforeId()); break;
    }
}

void ShelfView::newBookMenu(const QString &shelfId, QPoint globalPos)
{
    QMenu m(this);
    QAction *book = m.addAction(t("New Book"));
    QAction *script = m.addAction(t("New Script"));
    QAction *picked = m.exec(globalPos);
    if (picked == book) emit openBook(m_app->createBook(shelfId));
    else if (picked == script) emit openBook(m_app->createBook(shelfId, true));
}

void ShelfView::setCoverFrom(const QString &bookId, const QString &path)
{
    const QString fname = m_app->lib().setCover(bookId, path);
    if (fname.isEmpty()) return;
    auto meta = m_app->meta(bookId);
    if (!meta) return;
    QJsonObject m = *meta;
    m.insert("coverImage", fname);
    m.insert("coverMode", "image");
    m_app->writeMeta(bookId, m);
    rebuild();
}

void ShelfView::tileMenu(BookTile *tile, QPoint globalPos)
{
    auto *row = dynamic_cast<ShelfRow *>(tile->parentWidget()->parentWidget());
    const QString sid = row ? row->id() : QString();
    if (tile->kind() == BookTile::NewBook) {
        if (!shelves::isBound(shelves::shelf(m_app->library(), sid))) newBookMenu(sid, globalPos);
        return;
    }
    if (tile->kind() == BookTile::GhostPage || tile->kind() == BookTile::PartSeam) return;
    const QString id = tile->bookId();
    auto metaOpt = m_app->meta(id);
    if (!metaOpt) return;
    QJsonObject meta = *metaOpt;
    const QString title = meta.value("title").toString();
    if (tile->kind() == BookTile::Page) {
        const QVariant c = optionModal(this, pageKindName(tile->pageKind()), {},
                                       {{t("Open"), {}, "open"},
                                        {t("Remove page"), t("Sends the page to your system trash, where you can recover it."), "remove", true}});
        if (c == "open") tileActivated(tile);
        else if (c == "remove" && m_app->lib().trashBook(id)) {
            shelves::removeBook(m_app->library(), id);
            m_app->writeLibrary();
            rebuild();
        }
        return;
    }
    // the ↻: a new cover straight away
    if (globalPos == QPoint(-1, -1)) {
        meta.insert("coverSeed", id + ":" + QString::number(meta.value("wordCount").toInt()) + ":" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36));
        if (coverMode(meta) == "image") meta.insert("coverMode", "abstract");
        m_app->writeMeta(id, meta);
        tile->update();
        rebuild();
        return;
    }
    QList<Choice> options;
    const QJsonObject home = shelves::shelf(m_app->library(), shelves::shelfOf(m_app->library(), id));
    if (shelves::isBound(home) && tile->kind() == BookTile::Book) {
        const QStringList ids = shelves::bookIds(home);
        const int at = int(ids.indexOf(id));
        const bool prevPart = at > 0 && m_app->kindOf()(ids[at - 1]) == "part";
        if (!prevPart) options << Choice{t("Start a part here"), t("A part page goes in before “{title}”.", {{"title", title}}), "part"};
    }
    const bool script = isScript(meta);
    if (!script) {
        options << Choice{meta.value("coverImage").toString().isEmpty() ? t("Set cover art…") : t("Replace cover art…"),
                          t("Pick an image (2:3 works best). Or just drag one from Finder onto the book."), "cover"};
        if (!meta.value("coverImage").toString().isEmpty())
            options << Choice{t("Remove cover art"), t("Deletes the image from the book folder. (To just hide it, use the ↻ on the book.)"), "uncover", true};
        options << Choice{t("Save cover as image…"), t("Full size, with your title and author."), "saveCover"};
        options << Choice{t("New cover"), {}, "refresh"};
    }
    options << Choice{t("Set word goal…"), t("Adds the subtle progress bar to the cover."), "goal"}
            << Choice{t("Remove from bookshelf"), t("Takes it off your shelves. The files stay safe in your NEO Library folder on disk."), "remove"}
            << Choice{t("Move to Trash"), t("Sends the book folder to your system trash, where you can recover it."), "trash", true};
    if (tile->kind() == BookTile::Cover) {
        options.clear();
        options << Choice{meta.value("coverImage").toString().isEmpty() ? t("Set cover art…") : t("Replace cover art…"),
                          t("Pick an image (2:3 works best). Or just drag one from Finder onto the book."), "cover"};
        if (!meta.value("coverImage").toString().isEmpty())
            options << Choice{t("Remove cover art"), t("Deletes the image from the book folder. (To just hide it, use the ↻ on the book.)"), "uncover", true};
        options << Choice{t("New cover"), {}, "refresh"}
                << Choice{t("Unbind"), t("A shelf of separate titles again. Its pages wait for the next binding."), "unbind"};
    }
    const QVariant choice = optionModal(this, QStringLiteral("“%1”").arg(title), {}, options);
    const QString c = choice.toString();
    if (c == "part") addPage(home.value("id").toString(), "part", id);
    else if (c == "cover") {
        const QString path = QFileDialog::getOpenFileName(this, t("Choose cover art"), QString(), t("Images") + " (*.png *.jpg *.jpeg *.webp)");
        if (!path.isEmpty()) setCoverFrom(id, path);
    } else if (c == "uncover") {
        m_app->lib().removeCover(id);
        meta.remove("coverImage");
        m_app->writeMeta(id, meta);
        rebuild();
    } else if (c == "saveCover") {
        const QString path = QFileDialog::getSaveFileName(this, t("Save cover as image…"),
                                                          QDir::home().filePath(safeName(title) + "-cover.jpg"), "JPEG (*.jpg)");
        if (path.isEmpty()) return;
        QImage art;
        if (coverMode(meta) == "image") art.load(m_app->lib().coverPath(id, meta.value("coverImage").toString()));
        if (covers::renderFull(meta, art).save(path, "JPG", 92)) emit m_app->toast(t("Saved: {file}", {{"file", QFileInfo(path).fileName()}}));
    } else if (c == "refresh") {
        tileMenu(tile, QPoint(-1, -1));
    } else if (c == "goal") {
        auto goal = askInput(this, t("Word count goal for “{title}”", {{"title", title}}), t("e.g. 80000 — blank removes the goal"),
                             meta.value("wordGoal").toInt() ? QString::number(meta.value("wordGoal").toInt()) : QString());
        if (!goal) return;
        meta.insert("wordGoal", goal->toInt());
        m_app->writeMeta(id, meta);
        rebuild();
    } else if (c == "remove") {
        shelves::removeBook(m_app->library(), id);
        m_app->writeLibrary();
        rebuild();
        emit m_app->toast(t("“{title}” removed from the shelves — its files are still in your NEO Library", {{"title", title}}));
    } else if (c == "trash") {
        if (!confirm(this, t("Move “{title}” to the Trash?", {{"title", title}}), t("The book folder goes to your system trash, so you can recover it."), t("Move to Trash"), true))
            return;
        if (m_app->lib().trashBook(id)) {
            shelves::removeBook(m_app->library(), id);
            m_app->writeLibrary();
            rebuild();
        } else {
            emit m_app->toast(t("NEO couldn’t move that folder to the Trash.") + " " + t("The book is untouched. Its folder is highlighted so you can deal with it yourself."), 8000);
        }
    } else if (c == "unbind") {
        shelves::unbind(m_app->library(), home.value("id").toString(), m_app->kindOf());
        m_app->writeLibrary();
        rebuild();
        emit m_app->toast(t("Unbound. Its pages wait for the next binding."));
    }
}

void ShelfView::shelfMenu(const QString &shelfId)
{
    const QJsonObject s = shelves::shelf(m_app->library(), shelfId);
    const int n = int(shelves::bookIds(s).size());
    const QVariant c = optionModal(this, t("Shelf “{name}”", {{"name", s.value("name").toString()}}), {},
                                   {{t("Bind into one book"), t("Its titles become one book, with a cover, front and back pages, and one table of contents."), "bind"},
                                    {t("Export shelf as anthology…"),
                                     n ? t("Collect its {n} works, in shelf order, into a single book with a table of contents.", {{"n", n}})
                                       : t("Collect the works, in shelf order, into a single book with a table of contents."),
                                     "anthology"},
                                    {t("Delete shelf"), t("Books move to another shelf. Nothing is deleted from disk."), "del", true}});
    if (c == "bind") bindShelf(shelfId);
    else if (c == "anthology") QMetaObject::invokeMethod(window(), "exportShelf", Q_ARG(QString, shelfId), Q_ARG(bool, false));
    else if (c == "del") {
        if (!shelves::deleteShelf(m_app->library(), shelfId, m_app->kindOf())) {
            emit m_app->toast(t("This is your only shelf — add another before deleting this one"));
            return;
        }
        m_app->writeLibrary();
        rebuild();
    }
}

void ShelfView::bindShelf(const QString &shelfId)
{
    QJsonObject s = shelves::shelf(m_app->library(), shelfId);
    QString coverId;
    for (const QString &id : shelves::bookIds(s))
        if (m_app->kindOf()(id) == "cover") coverId = id;
    // parked pages count too
    for (const auto &p : s.value("binding").toObject().value("parked").toArray())
        if (p.toObject().value("kind").toString() == "cover") coverId = p.toObject().value("id").toString();
    if (coverId.isEmpty()) coverId = m_app->createPageBook(shelfId, "cover");
    shelves::bind(m_app->library(), shelfId, coverId);
    m_app->writeLibrary();
    rebuild();
    emit m_app->toast(t("“{name}” is bound into one book", {{"name", s.value("name").toString()}}));
}

void ShelfView::boundMenu(const QString &shelfId)
{
    QJsonObject s = shelves::shelf(m_app->library(), shelfId);
    QJsonObject binding = s.value("binding").toObject();
    const bool through = binding.value("numbering").toString("through") != "restart";
    const QVariant c = optionModal(this, t("“{name}” · one book", {{"name", s.value("name").toString()}}), {},
                                   {{t("Export the book…"), t("EPUB, Word or PDF, with its cover, its pages and one table of contents."), "export"},
                                    {(through ? QStringLiteral("✓ ") : QString()) + t("Number chapters straight through"),
                                     through ? t("Each title picks up where the one before it left off.") : t("Each title starts again at Chapter 1."), "numbering"},
                                    {t("Unbind"), t("A shelf of separate titles again. Its pages wait for the next binding."), "unbind"}});
    if (c == "export") QMetaObject::invokeMethod(window(), "exportShelf", Q_ARG(QString, shelfId), Q_ARG(bool, true));
    else if (c == "numbering") {
        binding.insert("numbering", through ? "restart" : "through");
        s.insert("binding", binding);
        shelves::putShelf(m_app->library(), s);
        m_app->writeLibrary();
        emit m_app->toast(through ? t("Each title starts again at Chapter 1") : t("Chapters are numbered straight through"));
    } else if (c == "unbind") {
        shelves::unbind(m_app->library(), shelfId, m_app->kindOf());
        m_app->writeLibrary();
        rebuild();
        emit m_app->toast(t("Unbound. Its pages wait for the next binding."));
    }
}

void ShelfView::addPage(const QString &shelfId, const QString &kind, const QString &beforeId)
{
    const QString id = m_app->createPageBook(shelfId, kind);
    QJsonObject s = shelves::shelf(m_app->library(), shelfId);
    QStringList ids = shelves::bookIds(s);
    ids.insert(shelves::pageIndex(ids, kind, beforeId, m_app->kindOf()), id);
    s.insert("bookIds", QJsonArray::fromStringList(ids));
    shelves::putShelf(m_app->library(), s);
    m_app->writeLibrary();
    rebuild();
    QString label = pageKindName(kind);
    if (kind == "part") {
        int n = 0;
        for (const QString &b : ids) {
            if (m_app->kindOf()(b) == "part") n++;
            if (b == id) break;
        }
        label = partLabel(n);
    }
    openPage(shelfId, id, label);
}

void ShelfView::openPage(const QString &shelfId, const QString &bookId, const QString &label)
{
    auto meta = m_app->meta(bookId);
    if (!meta) return;
    const QString kind = meta->value("kind").toString();
    // a prologue or an epilogue is story: it opens in the editor
    if (kPageWritten.contains(kind)) {
        emit openBook(bookId);
        return;
    }
    QJsonObject live = *meta;
    QString chId = chapterOrder(live).value(0);
    if (chId.isEmpty()) {
        chId = newId("ch-");
        live.insert("chapterOrder", QJsonArray{chId});
        m_app->writeMeta(bookId, live);
    }
    // the page, open as it will print
    QDialog d(this, Qt::Dialog | Qt::FramelessWindowHint);
    d.setModal(true);
    d.setStyleSheet("QDialog { background: #161616; }");
    auto *col = new QVBoxLayout(&d);
    col->setContentsMargins(24, 18, 24, 24);
    auto *bar = new QHBoxLayout;
    auto *name = new QLabel(label.toUpper(), &d);
    name->setStyleSheet("color: #9a958c; font-size: 11px; letter-spacing: 2px;");
    auto *done = new QPushButton(t("Done"), &d);
    done->setObjectName("outline");
    bar->addWidget(name);
    bar->addStretch();
    bar->addWidget(done);
    col->addLayout(bar);
    auto *paper = new QTextEdit(&d);
    paper->setFixedSize(470, 600);
    paper->setStyleSheet(QStringLiteral("QTextEdit { background: %1; color: %2; border: none; padding: 60px 52px; }")
                             .arg(theme().paper.name(), theme().ink.name()));
    QFont f = paper->font();
    f.setFamily("Gelasio");
    f.setPixelSize(kind == "copyright" ? 11 : 15);
    f.setItalic(kind == "dedication" || kind == "epigraph" || kind == "part");
    paper->setFont(f);
    doc::loadHtml(*paper->document(), m_app->lib().readChapter(bookId, chId));
    if (kind == "dedication" || kind == "epigraph" || kind == "part") {
        QTextCursor c(paper->document());
        c.select(QTextCursor::Document);
        QTextBlockFormat bf;
        bf.setAlignment(Qt::AlignHCenter);
        c.mergeBlockFormat(bf);
    }
    paper->document()->setModified(false);
    col->addWidget(paper);
    connect(done, &QPushButton::clicked, &d, &QDialog::accept);
    paper->setFocus();
    paper->moveCursor(QTextCursor::End);
    d.exec();
    if (paper->document()->isModified()) {
        // stored as NEO's plain paragraphs; the centring was only for the eye
        QList<Para> paras = doc::paragraphs(*paper->document());
        for (Para &p : paras) p.setAlign({});
        m_app->lib().writeChapter(bookId, chId, serializeChapter(paras));
    }
}

void ShelfView::openTitlePage(const QString &shelfId, const QString &coverId)
{
    auto meta = m_app->meta(coverId);
    if (!meta) return;
    QJsonObject live = *meta;
    QJsonObject s = shelves::shelf(m_app->library(), shelfId);
    auto title = askInput(this, t("Title Page"), t("Title"), s.value("name").toString());
    if (!title) return;
    auto subtitle = askInput(this, t("Title Page"), t("Subtitle"), live.value("subtitle").toString());
    if (!subtitle) return;
    QString name = title->isEmpty() ? s.value("name").toString() : *title;
    s.insert("name", name);
    shelves::putShelf(m_app->library(), s);
    live.insert("title", name);
    live.insert("subtitle", *subtitle);
    m_app->writeMeta(coverId, live);
    m_app->writeLibrary();
    rebuild();
}

void ShelfView::authorMenu()
{
    QJsonObject &lib = m_app->library();
    const QJsonObject cur = shelves::currentAuthor(lib, m_app->anonymous());
    QList<Choice> opts;
    for (const auto &a : lib.value("authors").toArray()) {
        const QJsonObject o = a.toObject();
        if (o.value("id") != cur.value("id"))
            opts << Choice{t("Write as {name}", {{"name", o.value("name").toString()}}), t("Switch to this name’s shelves"), "sw:" + o.value("id").toString()};
    }
    opts << Choice{t("Rename {name}", {{"name", cur.value("name").toString()}}), {}, "rename"};
    opts << Choice{t("Add a pen name…"), t("A separate set of shelves under another name"), "add"};
    if (lib.value("authors").toArray().size() > 1)
        opts << Choice{t("Remove {name}", {{"name", cur.value("name").toString()}}),
                       t("These shelves and books move to your other name. Nothing is deleted from disk."), "del", true};
    const QString pick = optionModal(this, t("Writing as {name}", {{"name", cur.value("name").toString()}}), {}, opts).toString();
    if (pick.isEmpty()) return;
    if (pick.startsWith("sw:")) lib.insert("currentAuthorId", pick.mid(3));
    else if (pick == "rename") {
        auto name = askInput(this, t("Author name"), t("Shown on your title pages"), cur.value("name").toString());
        if (!name || name->isEmpty()) return;
        shelves::renameAuthor(lib, cur.value("id").toString(), *name);
    } else if (pick == "add") {
        auto name = askInput(this, t("New pen name"), t("Shown on that name’s title pages"));
        if (!name || name->isEmpty()) return;
        shelves::addAuthor(lib, *name, t("Works in Progress"));
    } else if (pick == "del") {
        shelves::removeAuthor(lib, cur.value("id").toString());
    }
    m_app->writeLibrary();
    rebuild();
}

void ShelfView::addShelf()
{
    shelves::addShelf(m_app->library(), t("New Shelf"), m_app->authorId());
    m_app->writeLibrary();
    rebuild();
    QMetaObject::invokeMethod(this, [this] {
        ensureVisible(0, m_content->height(), 0, 0);
        const auto edits = m_content->findChildren<QLineEdit *>();
        if (!edits.isEmpty()) {
            edits.last()->setFocus();
            edits.last()->selectAll();
        }
    }, Qt::QueuedConnection);
}

void ShelfView::importOnto(const QString &shelfId, const QStringList &paths)
{
    emit m_app->toast(t("Importing…"));
    const App::ImportOutcome r = m_app->importFiles(paths, shelfId);
    for (const QString &e : r.errors) emit m_app->toast(t("Couldn’t import {name}: {error}", {{"name", e.section(": ", 0, 0)}, {"error", e.section(": ", 1)}}), 6000);
    const QString shelfName = shelves::shelf(m_app->library(), shelfId).value("name").toString();
    if (r.books)
        emit m_app->toast(t("{n} books imported onto “{shelf}” — chapters and scene breaks detected", {{"n", r.books}, {"shelf", shelfName}}), 6000);
    else if (r.scripts)
        emit m_app->toast(t("{n} scripts imported onto “{shelf}”", {{"n", r.scripts}, {"shelf", shelfName}}), 6000);
}

} // namespace neosea

#include "moc_shelfview.cpp"
