#include "app/editorview.h"

#include "app/app.h"
#include "app/auxpage.h"
#include "app/outlineboard.h"
#include "app/dialogs.h"
#include "app/navpane.h"
#include "app/sidepane.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/counters.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/pagelayout.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBlock>
#include <QVBoxLayout>

namespace neosea {

namespace {

// the page: a book page's width, a little wider in a wide window
int pageWidth(int window, double zoom)
{
    return int(std::clamp(window * 0.44, 680.0, 760.0) * zoom);
}

double pageZoom(App *app) { return std::clamp(app->library().value("pageZoom").toDouble(1), 0.75, 3.0); }

int editorPx(App *app)
{
    return std::clamp(app->library().value("editorFontSize").toInt(17), 14, 22);
}

void paintPaper(QWidget *w, QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(w->rect()).adjusted(18, 4, -18, -30);
    // the page's shadow on the room
    for (int i = 10; i > 0; --i) {
        QColor c(0, 0, 0, int(10 + 3 * (10 - i)) / 2);
        p.fillRect(r.adjusted(-i, -i + 4, i, i + 4), c);
    }
    p.fillRect(r, theme().paper);
    if (theme().name == "night") {
        p.setPen(QColor("#2e2d2b"));
        p.drawRect(r);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// the title page: its own sheet

class TitleSheet : public QWidget {
public:
    QLineEdit *title, *subtitle, *author;

    explicit TitleSheet(QWidget *parent) : QWidget(parent)
    {
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(90, 70, 90, 120);
        col->addStretch();
        title = field(t("Untitled"), 34, true);
        subtitle = field(t("Subtitle (optional)"), 17, false, true);
        author = field(t("Author"), 14);
        QFont af = author->font();
        af.setCapitalization(QFont::AllUppercase);
        af.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        author->setFont(af);
        col->addWidget(title);
        col->addSpacing(14);
        col->addWidget(subtitle);
        col->addSpacing(46);
        col->addWidget(author);
        col->addStretch();
    }

    void setLook(const QString &family, double zoom)
    {
        auto fit = [&](QLineEdit *e, double px, bool bold, bool italic, QColor color) {
            QFont f = e->font();
            f.setFamily(family);
            f.setPixelSize(int(px * zoom));
            f.setBold(bold);
            f.setItalic(italic);
            e->setFont(f);
            QPalette pal = e->palette();
            pal.setColor(QPalette::Text, color);
            pal.setColor(QPalette::PlaceholderText, theme().name == "night" ? QColor("#5f5b52") : QColor("#b9b4a8"));
            e->setPalette(pal);
        };
        fit(title, 34, true, false, theme().ink);
        fit(subtitle, 17, false, true, theme().name == "night" ? QColor("#a09a8c") : QColor("#555555"));
        fit(author, 14, false, false, theme().name == "night" ? QColor("#918b7d") : QColor("#444444"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintPaper(this, p);
    }

private:
    QLineEdit *field(const QString &placeholder, int px, bool bold = false, bool italic = false)
    {
        auto *e = new QLineEdit(this);
        e->setFrame(false);
        e->setAlignment(Qt::AlignCenter);
        e->setPlaceholderText(placeholder);
        e->setStyleSheet("QLineEdit { background: transparent; border: none; }");
        QFont f = e->font();
        f.setPixelSize(px);
        f.setBold(bold);
        f.setItalic(italic);
        e->setFont(f);
        return e;
    }
};

// ---------------------------------------------------------------------------
// a chapter's sheet: its heading, then its text

class ChapterSheet : public QWidget {
public:
    QString chId;
    QLabel *number;
    QLabel *sep;
    QLineEdit *titleEdit;
    QWidget *head;
    ChapterEdit *edit;
    bool solo = false;

    ChapterSheet(EditorView *view, const QString &id, QWidget *parent) : QWidget(parent), chId(id)
    {
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(90, 74, 90, 120);
        col->setSpacing(0);
        head = new QWidget(this);
        auto *row = new QHBoxLayout(head);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        row->addStretch();
        number = new QLabel(head);
        sep = new QLabel(QStringLiteral("—"), head);
        titleEdit = new QLineEdit(head);
        titleEdit->setFrame(false);
        titleEdit->setPlaceholderText(t("add a title"));
        titleEdit->setStyleSheet("QLineEdit { background: transparent; border: none; }");
        titleEdit->setPlaceholderText(QString());
        head->setAttribute(Qt::WA_Hover);
        head->installEventFilter(this);
        QObject::connect(titleEdit, &QLineEdit::textEdited, this, [this] { fitTitle(); });
        row->addWidget(number);
        row->addWidget(sep);
        row->addWidget(titleEdit);
        row->addStretch();
        head->setContextMenuPolicy(Qt::CustomContextMenu);
        number->setContextMenuPolicy(Qt::NoContextMenu);
        QObject::connect(head, &QWidget::customContextMenuRequested, view, [view, this](QPoint p) {
            view->chapterMenu(chId, head->mapToGlobal(p));
        });
        col->addWidget(head);
        col->addSpacing(44);
        edit = new ChapterEdit(view, id, this);
        col->addWidget(edit);
        col->addStretch();
        QObject::connect(edit, &ChapterEdit::heightChanged, this, [this] { updateGeometry(); });
    }

    void setHeading(const QString &name, const QString &title, bool story, bool unnumbered, const QString &family, double zoom, bool faint)
    {
        number->setText(name.toUpper());
        number->setVisible(!unnumbered);
        titleEdit->setVisible(story);
        // an unnumbered chapter goes by its title: the prompt stays in view
        if (unnumbered) titleEdit->setPlaceholderText(t("add a title"));
        if (titleEdit->text() != title && !titleEdit->hasFocus()) titleEdit->setText(title);
        const QColor c = faint ? (theme().name == "night" ? QColor("#5f5b52") : QColor("#c4beb0")) : theme().chapterHead;
        // the style first: a style sheet set after the font takes the font away
        const QString css = QStringLiteral("color: %1; background: transparent;").arg(c.name());
        number->setStyleSheet(css);
        sep->setStyleSheet(css);
        QFont f;
        f.setFamily(family);
        f.setPixelSize(int((faint ? 11 : 15) * zoom));
        f.setLetterSpacing(QFont::AbsoluteSpacing, faint ? 3 : 4);
        f.setCapitalization(QFont::AllUppercase);
        number->setFont(f);
        sep->setFont(f);
        titleEdit->setFont(f);
        QPalette pal = titleEdit->palette();
        pal.setColor(QPalette::Text, c);
        pal.setColor(QPalette::PlaceholderText, theme().name == "night" ? QColor("#5f5b52") : QColor("#c4beb0"));
        titleEdit->setPalette(pal);
        fitTitle();
        head->setVisible(!solo);
    }

    // the title takes the room its words need; "add a title" waits for the pointer
    void fitTitle()
    {
        const QString shown = titleEdit->text().isEmpty() ? titleEdit->placeholderText() : titleEdit->text();
        const int w = QFontMetrics(titleEdit->font()).horizontalAdvance(shown.toUpper()) + 40;
        titleEdit->setFixedWidth(std::max(24, w));
        sep->setVisible(!titleEdit->isHidden() && !number->isHidden() && (!titleEdit->text().isEmpty() || hovered));
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == head && (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave)) {
            hovered = e->type() == QEvent::HoverEnter;
            titleEdit->setPlaceholderText(hovered || !number->isVisible() ? t("add a title") : QString());
            fitTitle();
        }
        return QWidget::eventFilter(o, e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintPaper(this, p);
    }

private:
    bool hovered = false;
};

// ---------------------------------------------------------------------------

EditorView::EditorView(App *app, QWidget *parent) : QWidget(parent), m_app(app)
{
    setFocusPolicy(Qt::NoFocus);
    m_stack = new QStackedWidget(this);
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_room = new QWidget;
    m_room->setObjectName("room");
    auto *outer = new QHBoxLayout(m_room);
    outer->setContentsMargins(0, 40, 0, 120);
    m_column = new QWidget(m_room);
    m_sheets = new QVBoxLayout(m_column);
    m_sheets->setContentsMargins(0, 0, 0, 0);
    m_sheets->setSpacing(14);
    outer->addStretch();
    outer->addWidget(m_column);
    outer->addStretch();
    m_scroll->setWidget(m_room);
    m_stack->addWidget(m_scroll);
    m_aux = new AuxPage(this);
    m_stack->addWidget(m_aux);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { m_scrollTimer.start(120); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { applyPendingScroll(); });
    m_scrollTimer.setSingleShot(true);
    connect(&m_scrollTimer, &QTimer::timeout, this, &EditorView::trackScroll);

    // the bottom bar
    m_bar = new QWidget(this);
    m_bar->setObjectName("bottombar");
    auto *bar = new QHBoxLayout(m_bar);
    bar->setContentsMargins(14, 0, 14, 0);
    auto *back = new QPushButton(QStringLiteral("⇤ ") + t("Shelf"), m_bar);
    back->setToolTip(t("Back to your bookshelf"));
    connect(back, &QPushButton::clicked, this, &EditorView::backToShelf);
    bar->addWidget(back);
    bar->addStretch();
    for (const auto &[key, label] : QList<QPair<QString, QString>>{{"manuscript", t("Manuscript")}, {"notes", t("Notes")}, {"outline", t("Outline")}, {"darlings", t("Darlings")}}) {
        auto *b = new QPushButton(label, m_bar);
        b->setCheckable(true);
        b->setProperty("tab", key);
        connect(b, &QPushButton::clicked, this, [this, key] { showTab(key); });
        if (key == "darlings") b->setToolTip(t("Drag any selection here. It's saved, not gone."));
        m_tabs.insert(key, b);
        bar->addWidget(b);
    }
    bar->addStretch();
    m_goalCounter = new QLabel(m_bar);
    m_goalCounter->setToolTip(t("Today's words — click for goals, sprints, and your progress chart"));
    m_posCounter = new QLabel(m_bar);
    m_wordCounter = new QLabel(m_bar);
    m_wordCounter->setToolTip(t("Click to cycle book / chapter word count"));
    for (QLabel *l : {m_goalCounter, m_posCounter, m_wordCounter}) {
        l->setCursor(Qt::PointingHandCursor);
        l->installEventFilter(this);
        bar->addWidget(l);
        bar->addSpacing(14);
    }
    m_bar->setStyleSheet(QStringLiteral("#bottombar { background: transparent; } #bottombar QPushButton { color: %1; padding: 4px 10px; } "
                                        "#bottombar QPushButton:hover, #bottombar QPushButton:checked { color: %2; } "
                                        "#bottombar QLabel { color: %1; font-size: 12px; }")
                             .arg(theme().muted.name(), theme().accent.name()));

    m_nav = new NavPane(this);
    m_side = new SidePane(this);

    m_counterTimer.setSingleShot(true);
    connect(&m_counterTimer, &QTimer::timeout, this, &EditorView::updateCounters);
    // every twenty seconds, whatever wasn't saved yet goes to disk
    connect(&m_flushTimer, &QTimer::timeout, this, [this] { flush(); });
    // and a look at the disk every half minute, for the other device
    connect(&m_refreshTimer, &QTimer::timeout, this, &EditorView::refreshFromDisk);
    setMouseTracking(true);
    qApp->installEventFilter(this);
}

void EditorView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutOverlays();
    applyLook();
}

void EditorView::layoutOverlays()
{
    const int barH = 40;
    m_stack->setGeometry(0, 0, width(), height() - barH);
    m_bar->setGeometry(0, height() - barH, width(), barH);
    m_nav->setGeometry(m_nav->isOpen() ? 0 : -248, 0, 248, height() - barH);
    m_side->setGeometry(m_side->isOpen() ? width() - 250 : width(), 0, 250, height() - barH);
    m_nav->raise();
    m_side->raise();
}

bool EditorView::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseMove && isVisible()) {
        // the panes wake from the margins, never over the page
        const QPoint p = mapFromGlobal(QCursor::pos());
        if (rect().contains(p)) {
            const int margin = std::clamp((width() - m_column->width()) / 2 - 24, 18, 96);
            if (p.x() < margin && p.y() < height() - 40) m_nav->open();
            else if (!m_nav->pinned() && p.x() > 248 && !m_nav->busy()) m_nav->close();
            if (p.x() > width() - margin && p.y() < height() - 40) m_side->open();
            else if (!m_side->pinned() && p.x() < width() - 250 && !m_side->busy()) m_side->close();
        }
    }
    if (e->type() == QEvent::MouseButtonPress && (o == m_goalCounter || o == m_posCounter || o == m_wordCounter)) {
        if (o == m_wordCounter) {
            m_wordMode = m_wordMode == "book" ? "chapter" : "book";
            updateCounters();
        } else if (o == m_posCounter) {
            QJsonObject &lib = m_app->library();
            lib.insert("posMode", lib.value("posMode").toString() == "page" ? "chapter" : "page");
            m_app->writeLibrary();
            updateCounters();
        } else {
            QMetaObject::invokeMethod(window(), "openGoals");
        }
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void EditorView::keyPressEvent(QKeyEvent *e)
{
    QWidget::keyPressEvent(e);
}

void EditorView::applyLook()
{
    BookSession *s = m_app->session();
    const double zoom = pageZoom(m_app);
    const int w = pageWidth(window()->width(), zoom) + 36; // the shadow's room
    m_column->setFixedWidth(std::min(w, int(width() * 0.94)));
    const QJsonObject fonts = m_app->library().value("fonts").toObject();
    const QString family = bodyFontFamily(fonts.value("body").toString());
    const QString capStyle = fonts.value("dropcap").toString("literary");
    const bool script = s && isScript(s->book());
    const double px = (script ? 16 : editorPx(m_app)) * zoom;
    const int minH = int(m_scroll->viewport()->height() * 0.88);
    if (m_title) {
        m_title->setLook(family, zoom);
        m_title->setMinimumHeight(minH);
    }
    // the text's width is known before the sheets are laid out, so each
    // chapter knows its height straight away (and the page can scroll to it)
    const int textWidth = m_column->width() - 36 - 180;
    for (ChapterSheet *sheet : m_chapters) {
        sheet->setMinimumHeight(minH);
        if (textWidth > 100 && sheet->edit->document()->textWidth() != textWidth) sheet->edit->document()->setTextWidth(textWidth);
        QFont f;
        f.setFamilies(script ? QStringList{kScriptFamily} : QStringList{family, "Merriweather Light 18pt", "Gelasio"});
        f.setPixelSize(int(std::lround(px)));
        sheet->edit->document()->setDefaultFont(f);
        PageStyle st;
        st.fontPx = px;
        st.lineHeight = script ? 1.0 : 1.75;
        const QString kind = s ? chapterKind(sheet->chId, s->book()) : "chapter";
        st.story = kStoryKinds.contains(kind);
        st.pageKind = kind;
        st.script = script;
        st.dropCapFamily = (st.story && !script) ? dropCapFamily(capStyle) : QString();
        st.ink = theme().ink;
        st.muted = theme().sceneBreak;
        st.ghost = theme().ghost;
        st.caret = theme().ink;
        sheet->edit->pageLayout()->setStyle(st);
        QPalette pal = sheet->edit->palette();
        pal.setColor(QPalette::Text, theme().ink);
        pal.setColor(QPalette::Highlight, QColor(201, 168, 106, 90));
        pal.setColor(QPalette::HighlightedText, theme().ink);
        sheet->edit->setPalette(pal);
        if (s) {
            const QJsonObject &b = s->book();
            const bool faint = QStringList{"copyright", "dedication", "epigraph"}.contains(kind);
            sheet->solo = soloStory(b) == sheet->chId;
            sheet->setHeading(chapterName(sheet->chId, b), chapterTitle(sheet->chId, b), st.story, kind == "unnumbered", family, zoom, faint);
        }
    }
}

void EditorView::openSession()
{
    BookSession *s = m_app->session();
    if (!s) return;
    if (settleChapterKinds(s->book())) s->saveMeta();
    m_current.clear();
    m_wordCache.clear();
    rebuildChapters();
    m_nav->rebuild();
    m_side->rebuild();
    m_tabs.value("notes")->setText([&] {
        const QString n = s->book().value("tabNames").toObject().value("notes").toString("Notes");
        return n == "Notes" ? t("Notes") : n;
    }());
    m_tabs.value("outline")->setText([&] {
        const QString n = s->book().value("tabNames").toObject().value("outline").toString("Outline");
        return n == "Outline" ? t("Outline") : n;
    }());
    const bool isNew = s->order().isEmpty();
    const bool plotter = m_app->library().value("writingStyle").toString() == "plotter";
    showTab(isNew && plotter && !isScript(s->book()) ? "outline" : "manuscript");
    if (isNew || (isScript(s->book()) && isUntitled(s->book().value("title").toString()))) {
        m_title->title->setFocus();
    } else {
        const QJsonObject pos = s->book().value("lastPosition").toObject();
        if (s->order().contains(pos.value("chapterId").toString())) {
            QTimer::singleShot(0, this, [this, pos] {
                restoreCaret(QJsonObject{{"chId", pos.value("chapterId")}, {"pIdx", pos.value("pIdx")}, {"off", pos.value("off")}});
                if (pos.contains("scroll") && !pos.contains("pIdx")) m_scroll->verticalScrollBar()->setValue(pos.value("scroll").toInt());
                else if (ChapterEdit *e = editorFor(m_current)) {
                    const QRect r = e->cursorRect();
                    const QPoint at = e->viewport()->mapTo(m_room, r.topLeft());
                    m_scroll->verticalScrollBar()->setValue(at.y() - m_scroll->viewport()->height() / 3);
                }
            });
        }
    }
    updateCounters();
    m_flushTimer.start(20000);
    m_refreshTimer.start(30000);
}

void EditorView::closeSession()
{
    flush(true);
    m_flushTimer.stop();
    m_refreshTimer.stop();
    for (QTimer *t : m_saveTimers) t->deleteLater();
    m_saveTimers.clear();
}

void EditorView::rebuildChapters()
{
    BookSession *s = m_app->session();
    m_rebuilding = true;
    m_wordCache.clear(); // chapters may have traded writing
    const int keep = m_scroll->verticalScrollBar()->value();
    while (QLayoutItem *item = m_sheets->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_chapters.clear();
    m_title = new TitleSheet(m_column);
    m_sheets->addWidget(m_title);
    if (s) {
        const QJsonObject &b = s->book();
        m_title->title->setText(isUntitled(b.value("title").toString()) ? QString() : b.value("title").toString());
        m_title->subtitle->setText(b.value("subtitle").toString());
        m_title->author->setText(b.value("author").toString().isEmpty() ? t("Anonymous") : b.value("author").toString());
        connect(m_title->title, &QLineEdit::textEdited, this, [this](const QString &v) {
            if (!m_app->session()) return;
            m_app->session()->book().insert("title", v.trimmed().isEmpty() ? t("Untitled") : v.trimmed());
            m_app->session()->saveMeta();
        });
        connect(m_title->subtitle, &QLineEdit::textEdited, this, [this](const QString &v) {
            if (!m_app->session()) return;
            m_app->session()->book().insert("subtitle", v.trimmed());
            m_app->session()->saveMeta();
        });
        connect(m_title->author, &QLineEdit::textEdited, this, [this](const QString &v) {
            if (!m_app->session()) return;
            m_app->session()->book().insert("author", v.trimmed());
            m_app->session()->saveMeta();
        });
        // Enter on the title page drops you into the story
        for (QLineEdit *f : {m_title->title, m_title->subtitle}) {
            connect(f, &QLineEdit::returnPressed, this, [this] {
                BookSession *s = m_app->session();
                if (!s) return;
                QString first;
                for (const QString &c : s->order())
                    if (isStory(c, s->book())) { first = c; break; }
                if (first.isEmpty()) {
                    // into a first chapter, before the pages at the back
                    qsizetype at = s->order().size();
                    while (at > 0 && kBackKinds.contains(chapterKind(s->order()[at - 1], s->book()))) at--;
                    first = s->createChapterAt(at);
                    rebuildChapters();
                }
                focusChapter(first);
            });
        }
        for (const QString &chId : s->order()) {
            auto *sheet = new ChapterSheet(this, chId, m_column);
            QString html = s->chapterHtml(chId);
            if (html.trimmed().isEmpty()) html = "<p><br></p>";
            sheet->edit->loadHtml(html);
            sheet->edit->setVisible(chapterKind(chId, b) != "contents");
            connect(sheet->titleEdit, &QLineEdit::editingFinished, this, [this, sheet] {
                BookSession *s = m_app->session();
                if (!s) return;
                QJsonObject titles = s->book().value("chapterTitles").toObject();
                const QString v = sheet->titleEdit->text().trimmed();
                if (titles.value(sheet->chId).toString() == v) return;
                titles.insert(sheet->chId, v);
                s->book().insert("chapterTitles", titles);
                s->saveMeta();
                m_nav->rebuild();
                applyLook();
            });
            connect(sheet->titleEdit, &QLineEdit::returnPressed, sheet->edit, [sheet] { sheet->edit->focusStart(); });
            m_chapters << sheet;
            m_sheets->addWidget(sheet);
        }
    }
    m_rebuilding = false;
    applyLook();
    QTimer::singleShot(0, this, [this, keep] { m_scroll->verticalScrollBar()->setValue(keep); });
    m_nav->rebuild();
}

ChapterEdit *EditorView::editorFor(const QString &chId) const
{
    for (ChapterSheet *s : m_chapters)
        if (s->chId == chId) return s->edit;
    return nullptr;
}

void EditorView::focusChapter(const QString &chId, bool atEnd)
{
    if (m_tab != "manuscript") showTab("manuscript");
    ChapterEdit *e = editorFor(chId);
    if (!e) return;
    m_current = chId;
    if (atEnd) e->focusEnd();
    else e->focusStart();
    // after the sheets have their places (a rebuild lays them out on the next turn)
    QTimer::singleShot(0, this, [this, chId, atEnd] {
        ChapterEdit *e = editorFor(chId);
        if (!e) return;
        m_sheets->activate();
        m_room->layout()->activate();
        if (atEnd) {
            const QRect r = e->cursorRect();
            const QPoint at = e->viewport()->mapTo(m_room, r.center());
            m_scroll->ensureVisible(at.x(), at.y(), 0, m_scroll->viewport()->height() / 4);
        } else {
            scrollToSheet(chId);
        }
    });
    m_nav->highlight(chId);
    updateCounters();
}

void EditorView::gotoChapter(int step)
{
    BookSession *s = m_app->session();
    if (!s || s->order().isEmpty()) return;
    const QStringList order = s->order();
    int at = int(order.indexOf(m_current));
    if (at < 0) at = step > 0 ? -1 : int(order.size());
    const int to = std::clamp(at + step, 0, int(order.size()) - 1);
    if (to == at) return;
    focusChapter(order[to]);
}

void EditorView::scrollToSheet(const QString &chId)
{
    m_pendingSheet = chId;
    if (applyPendingScroll()) return;
    // the page's height catches up a turn later: the range tells us when
    QTimer::singleShot(600, this, [this] { m_pendingSheet.clear(); });
}

bool EditorView::applyPendingScroll()
{
    if (m_pendingSheet.isEmpty()) return false;
    for (ChapterSheet *s : m_chapters) {
        if (s->chId != m_pendingSheet) continue;
        const int want = s->mapTo(m_room, QPoint(0, 0)).y() + 40;
        QScrollBar *bar = m_scroll->verticalScrollBar();
        bar->setValue(want);
        if (bar->value() == std::min(want, bar->maximum()) && bar->maximum() >= want) {
            m_pendingSheet.clear();
            return true;
        }
        return false;
    }
    return false;
}

void EditorView::showTab(const QString &tab)
{
    if (m_tab == "manuscript" && tab != "manuscript") syncAll();
    m_tab = tab;
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) it.value()->setChecked(it.key() == tab);
    if (tab == "manuscript") {
        m_stack->setCurrentWidget(m_scroll);
        m_aux->leave();
    } else {
        m_aux->show(tab);
        m_stack->setCurrentWidget(m_aux);
    }
    m_side->setOutlineMode(tab == "outline");
}

void EditorView::syncAll()
{
    BookSession *s = m_app->session();
    if (!s) return;
    for (ChapterSheet *sheet : m_chapters)
        if (sheet->edit->document()->isModified()) {
            s->setChapterHtml(sheet->chId, sheet->edit->currentHtml());
            sheet->edit->document()->setModified(false);
        }
}

void EditorView::flush(bool exact)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    // where you were, for the next session and for the other device
    const QJsonObject caret = captureCaret();
    QJsonObject prev = s->book().value("lastPosition").toObject();
    const int scroll = m_scroll->verticalScrollBar()->value();
    QJsonObject spot;
    if (!caret.isEmpty()) spot = QJsonObject{{"chapterId", caret.value("chId")}, {"pIdx", caret.value("pIdx")}, {"off", caret.value("off")}};
    else spot = QJsonObject{{"chapterId", m_current}};
    const bool newSpot = spot.value("chapterId") != prev.value("chapterId") || spot.value("pIdx") != prev.value("pIdx");
    const bool newLetter = newSpot || spot.value("off") != prev.value("off");
    const bool moved = newSpot || (exact && newLetter) || std::abs(prev.value("scroll").toInt() - scroll) > 40;
    if (moved && !m_current.isEmpty()) {
        spot.insert("scroll", scroll);
        spot.insert("at", newLetter ? double(QDateTime::currentMSecsSinceEpoch()) : prev.value("at").toDouble(double(QDateTime::currentMSecsSinceEpoch())));
        s->book().insert("lastPosition", spot);
    }
    for (const QString &chId : s->order())
        if (s->chapterDirty(chId)) s->persistChapter(chId);
    m_aux->flush();
    if (moved || s->metaDirty()) s->saveMeta();
}

QJsonObject EditorView::captureCaret() const
{
    if (m_tab != "manuscript") return {};
    for (ChapterSheet *sheet : m_chapters) {
        if (!sheet->edit->hasFocus() && sheet->chId != m_current) continue;
        const auto [block, off] = sheet->edit->caretAddress();
        return QJsonObject{{"chId", sheet->chId}, {"pIdx", block}, {"off", off}, {"scroll", m_scroll->verticalScrollBar()->value()}};
    }
    return {};
}

void EditorView::restoreCaret(const QJsonObject &caret)
{
    ChapterEdit *e = editorFor(caret.value("chId").toString());
    if (!e) return;
    e->setFocus();
    e->placeCaret(caret.value("pIdx").toInt(), caret.value("off").toInt());
    m_current = caret.value("chId").toString();
    if (caret.contains("scroll")) m_scroll->verticalScrollBar()->setValue(caret.value("scroll").toInt());
    m_nav->highlight(m_current);
}

void EditorView::saveChapterSoon(const QString &chId)
{
    QTimer *t = m_saveTimers.value(chId);
    if (!t) {
        t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this, chId] {
            BookSession *s = m_app->session();
            // the chapter was deleted or merged away while this save waited
            if (!s || !s->order().contains(chId)) return;
            if (ChapterEdit *e = editorFor(chId); e && e->document()->isModified()) {
                s->setChapterHtml(chId, e->currentHtml());
                e->document()->setModified(false);
            }
            if (s->chapterDirty(chId)) s->persistChapter(chId);
        });
        m_saveTimers.insert(chId, t);
    }
    t->start(800);
}

void EditorView::chapterEdited(ChapterEdit *e)
{
    m_wordCache.remove(e->chId());
    m_current = e->chId();
    saveChapterSoon(e->chId());
    scheduleCounters();
    m_nav->scheduleRefresh();
}

void EditorView::chapterFocused(ChapterEdit *e)
{
    m_current = e->chId();
    m_nav->highlight(m_current);
    scheduleCounters();
}

void EditorView::scheduleCounters() { m_counterTimer.start(150); }

int EditorView::chapterWords(const QString &chId)
{
    if (auto it = m_wordCache.constFind(chId); it != m_wordCache.constEnd()) return *it;
    int n = 0;
    if (ChapterEdit *e = editorFor(chId)) {
        QString text;
        for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next())
            if (!doc::hasClass(b, "ghost")) text += doc::blockText(b) + '\n';
        n = countWords(text);
    } else if (m_app->session()) {
        n = countWords(chapterPlainText(m_app->session()->chapterHtml(chId)));
    }
    m_wordCache.insert(chId, n);
    return n;
}

int EditorView::bookWords()
{
    BookSession *s = m_app->session();
    if (!s) return 0;
    int n = 0;
    for (const QString &chId : s->order())
        if (isStory(chId, s->book())) n += chapterWords(chId);
    return n;
}

void EditorView::updateCounters()
{
    BookSession *s = m_app->session();
    if (!s) return;
    QJsonObject &b = s->book();
    const int total = bookWords();
    const QString cur = s->order().contains(m_current) ? m_current : QString();
    // a selection reports its size
    if (ChapterEdit *e = editorFor(cur); e && e->textCursor().hasSelection()) {
        const int n = countWords(e->textCursor().selectedText());
        if (n > 0) m_wordCounter->setText(t("{n} selected", {{"n", n}}));
    } else if (m_wordMode == "book") {
        m_wordCounter->setText(t("{n} words", {{"n", total}}));
    } else {
        const int n = cur.isEmpty() ? 0 : chapterWords(cur);
        m_wordCounter->setText(!cur.isEmpty() && chapterKind(cur, b) != "chapter"
                                   ? t("{name}: {n} words", {{"name", chapterName(cur, b)}, {"n", n}})
                                   : t("ch. {ch}: {n} words", {{"ch", cur.isEmpty() ? 0 : chapterNumber(cur, b)}, {"n", n}}));
    }
    if (m_app->library().value("posMode").toString() == "page") {
        m_posCounter->setText(t("{n} pages", {{"n", counters::pageCount(total)}}));
    } else if (cur.isEmpty()) {
        m_posCounter->setText(numberedChapters(b) > 1 ? t("{n} chapters", {{"n", numberedChapters(b)}}) : QString());
    } else if (cur == soloStory(b)) {
        m_posCounter->clear();
    } else if (chapterKind(cur, b) != "chapter") {
        m_posCounter->setText(chapterName(cur, b));
    } else {
        m_posCounter->setText(t("chapter {ch} of {total}", {{"ch", chapterNumber(cur, b)}, {"total", numberedChapters(b, cur)}}));
    }
    if (b.value("wordCount").toInt(-1) != total) {
        b.insert("wordCount", total);
    }
    bool changed = false;
    const QString today = counters::writingDay(QDateTime::currentDateTime(), m_app->library().value("dayEndsAt").toInt());
    const int words = counters::trackDaily(b, total, today, &changed);
    const int goal = m_app->library().value("dailyGoal").toInt();
    m_goalCounter->setText(goal ? t("{n} / {goal} today", {{"n", words}, {"goal", goal}}) : t("{n} today", {{"n", words}}));
    if (changed) s->saveMeta();
}

void EditorView::trackScroll()
{
    // which chapter you're scrolled to
    const int mid = int(m_scroll->viewport()->height() * 0.4);
    QString best;
    for (ChapterSheet *sheet : m_chapters) {
        const int top = sheet->mapTo(m_scroll->viewport(), QPoint(0, 0)).y();
        if (top < mid) best = sheet->chId;
    }
    if (!best.isEmpty() && best != m_current) {
        m_current = best;
        m_nav->highlight(best);
        updateCounters();
    }
}

// ---------------------------------------------------------------------------
// ChapterHost

bool EditorView::isStoryChapter(const QString &chId) const
{
    BookSession *s = m_app->session();
    return s && isStory(chId, s->book());
}

bool EditorView::isScriptBook() const
{
    BookSession *s = m_app->session();
    return s && isScript(s->book());
}

edit::TypingContext EditorView::typingContext(ChapterEdit *) const
{
    edit::TypingContext tc;
    tc.language = m_app->writingLanguage();
    tc.uiLocale = I18n::locale();
    tc.markdown = !m_app->library().value("markdownOff").toBool();
    tc.manuscript = true;
    tc.doubleQuotes = typing::quoteStyle(tc.language);
    return tc;
}

typing::QuoteStyle EditorView::bookQuoteStyle(ChapterEdit *e) const
{
    BookSession *s = m_app->session();
    const QString lang = m_app->writingLanguage();
    if (!s) return typing::quoteStyle(lang);
    QString book;
    for (const QString &c : s->order()) book += s->chapterHtml(c) + ' ';
    return typing::bookQuotes(lang, e->document()->toPlainText(), book);
}

void EditorView::snapshot(const QString &label, bool rejoin)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    s->snapshot(label, rejoin, captureCaret());
}

