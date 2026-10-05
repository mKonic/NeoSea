#include "app/walknote.h"

#include "app/app.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/i18n.h"
#include "core/outline.h"
#include "core/pagelayout.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBlock>

namespace neosea {

WalkNote::WalkNote(EditorView *view) : QObject(view), m_view(view)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &WalkNote::update);
}

void WalkNote::watch(ChapterEdit *e)
{
    connect(e, &QTextEdit::cursorPositionChanged, this, &WalkNote::queue);
    connect(e, &QTextEdit::textChanged, this, &WalkNote::queue);
}

void WalkNote::queue() { m_timer.start(); }

void WalkNote::hide()
{
    if (m_edit) m_edit->pageLayout()->setSpaceAfter(-1, 0);
    if (m_box) m_box->deleteLater();
    m_box = nullptr;
    m_text = nullptr;
    m_edit = nullptr;
    m_ch.clear();
    m_sec.clear();
}

void WalkNote::update()
{
    BookSession *s = m_view->app()->session();
    QWidget *focus = QApplication::focusWidget();
    auto *e = qobject_cast<ChapterEdit *>(focus);
    if (!focus && m_edit && m_edit->isVisible()) e = m_edit; // the window is away
    if (!s || !e || m_view->tab() != "manuscript" || isScript(s->book())) return hide();
    const QTextBlock block = e->textCursor().block();
    // the page as paragraphs, as far as sections go: classes and note ids
    QList<Para> paras;
    for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next()) {
        Para p;
        for (const QString &c : doc::classes(b)) p.addClass(c);
        const QString sec = doc::attr(b, "data-sec-id");
        if (!sec.isEmpty()) p.setAttr("data-sec-id", sec);
        paras << p;
    }
    const outline::Board board(*s);
    const QList<outline::Note> notes = board.notes(e->chId());
    const QString id = outline::sectionIdAt(paras, notes, block.blockNumber());
    outline::Note note;
    for (const auto &n : notes)
        if (n.id == id) note = n;
    if (id.isEmpty() || note.text.trimmed().isEmpty() || note.dismissed) return hide();

    if (!m_box || m_edit != e || m_sec != id) {
        hide();
        m_edit = e;
        m_ch = e->chId();
        m_sec = id;
        m_box = new QWidget(e->viewport());
        auto *row = new QHBoxLayout(m_box);
        row->setContentsMargins(0, 2, 0, 0);
        row->setSpacing(10);
        m_text = new QLabel(m_box);
        m_text->setWordWrap(true);
        m_text->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *dismiss = new QPushButton(t("Dismiss"), m_box);
        dismiss->setToolTip(t("Put this outline note away for this section (it stays on its card)"));
        dismiss->setFocusPolicy(Qt::NoFocus); // the caret stays where it is
        dismiss->setCursor(Qt::PointingHandCursor);
        const QColor paper = e->palette().color(QPalette::Base), ink = e->palette().color(QPalette::Text);
        auto mix = [&](double k) {
            return QColor::fromRgbF(ink.redF() * k + paper.redF() * (1 - k), ink.greenF() * k + paper.greenF() * (1 - k), ink.blueF() * k + paper.blueF() * (1 - k));
        };
        m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(mix(0.38).name()));
        dismiss->setStyleSheet(QStringLiteral("QPushButton { background: none; border: none; padding: 0; font-size: 11px; color: %1; } QPushButton:hover { color: %2; }")
                                   .arg(mix(0.3).name(), theme().accent.name()));
        row->addWidget(m_text, 1, Qt::AlignTop);
        row->addWidget(dismiss, 0, Qt::AlignTop);
        connect(dismiss, &QPushButton::clicked, this, [this] {
            if (BookSession *s = m_view->app()->session()) outline::Board(*s).dismissNote(m_ch, m_sec);
            hide();
        });
        m_box->show();
    }
    const PageStyle &st = e->pageLayout()->style();
    QFont f = e->document()->defaultFont();
    f.setItalic(true);
    f.setPixelSize(std::max(9, int(st.fontPx * 0.82)));
    m_text->setFont(f);
    m_text->setText(note.text);
    // the note sits two ems in, under the paragraph, which makes room for it
    const int left = int(2 * st.fontPx);
    const int width = e->viewport()->width() - left;
    m_box->setFixedWidth(width);
    const int h = m_box->layout()->heightForWidth(width);
    m_box->setFixedHeight(h);
    PageLayout *layout = e->pageLayout();
    layout->setSpaceAfter(block.blockNumber(), h + 8);
    const QRectF r = layout->blockBoundingRect(block);
    m_box->move(left, int(r.bottom() - layout->spaceAfter() + 2));
    m_box->raise();
}

} // namespace neosea

#include "moc_walknote.cpp"
