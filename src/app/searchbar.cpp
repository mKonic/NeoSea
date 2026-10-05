#include "app/searchbar.h"

#include "app/app.h"
#include "app/auxpage.h"
#include "app/editorview.h"
#include "app/decorations.h"
#include "app/theme.h"
#include "core/booksession.h"
#include "core/i18n.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>

namespace neosea {

SearchBar::SearchBar(EditorView *view) : QFrame(view), m_view(view)
{
    setObjectName("searchbar");
    const bool light = theme().name != "night";
    setStyleSheet(QStringLiteral("#searchbar { background: %1; border: 1px solid %2; border-radius: 8px; } "
                                 "QLineEdit { background: %3; border: 1px solid %2; border-radius: 5px; color: %4; font-size: 13px; padding: 5px 9px; } "
                                 "QLineEdit:focus { border-color: %5; } "
                                 "QPushButton { background: none; border: 1px solid %2; border-radius: 5px; color: %6; font-size: 12px; padding: 4px 9px; } "
                                 "QPushButton:hover { color: %5; border-color: %5; } QLabel { color: %7; font-size: 11px; }")
                      .arg(light ? "#ffffff" : "#262626", light ? "#d9d4ca" : "#3a3a3a", light ? "#faf9f6" : theme().bg.name(), light ? "#1c1a17" : "#eeeeee",
                           theme().accent.name(), light ? "#5a564e" : "#aaaaaa", theme().muted.name()));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(8);
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(t("Find"));
    m_find->setFixedWidth(170);
    m_count = new QLabel(this);
    m_count->setMinimumWidth(52);
    m_count->setAlignment(Qt::AlignCenter);
    auto *prev = new QPushButton(QStringLiteral("↑"), this);
    prev->setToolTip(t("Previous (⇧Enter)"));
    auto *next = new QPushButton(QStringLiteral("↓"), this);
    next->setToolTip(t("Next (Enter)"));
    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(t("Replace with"));
    m_replace->setFixedWidth(170);
    m_one = new QPushButton(t("Replace"), this);
    m_all = new QPushButton(t("All"), this);
    auto *x = new QPushButton(QStringLiteral("✕"), this);
    x->setToolTip(t("Close (Esc)"));
    for (QPushButton *b : {prev, next, m_one, m_all, x}) b->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_find);
    row->addWidget(m_count);
    row->addWidget(prev);
    row->addWidget(next);
    row->addWidget(m_replace);
    row->addWidget(m_one);
    row->addWidget(m_all);
    row->addWidget(x);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &SearchBar::run);
    // an edit on the page moves the matches: find them again where they are now
    m_rerun.setSingleShot(true);
    m_rerun.setInterval(150);
    connect(&m_rerun, &QTimer::timeout, this, [this] {
        if (isHidden() || m_replacing) return;
        const int keep = m_idx;
        run();
        if (keep >= 0 && !m_hits.isEmpty()) {
            m_idx = std::min(keep, int(m_hits.size()) - 1);
            paint();
            m_count->setText(t("{i} of {n}", {{"i", m_idx + 1}, {"n", m_hits.size()}}));
        }
    });
    connect(m_find, &QLineEdit::textEdited, this, &SearchBar::runSoon);
    connect(prev, &QPushButton::clicked, this, [this] { freshIfStale(); gotoMatch(m_idx - 1); });
    connect(next, &QPushButton::clicked, this, [this] { freshIfStale(); gotoMatch(m_idx + 1); });
    connect(m_one, &QPushButton::clicked, this, &SearchBar::replaceCurrent);
    connect(m_all, &QPushButton::clicked, this, &SearchBar::replaceAll);
    connect(x, &QPushButton::clicked, this, &SearchBar::close);
    m_find->installEventFilter(this);
    m_replace->installEventFilter(this);
    hide();
}

void SearchBar::watch(QTextEdit *e)
{
    connect(e, &QTextEdit::textChanged, &m_rerun, [this] {
        if (!isHidden()) m_rerun.start();
    });
}

void SearchBar::place()
{
    adjustSize();
    move((parentWidget()->width() - width()) / 2, 14);
    raise();
}

