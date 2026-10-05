#include "app/spellpass.h"

#include "app/app.h"
#include "app/chapteredit.h"
#include "app/decorations.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/i18n.h"
#include "core/spell.h"
#include "core/textdoc.h"
#include "core/typing.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTextBlock>
#include <QTextEdit>

namespace neosea {

SpellPass::SpellPass(EditorView *view) : QObject(view), m_view(view)
{
    connect(view->app(), &App::spellerChanged, this, [this] {
        m_cache.clear();
        recheckAll();
    });
}

void SpellPass::watch(QTextEdit *e)
{
    auto *timer = new QTimer(e);
    timer->setSingleShot(true);
    timer->setInterval(600);
    m_rescan.insert(e, timer);
    connect(timer, &QTimer::timeout, this, [this, e] {
        if (m_on && m_scanned.contains(e)) scan(e);
    });
    connect(e->document(), &QTextDocument::contentsChanged, timer, [timer] { timer->start(); });
    connect(e, &QObject::destroyed, this, [this, e] {
        m_scanned.remove(e);
        m_caps.remove(e);
        m_rescan.remove(e);
    });
}

void SpellPass::toggle()
{
    m_on = !m_on;
    if (m_on) {
        m_scanned.clear();
        if (auto *e = qobject_cast<QTextEdit *>(QApplication::focusWidget())) here(e);
        else if (ChapterEdit *c = m_view->editorFor(m_view->currentChapter())) here(c);
    } else {
        for (QTextEdit *e : m_rescan.keys()) clear(e);
        m_scanned.clear();
    }
    emit m_view->app()->toast(m_on ? t("Spellcheck on") : t("Spellcheck off"));
}

void SpellPass::here(QTextEdit *e)
{
    if (m_on && e && m_rescan.contains(e) && !m_scanned.contains(e)) scan(e);
}

void SpellPass::recheckAll()
{
    if (!m_on) return;
    const auto pages = m_scanned;
    for (QTextEdit *e : pages) scan(e);
}

void SpellPass::clear(QTextEdit *e)
{
    deco::set(e, "spell", {});
    m_caps.remove(e);
}

bool SpellPass::correct(const QString &word)
{
    auto it = m_cache.constFind(word);
    if (it != m_cache.constEnd()) return *it;
    const bool ok = m_view->app()->speller().check(word);
    // a dictionary still loading calls everything fine: don't remember that
    if (m_view->app()->speller().loaded()) m_cache.insert(word, ok);
    return ok;
}

void SpellPass::scan(QTextEdit *e)
{
    m_scanned.insert(e);
    const bool manuscript = qobject_cast<ChapterEdit *>(e) != nullptr;
    const bool english = m_view->app()->writingLanguage().startsWith("en");
    QTextCharFormat squiggle;
    squiggle.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    squiggle.setUnderlineColor(theme().red);
    QList<QTextEdit::ExtraSelection> marks;
    QSet<int> caps;
    auto mark = [&](int from, int to) {
        QTextEdit::ExtraSelection x;
        x.cursor = QTextCursor(e->document());
        x.cursor.setPosition(from);
        x.cursor.setPosition(to, QTextCursor::KeepAnchor);
        x.format = squiggle;
        marks << x;
    };
    for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next()) {
        const QStringList cls = doc::classes(b);
        if (cls.contains("scene-break") || cls.contains("ghost")) continue;
        const QString text = b.text();
        for (const auto &[from, to] : spell::wrong(spell::occurrences(text), [this](const QString &w) { return correct(w); }))
            mark(b.position() + from, b.position() + to);
        // capitals: manuscript prose only (notes are scratch paper, and poetry sets its own case)
        if (manuscript && !cls.contains("poetry"))
            for (qsizetype i : typing::capitalSlips(text, english)) {
                caps.insert(b.position() + int(i));
                mark(b.position() + int(i), b.position() + int(i) + 1);
            }
    }
    m_caps.insert(e, caps);
    deco::set(e, "spell", marks);
}

bool SpellPass::menu(QTextEdit *e, QContextMenuEvent *ev)
{
    if (!m_on || !m_scanned.contains(e)) return false;
    const QTextCursor at = e->cursorForPosition(ev->pos());
    const QTextBlock b = at.block();
    const QString text = b.text();
    const int off = at.positionInBlock();
    auto isW = [&](int i) { return i >= 0 && i < text.size() && (text[i].isLetter() || text[i].isMark() || text[i] == '\'' || text[i] == QChar(0x2019)); };
    int a = off, z = off;
    while (isW(a - 1)) a--;
    while (isW(z)) z++;
    QMenu m(e);
    auto replaceWith = [this, e](int from, int to, const QString &with) {
        QTextCursor c(e->document());
        c.setPosition(from);
        c.setPosition(from + 1, QTextCursor::KeepAnchor);
        const QTextCharFormat fmt = c.charFormat();
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        c.insertText(with, fmt);
        scan(e);
    };
    // a capital slip: the letter's capital, and nothing to learn
    if (m_caps.value(e).contains(b.position() + a)) {
        const int pos = b.position() + a;
        const QString upper = QLocale(m_view->app()->writingLanguage()).toUpper(text.mid(a, 1));
        QAction *up = m.addAction(upper);
        if (m.exec(ev->globalPos()) == up) replaceWith(pos, pos + 1, upper);
        return true;
    }
    if (a == z) return false;
    QString word = spell::norm(text.mid(a, z - a));
    int from = a, to = z;
    if (correct(word)) {
        // …or a hyphenated word underlined whole: every piece a word, the whole not
        int wa = a, wb = z;
        while (wa >= 2 && text[wa - 1] == '-' && isW(wa - 2)) {
            wa--;
            while (isW(wa - 1)) wa--;
        }
        while (wb + 1 < text.size() && text[wb] == '-' && isW(wb + 1)) {
            wb++;
            while (isW(wb)) wb++;
        }
        const QString whole = spell::norm(text.mid(wa, wb - wa));
        if (whole == word || correct(whole)) return false; // only flagged words get our menu
        for (const QString &piece : text.mid(wa, wb - wa).split('-'))
            if (!correct(spell::norm(piece))) return false;
        from = wa;
        to = wb;
        word = whole;
    }
    const QStringList sugg = m_view->app()->speller().suggest(word);
    QList<QAction *> picks;
    for (const QString &s : sugg) picks << m.addAction(s);
    if (sugg.isEmpty()) m.addAction(t("No suggestions"))->setEnabled(false);
    m.addSeparator();
    QAction *learn = m.addAction(t("Add “{word}” to dictionary", {{"word", word}}));
    QAction *picked = m.exec(ev->globalPos());
    if (!picked) return true;
    if (picked == learn) {
        m_view->app()->learnWord(word);
        m_cache.clear();
        m_cache.insert(word, true);
        recheckAll();
    } else if (picks.contains(picked)) {
        replaceWith(b.position() + from, b.position() + to, picked->text());
    }
    return true;
}

} // namespace neosea

#include "moc_spellpass.cpp"