void EditorView::splitChapter(ChapterEdit *e, int blockNumber)
{
    BookSession *s = m_app->session();
    if (!s) return;
    QList<Para> paras = doc::paragraphs(*e->document());
    auto blank = [](const Para &p) { return !p.hasClass("scene-break") && p.isBlank() && !p.hasMark(); };
    int from = std::clamp(blockNumber, 0, int(paras.size()));
    // an empty line is no way to start a chapter, or end one: blank lines at
    // the seam stay behind
    while (from < paras.size() - 1 && blank(paras[from])) paras.removeAt(from);
    while (from > 1 && blank(paras[from - 1])) {
        paras.removeAt(from - 1);
        from--;
    }
    QList<Para> head = paras.mid(0, from), tail = paras.mid(from);
    if (head.isEmpty()) head << Para{};
    if (tail.isEmpty()) tail << Para{};
    const QString chId = e->chId();
    s->setChapterHtml(chId, serializeChapter(head));
    s->persistChapter(chId);
    const QString newId = s->createChapterAt(s->order().indexOf(chId) + 1, serializeChapter(tail));
    rebuildChapters();
    focusChapter(newId);
    if (ChapterEdit *ne = editorFor(newId)) ne->breakRun()++;
}

void EditorView::deleteChapterQuiet(const QString &chId)
{
    if (QTimer *t = m_saveTimers.take(chId)) {
        t->stop();
        t->deleteLater();
    }
    m_wordCache.remove(chId);
    m_app->session()->deleteChapter(chId);
}