QList<QTextEdit *> SearchBar::roots() const
{
    QList<QTextEdit *> out;
    BookSession *s = m_view->app()->session();
    if (!s) return out;
    if (m_view->tab() == "manuscript") {
        for (const QString &chId : s->order())
            if (ChapterEdit *e = m_view->editorFor(chId); e && !e->isHidden()) out << e;
    } else if (m_view->tab() == "notes") {
        out << m_view->auxPage()->notesEdit();
    }
    return out;
}

void SearchBar::open()
{
    if (!m_view->app()->session()) {
        emit m_view->app()->toast(t("Open a book first"));
        return;
    }
    // what's selected is what to find; where the caret was is where Esc goes back to
    auto *ed = qobject_cast<QTextEdit *>(QApplication::focusWidget());
    if (ed && roots().contains(ed)) {
        const QString sel = ed->textCursor().selectedText().left(80).trimmed();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator)) m_find->setText(sel);
        m_homeEd = ed;
        m_homePos = ed->textCursor().position();
    }
    show();
    m_find->setFocus();
    m_find->selectAll();
    retab();
}

void SearchBar::retab()
{
    if (isHidden()) return;
    const bool manuscript = m_view->tab() == "manuscript";
    m_replace->setVisible(manuscript);
    m_one->setVisible(manuscript);
    m_all->setVisible(manuscript);
    place();
    run();
}

void SearchBar::close()
{
    hide();
    m_hits.clear();
    m_idx = -1;
    m_query.clear();
    paint();
    for (QTextEdit *e : roots()) deco::set(e, "search", {});
}

void SearchBar::run()
{
    const QString q = m_find->text();
    for (const Hit &h : m_hits)
        if (h.ed) deco::set(h.ed, "search", {});
    m_hits.clear();
    m_idx = -1;
    m_query = q;
    m_tab = m_view->tab();
    if (q.isEmpty()) {
        m_count->clear();
        return;
    }
    for (QTextEdit *e : roots())
        for (const search::Match &m : search::find(*e->document(), q)) m_hits << Hit{e, m};
    m_count->setText(m_hits.isEmpty() ? t("none") : t("{n} found", {{"n", m_hits.size()}}));
    paint();
}

void SearchBar::freshIfStale()
{
    if (m_query != m_find->text() || m_tab != m_view->tab()) run();
}

void SearchBar::paint()
{
    QHash<QTextEdit *, QList<QTextEdit::ExtraSelection>> per;
    QTextCharFormat all, cur;
    QColor soft = theme().accent;
    soft.setAlphaF(0.35);
    all.setBackground(soft);
    cur.setBackground(theme().accent);
    cur.setForeground(QColor("#1c1c1c"));
    for (int i = 0; i < m_hits.size(); ++i) {
        const Hit &h = m_hits[i];
        if (!h.ed) continue;
        QTextEdit::ExtraSelection x;
        x.cursor = QTextCursor(h.ed->document());
        x.cursor.setPosition(h.m.pos);
        x.cursor.setPosition(h.m.pos + h.m.len, QTextCursor::KeepAnchor);
        x.format = i == m_idx ? cur : all;
        per[h.ed] << x;
    }
    for (QTextEdit *e : roots()) deco::set(e, "search", per.value(e));
}

void SearchBar::reveal(QTextEdit *ed, int pos)
{
    QTextCursor c(ed->document());
    c.setPosition(pos);
    const QRect r = ed->cursorRect(c);
    if (ed->verticalScrollBar()->maximum() > 0) {
        ed->verticalScrollBar()->setValue(ed->verticalScrollBar()->value() + r.top() - int(ed->viewport()->height() * 0.45));
        return;
    }
    // a page that grows to its text: the room around it scrolls
    for (QWidget *w = ed->parentWidget(); w; w = w->parentWidget())
        if (auto *sa = qobject_cast<QScrollArea *>(w); sa && sa->widget() && sa->widget()->isAncestorOf(ed)) {
            const QPoint p = ed->viewport()->mapTo(sa->widget(), r.topLeft());
            sa->verticalScrollBar()->setValue(p.y() - int(sa->viewport()->height() * 0.45));
            return;
        }
}

