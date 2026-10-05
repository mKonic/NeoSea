#include "app/readaloud.h"

#include "app/app.h"
#include "app/auxpage.h"
#include "app/chapteredit.h"
#include "app/decorations.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/i18n.h"
#include "core/sentences.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>

#ifdef NEOSEA_HAVE_TTS
#include <QTextToSpeech>
#include <QVoice>
#endif

namespace neosea {

ReadAloud::ReadAloud(EditorView *view) : QObject(view), m_view(view) {}

void ReadAloud::toggle()
{
    if (m_on) return stop(true);
#ifndef NEOSEA_HAVE_TTS
    emit m_view->app()->toast(t("Read aloud needs a voice on this computer"));
#else
    if (!m_voice) {
        // NEOSEA_TTS_ENGINE picks one (mock, for a machine with no voice); else the system's own
        const QString engine = qEnvironmentVariable("NEOSEA_TTS_ENGINE");
        m_voice = engine.isEmpty() ? new QTextToSpeech(this) : new QTextToSpeech(engine, this);
        connect(m_voice, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State s) {
            if (!m_on) return;
            if (s == QTextToSpeech::Ready) next();
            else if (s == QTextToSpeech::Error) stop(false);
        });
    }
    if (m_voice->state() == QTextToSpeech::Error) {
        emit m_view->app()->toast(t("Read aloud needs a voice on this computer"));
        return;
    }
    // a voice in the language being written, else the system's
    const QLocale want(m_view->app()->writingLanguage().replace('-', '_'));
    for (const QLocale &l : m_voice->availableLocales())
        if (l.language() == want.language()) {
            m_voice->setLocale(l);
            break;
        }
    // the pages to read, from the caret: the rest of its page, then (in the manuscript) the chapters after
    auto *here = qobject_cast<QTextEdit *>(QApplication::focusWidget());
    QList<QTextEdit *> pages;
    BookSession *s = m_view->app()->session();
    if (!s) return;
    if (m_view->tab() == "manuscript") {
        for (const QString &chId : s->order())
            if (ChapterEdit *e = m_view->editorFor(chId); e && !e->isHidden()) pages << e;
        if (!pages.contains(here)) here = m_view->editorFor(m_view->currentChapter()); // no caret in the text: the chapter on screen
    } else if (m_view->tab() == "notes") {
        pages << m_view->auxPage()->notesEdit();
        here = pages.first();
    }
    if (!here || !pages.contains(here)) return;
    const QString lang = m_view->app()->writingLanguage();
    const QTextCursor c = here->textCursor();
    const bool caretHere = here == QApplication::focusWidget();
    m_queue.clear();
    for (qsizetype i = pages.indexOf(here); i < pages.size(); ++i) {
        QTextEdit *p = pages[i];
        for (QTextBlock b = p->document()->begin(); b.isValid(); b = b.next()) {
            if (p == here && caretHere && b.blockNumber() < c.blockNumber()) continue;
            const QStringList cls = doc::classes(b);
            if (cls.contains("scene-break") || cls.contains("ghost")) continue;
            int from = 0;
            // start at the beginning of the sentence the caret is in
            if (p == here && caretHere && b == c.block()) from = sentenceAt(b.text(), c.positionInBlock(), lang).first;
            for (const auto &[a, z] : sentenceSpans(b.text(), from, lang)) m_queue << Item{p, b.blockNumber(), a, z};
        }
    }
    if (m_queue.isEmpty()) return;
    m_on = true;
    ++m_gen;
    qApp->installEventFilter(this);
    m_voice->stop();
    next();
#endif
}

void ReadAloud::next()
{
#ifdef NEOSEA_HAVE_TTS
    if (!m_on) return;
    while (!m_queue.isEmpty() && !m_queue.first().page) m_queue.removeFirst();
    if (m_queue.isEmpty()) return stop(false);
    m_now = m_queue.takeFirst();
    const QTextBlock b = m_now.page->document()->findBlockByNumber(m_now.block);
    QString text = b.text().mid(m_now.a, m_now.b - m_now.a).trimmed();
    light(m_now);
    m_voice->say(text.remove(QStringLiteral("⚑")));
#endif
}

void ReadAloud::light(const Item &it)
{
    for (QTextEdit *e : m_view->findChildren<QTextEdit *>()) deco::set(e, "speak", {});
    if (!it.page) return;
    const QTextBlock b = it.page->document()->findBlockByNumber(it.block);
    QTextEdit::ExtraSelection x;
    x.cursor = QTextCursor(it.page->document());
    x.cursor.setPosition(b.position() + it.a);
    x.cursor.setPosition(b.position() + it.b, QTextCursor::KeepAnchor);
    QColor lit = theme().accent;
    lit.setAlphaF(0.28);
    x.format.setBackground(lit);
    deco::set(it.page, "speak", {x});
    // keep the sentence on screen
    QTextCursor at(it.page->document());
    at.setPosition(b.position() + it.a);
    const QRect r = it.page->cursorRect(at);
    if (auto *ce = qobject_cast<ChapterEdit *>(it.page.data())) {
        QScrollArea *sa = m_view->scrollArea();
        const int y = ce->viewport()->mapTo(sa->viewport(), r.topLeft()).y();
        if (y < 40 || y > sa->viewport()->height() - 60) sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->value() + y - sa->viewport()->height() / 3);
    } else {
        QTextCursor keep = it.page->textCursor();
        it.page->setTextCursor(at);
        it.page->ensureCursorVisible();
        it.page->setTextCursor(keep);
    }
}

void ReadAloud::stop(bool leaveCaret)
{
    if (!m_on) return;
    m_on = false;
    ++m_gen;
    qApp->removeEventFilter(this);
#ifdef NEOSEA_HAVE_TTS
    if (m_voice) m_voice->stop();
#endif
    for (QTextEdit *e : m_view->findChildren<QTextEdit *>()) deco::set(e, "speak", {});
    // the writer stopped it: the caret goes to the sentence reached
    if (leaveCaret && m_now.page) {
        QTextCursor c(m_now.page->document());
        c.setPosition(m_now.page->document()->findBlockByNumber(m_now.block).position() + m_now.a);
        m_now.page->setFocus();
        m_now.page->setTextCursor(c);
    }
    m_queue.clear();
}

bool ReadAloud::eventFilter(QObject *, QEvent *e)
{
    if (!m_on || e->type() != QEvent::KeyPress) return false;
    auto *k = static_cast<QKeyEvent *>(e);
    switch (k->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock: return false;
    default: break;
    }
    // Ctrl+Shift+U is the menu's; Esc stops the voice and nothing else; any other key stops it and goes on
    if ((k->modifiers() & Qt::ControlModifier) && (k->modifiers() & Qt::ShiftModifier) && k->key() == Qt::Key_U) return false;
    const bool esc = k->key() == Qt::Key_Escape;
    stop(esc);
    return esc;
}

} // namespace neosea

#include "moc_readaloud.cpp"