void EditorView::removeEmptyChapter(ChapterEdit *e)
{
    BookSession *s = m_app->session();
    if (!s || s->order().size() < 2) return;
    const QStringList order = s->order();
    const int idx = int(order.indexOf(e->chId()));
    snapshot("empty chapter removed", false);
    deleteChapterQuiet(e->chId());
    rebuildChapters();
    if (idx > 0) focusChapter(order[idx - 1], true);
    else focusChapter(order.value(1));
    if (ChapterEdit *f = editorFor(m_current)) f->breakRun()++;
    m_side->rebuild();
}

void EditorView::backspaceAtStart(ChapterEdit *e)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    const QString chId = e->chId();
    const QStringList order = s->order();
    const int idx = int(order.indexOf(chId));
    if (idx <= 0) return;
    const QString prevId = order[idx - 1];
    if (chapterKind(prevId, s->book()) == "contents") return;
    const bool prevEmpty = chapterPlainText(s->chapterHtml(prevId)).trimmed().isEmpty();
    // only story runs into story: a page above stays a page
    if (!prevEmpty && !(isStory(chId, s->book()) && isStory(prevId, s->book()))) return;
    if (prevEmpty) {
        snapshot("empty chapter removed", false);
        deleteChapterQuiet(prevId);
        rebuildChapters();
        focusChapter(chId);
        if (ChapterEdit *f = editorFor(chId)) f->breakRun()++;
        return;
    }
    // words above: this chapter runs on into that one (⌘Z splits them again)
    snapshot("chapters merged", false);
    const int prevCount = int(parseChapter(s->chapterHtml(prevId)).size());
    s->setChapterHtml(prevId, s->chapterHtml(prevId) + s->chapterHtml(chId));
    s->persistChapter(prevId);
    QJsonArray stickies;
    for (const auto &v : s->stickies()) {
        QJsonObject o = v.toObject();
        if (o.value("chapterId").toString() == chId) o.insert("chapterId", prevId);
        stickies << o;
    }
    s->stickies() = stickies;
    s->saveStickies();
    QJsonArray darlings;
    for (const auto &v : s->darlings()) {
        QJsonObject o = v.toObject();
        if (o.value("chapterId").toString() == chId) o.insert("chapterId", prevId);
        darlings << o;
    }
    s->darlings() = darlings;
    s->saveDarlings();
    QJsonObject sectionNotes = s->book().value("sectionNotes").toObject();
    if (sectionNotes.contains(chId)) {
        QJsonArray merged = sectionNotes.value(prevId).toArray();
        for (const auto &n : sectionNotes.value(chId).toArray()) merged << n;
        sectionNotes.insert(prevId, merged);
        sectionNotes.remove(chId);
        s->book().insert("sectionNotes", sectionNotes);
    }
    for (const char *key : {"chapterTitles", "chapterNotes", "chapterKinds"}) {
        if (!s->book().contains(key)) continue;
        QJsonObject o = s->book().value(key).toObject();
        o.remove(chId);
        s->book().insert(key, o);
    }
    QStringList o = s->order();
    o.removeAll(chId);
    s->setOrder(o);
    m_app->lib().deleteChapter(s->id(), chId);
    s->saveMeta();
    rebuildChapters();
    restoreCaret(QJsonObject{{"chId", prevId}, {"pIdx", prevCount}, {"off", 0}});
    if (ChapterEdit *f = editorFor(prevId)) f->breakRun()++;
    m_side->rebuild();
}