void SearchBar::gotoMatch(int i)
{
    if (m_hits.isEmpty()) return;
    const int n = int(m_hits.size());
    m_idx = ((i % n) + n) % n;
    paint();
    if (const Hit &h = m_hits[m_idx]; h.ed) reveal(h.ed, h.m.pos);
    m_count->setText(t("{i} of {n}", {{"i", m_idx + 1}, {"n", n}}));
}

void SearchBar::replaceCurrent()
{
    if (m_view->tab() != "manuscript") return;
    freshIfStale();
    if (m_hits.isEmpty()) {
        emit m_view->app()->toast(t("No matches"));
        return;
    }
    if (m_idx < 0) m_idx = 0; // start from the very first match
    const Hit h = m_hits[m_idx];
    if (!h.ed) return run();
    const int old = m_idx;
    m_replacing = true;
    search::replace(*h.ed->document(), h.m, m_replace->text());
    m_replacing = false;
    run();
    if (!m_hits.isEmpty()) gotoMatch(std::min(old, int(m_hits.size()) - 1));
}

void SearchBar::replaceAll()
{
    const QString q = m_find->text();
    if (q.isEmpty() || m_view->tab() != "manuscript") return;
    int total = 0;
    for (QTextEdit *e : roots()) total += int(search::find(*e->document(), q).size());
    if (!total) {
        emit m_view->app()->toast(t("0 replaced"));
        return;
    }
    // every chapter, front to back, as one step back
    m_view->syncAll();
    m_view->snapshot("replace all", false);
    m_replacing = true;
    int n = 0;
    for (QTextEdit *e : roots()) {
        n += search::replaceAll(*e->document(), q, m_replace->text());
        // the book's Ctrl+Z takes it all back, not a chapter at a time
        if (auto *ce = qobject_cast<ChapterEdit *>(e)) ce->resetUndo();
    }
    m_replacing = false;
    emit m_view->app()->toast(t("{n} replaced across the whole book — {key} to undo", {{"n", n}, {"key", "Ctrl+Z"}}));
    run();
}

// Esc: the caret goes back to the page, to the match last gone to, or else
// where it was before Find
void SearchBar::returnToPage()
{
    QPointer<QTextEdit> ed;
    int pos = 0;
    if (m_idx >= 0 && m_idx < m_hits.size() && m_hits[m_idx].ed) {
        ed = m_hits[m_idx].ed;
        pos = m_hits[m_idx].m.pos;
    } else if (m_homeEd) {
        ed = m_homeEd;
        pos = std::min(m_homePos, ed->document()->characterCount() - 1);
    }
    close();
    if (!ed) return;
    QTextCursor c = ed->textCursor();
    c.setPosition(pos);
    ed->setTextCursor(c);
    ed->setFocus();
    reveal(ed, pos);
}

bool SearchBar::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() != QEvent::KeyPress) return false;
    auto *k = static_cast<QKeyEvent *>(e);
    if (k->key() == Qt::Key_Escape) {
        returnToPage();
        return true;
    }
    const bool enter = k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter;
    if (o == m_replace && enter) {
        replaceCurrent();
        return true;
    }
    if (o != m_find) return false;
    if (enter) {
        freshIfStale();
        gotoMatch(m_idx + ((k->modifiers() & Qt::ShiftModifier) ? -1 : 1));
        return true;
    }
    if (k->key() == Qt::Key_Tab && !(k->modifiers() & Qt::ShiftModifier)) {
        // Tab: write at the match, its end
        if (m_hits.isEmpty()) return false;
        const Hit h = m_hits[std::max(0, m_idx)];
        if (!h.ed) return false;
        QTextCursor c = h.ed->textCursor();
        c.setPosition(h.m.pos + h.m.len);
        h.ed->setTextCursor(c);
        h.ed->setFocus();
        return true;
    }
    return false;
}

} // namespace neosea

#include "moc_searchbar.cpp"