bool EditorView::structuralUndo()
{
    BookSession *s = m_app->session();
    if (!s || !s->canUndoStructure()) return false;
    auto snap = s->undoStructure();
    if (!snap) return false;
    rebuildChapters();
    m_side->rebuild();
    m_aux->refresh();
    restoreCaret(snap->caret);
    if (snap->rejoin) rejoinAtCaret();
    updateCounters();
    return true;
}

void EditorView::rejoinAtCaret()
{
    // after undoing a double-Enter break, the split the gesture made closes:
    // the caret's paragraph flows back into the one above it
    ChapterEdit *e = editorFor(m_current);
    if (!e) return;
    QTextCursor c = e->textCursor();
    QTextBlock b = c.block(), prev = b.previous();
    if (!prev.isValid()) return;
    const QStringList bc = doc::classes(b), pc = doc::classes(prev);
    if (bc.contains("scene-break") || pc.contains("scene-break")) return;
    if (bc.contains("poetry") != pc.contains("poetry") || bc.contains("flush") != pc.contains("flush")) return;
    const int at = prev.position() + prev.length() - 1;
    if (edit::isBlank(b)) {
        edit::removeParagraph(b);
    } else {
        c.setPosition(at);
        c.setPosition(b.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    c.setPosition(at);
    e->setTextCursor(c);
    e->resetUndo();
}

void EditorView::markResolved(const QString &sid)
{
    BookSession *s = m_app->session();
    if (!s) return;
    for (ChapterSheet *sheet : m_chapters) {
        QTextDocument *d = sheet->edit->document();
        for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (f.charFormat().property(doc::MarkSid).toString() != sid) continue;
                QTextCursor c(d);
                c.setPosition(f.position());
                c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
                c.removeSelectedText();
                // removing a flag between two spaces shouldn't leave both
                const QString text = b.text();
                const int at = c.positionInBlock();
                if (at > 0 && at < text.size() && text[at - 1] == ' ' && text[at] == ' ') {
                    c.setPosition(c.position() + 1, QTextCursor::KeepAnchor);
                    c.removeSelectedText();
                }
                sheet->edit->setTextCursor(c);
                goto done;
            }
    }
done:
    QJsonArray kept;
    for (const auto &v : s->stickies())
        if (v.toObject().value("id").toString() != sid) kept << v;
    s->stickies() = kept;
    s->saveStickies();
    m_side->rebuild();
    m_nav->scheduleRefresh();
}

bool EditorView::scriptKey(ChapterEdit *, QKeyEvent *) { return false; }

// ---------------------------------------------------------------------------

void EditorView::newChapterAfter(const QString &chId)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    const qsizetype at = chId.isEmpty() ? s->order().size() : s->order().indexOf(chId) + 1;
    const QString id = s->createChapterAt(at);
    rebuildChapters();
    focusChapter(id);
}

void EditorView::deleteChapterToDarlings(const QString &chId)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    snapshot("chapter delete", false);
    const QString kind = chapterKind(chId, s->book());
    const QString name = chapterName(chId, s->book());
    const QString html = s->chapterHtml(chId);
    const QString text = chapterPlainText(html).trimmed();
    if (!text.isEmpty()) {
        QJsonArray d = s->darlings();
        d.append(QJsonObject{{"id", newId("d-")},
                             {"html", html},
                             {"text", text.left(2000)},
                             {"chapterId", QJsonValue()},
                             {"chapterLabel", kind != "chapter" ? name : t("deleted Chapter {n}", {{"n", chapterNumber(chId, s->book())}})},
                             {"date", isoNow()}});
        s->darlings() = d;
        s->saveDarlings();
    }
    if (m_current == chId) m_current.clear();
    deleteChapterQuiet(chId);
    rebuildChapters();
    m_side->rebuild();
    if (!text.isEmpty())
        emit m_app->toast(kind == "chapter" ? t("Chapter removed — its words are in Darlings, or {key} to undo", {{"key", "Ctrl+Z"}})
                                            : t("{name} removed — its words are in Darlings, or {key} to undo", {{"name", name}, {"key", "Ctrl+Z"}}));
}

void EditorView::chapterMenu(const QString &chId, QPoint globalPos)
{
    BookSession *s = m_app->session();
    if (!s) return;
    syncAll();
    const QString kind = chapterKind(chId, s->book());
    const int words = countWords(chapterPlainText(s->chapterHtml(chId)));
    bool otherContents = false;
    for (const QString &c : s->order()) otherContents |= c != chId && chapterKind(c, s->book()) == "contents";
    QMenu m(this);
    m.setTitle(chapterName(chId, s->book()));
    QHash<QAction *, QString> kinds;
    for (const QString &k : kChapterKinds) {
        QAction *a = m.addAction(kindName(k));
        a->setCheckable(true);
        a->setChecked(k == kind);
        a->setEnabled(!(k == "contents" && k != kind && (otherContents || words > 0)));
        kinds.insert(a, k);
    }
    QAction *exportAct = nullptr, *restart = nullptr;
    if (kStoryKinds.contains(kind) && words > 0) {
        m.addSeparator();
        exportAct = m.addAction(t("Export Chapter…"));
    }
    if (kind == "part") {
        m.addSeparator();
        restart = m.addAction(t("Restart Chapter Numbers at Each Part"));
        restart->setCheckable(true);
        restart->setChecked(s->book().value("restartNumbering").toBool());
    }
    m.addSeparator();
    QAction *del = m.addAction(t("Delete"));
    QAction *picked = m.exec(globalPos);
    if (!picked) return;
    if (picked == exportAct) {
        emit exportChapter(chId);
        return;
    }
    if (picked == restart) {
        if (s->book().value("restartNumbering").toBool()) s->book().remove("restartNumbering");
        else s->book().insert("restartNumbering", true);
        s->saveMeta();
        applyLook();
        m_nav->rebuild();
        updateCounters();
        return;
    }
    if (picked == del) {
        deleteChapterToDarlings(chId);
        return;
    }
    const QString k = kinds.value(picked);
    if (k.isEmpty() || k == kind) return;
    snapshot("chapter kind", false);
    setChapterKind(s->book(), chId, k);
    // a copyright page starts with what every copyright page says
    if (k == "copyright" && words == 0) {
        const QString name = s->book().value("author").toString().isEmpty() ? m_app->authorName() : s->book().value("author").toString();
        s->setChapterHtml(chId, "<p>" + escHtml(t("Copyright © {year} {name}", {{"year", QString::number(QDate::currentDate().year())}, {"name", name}}))
                                    + "</p><p>" + escHtml(t("All rights reserved.")) + "</p>");
        s->persistChapter(chId);
    }
    s->saveMeta();
    rebuildChapters();
    updateCounters();
}

void EditorView::insertPlaceholder()
{
    BookSession *s = m_app->session();
    ChapterEdit *e = editorFor(m_current);
    if (!s || !e || !e->hasFocus()) {
        emit m_app->toast(t("Click into a chapter first, then {key} drops a placeholder", {{"key", "Ctrl+Shift+X"}}));
        return;
    }
    const QString sid = "s-" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    QTextCursor c = e->textCursor();
    c.clearSelection();
    c.insertText(QStringLiteral("⚑"), doc::markFormat(sid));
    c.insertText(" ", QTextCharFormat());
    e->setTextCursor(c);
    QJsonArray st = s->stickies();
    st.append(QJsonObject{{"id", sid}, {"chapterId", m_current}, {"text", ""}, {"resolved", false}});
    s->stickies() = st;
    s->saveStickies();
    m_side->rebuild();
    m_side->focusSticky(sid, true);
    m_nav->scheduleRefresh();
}

void EditorView::refreshFromDisk()
{
    BookSession *s = m_app->session();
    if (!s || !isVisible()) return;
    // the page holds what the editors hold
    syncAll();
    const QString when = QLocale(I18n::locale()).toString(QTime::currentTime(), QLocale::ShortFormat);
    const RefreshResult r = s->refresh(t("from other device, {time}", {{"time", when}}));
    if (!r.changed() && !r.displaced) return;
    const QJsonObject caret = captureCaret();
    rebuildChapters();
    if (!caret.isEmpty()) restoreCaret(caret);
    m_side->rebuild();
    m_aux->refresh();
    updateCounters();
    if (r.conflicts) emit m_app->toast(t("This chapter also changed on another device. That version is saved as the chapter after it."), 8000);
    else if (r.displaced) emit m_app->toast(t("Updated from your other device — the text it replaced is in Darlings"), 8000);
    else emit m_app->toast(t("Updated from your other device"));
}

void EditorView::openSidePane() { m_side->open(); }

void EditorView::refreshSidePane()
{
    QTimer::singleShot(0, this, [this] { m_side->rebuild(); });
}

bool EditorView::zoomCards(int dir)
{
    if (m_tab != "outline" || !m_aux->outline()->showingCards()) return false;
    m_aux->outline()->stepZoom(dir);
    return true;
}

void EditorView::refreshOutline()
{
    if (m_tab == "outline") m_aux->refresh();
}

} // namespace neosea

#include "moc_editorview.cpp"
