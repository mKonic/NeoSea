#include "app/auxpage.h"

#include "core/darlings.h"

#include "app/app.h"
#include "app/editorview.h"
#include "app/outlineboard.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/chapter.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/textdoc.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QVBoxLayout>

namespace neosea {

namespace {

// paper, on the dark ground, as the manuscript's sheets are
class Paper : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRectF r = QRectF(rect()).adjusted(18, 4, -18, -30);
        for (int i = 10; i > 0; --i) p.fillRect(r.adjusted(-i, -i + 4, i, i + 4), QColor(0, 0, 0, int(10 + 3 * (10 - i)) / 2));
        p.fillRect(r, theme().paper);
    }
};

QScrollArea *paperScroll(QWidget *inside, int width)
{
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *room = new QWidget;
    room->setObjectName("room");
    auto *row = new QHBoxLayout(room);
    row->setContentsMargins(0, 40, 0, 120);
    row->addStretch();
    inside->setFixedWidth(width);
    row->addWidget(inside, 0, Qt::AlignTop);
    row->addStretch();
    scroll->setWidget(room);
    return scroll;
}

QLabel *pageTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    l->setAlignment(Qt::AlignCenter);
    QFont f = l->font();
    f.setPixelSize(14);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    l->setFont(f);
    l->setStyleSheet("color: #777777;");
    return l;
}

} // namespace

AuxPage::AuxPage(EditorView *view) : QWidget(view), m_view(view)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack);

    // Notes
    auto *notesPaper = new Paper;
    auto *nl = new QVBoxLayout(notesPaper);
    nl->setContentsMargins(90, 74, 90, 120);
    m_notesTitle = pageTitle(t("Notes"), notesPaper);
    nl->addWidget(m_notesTitle);
    nl->addSpacing(40);
    m_notes = new QTextEdit(notesPaper);
    m_notes->setFrameShape(QFrame::NoFrame);
    m_notes->setPlaceholderText(t("Write freely…"));
    m_notes->setStyleSheet("QTextEdit { background: transparent; }");
    m_notes->setMinimumHeight(500);
    connect(m_notes->document(), &QTextDocument::contentsChanged, this, [this] { m_notesDirty = true; });
    nl->addWidget(m_notes, 1);
    notesPaper->setMinimumHeight(700);
    m_notesScroll = paperScroll(notesPaper, 720);
    m_stack->addWidget(m_notesScroll);

    // Outline
    m_outline = new OutlineBoard(view);
    m_stack->addWidget(m_outline);

    // Darlings
    auto *dPaper = new Paper;
    auto *dl = new QVBoxLayout(dPaper);
    dl->setContentsMargins(90, 74, 90, 120);
    dl->addWidget(pageTitle(t("Darlings"), dPaper));
    dl->addSpacing(40);
    m_darlingsList = new QWidget(dPaper);
    m_darlings = new QVBoxLayout(m_darlingsList);
    m_darlings->setContentsMargins(0, 0, 0, 0);
    m_darlings->setSpacing(18);
    dl->addWidget(m_darlingsList);
    dl->addStretch();
    dPaper->setMinimumHeight(700);
    m_darlingsScroll = paperScroll(dPaper, 720);
    m_stack->addWidget(m_darlingsScroll);
}

void AuxPage::show(const QString &tab)
{
    flush();
    m_tab = tab;
    BookSession *s = m_view->app()->session();
    if (!s) return;
    if (tab == "notes") {
        const QString n = s->book().value("tabNames").toObject().value("notes").toString("Notes");
        m_notesTitle->setText((n == "Notes" ? t("Notes") : n).toUpper());
        const QJsonObject fonts = m_view->app()->library().value("fonts").toObject();
        QFont f(bodyFontFamily(fonts.value("body").toString()));
        f.setPixelSize(16);
        m_notes->document()->setDefaultFont(f);
        QPalette pal = m_notes->palette();
        pal.setColor(QPalette::Text, theme().ink);
        m_notes->setPalette(pal);
        doc::loadHtml(*m_notes->document(), m_view->app()->lib().readAux(s->id(), "notes"));
        m_notesDirty = false;
        m_stack->setCurrentWidget(m_notesScroll);
        m_notes->setFocus();
    } else if (tab == "outline") {
        m_outline->render();
        m_stack->setCurrentWidget(m_outline);
    } else if (tab == "darlings") {
        buildDarlings();
        m_stack->setCurrentWidget(m_darlingsScroll);
    }
}

void AuxPage::leave()
{
    flush();
    m_tab.clear();
}

void AuxPage::flush()
{
    BookSession *s = m_view->app()->session();
    if (!s || !m_notesDirty) return;
    m_view->app()->lib().writeAux(s->id(), "notes", doc::html(*m_notes->document()));
    m_notesDirty = false;
}

void AuxPage::refresh()
{
    if (m_tab == "darlings") buildDarlings();
    if (m_tab == "outline") m_outline->render();
}

QWidget *AuxPage::looseCards(EditorView *view, QWidget *parent) { return OutlineBoard::looseCards(view, parent); }

void AuxPage::buildDarlings()
{
    while (QLayoutItem *item = m_darlings->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    BookSession *s = m_view->app()->session();
    if (!s) return;
    if (s->darlings().isEmpty()) {
        auto *empty = new QLabel(t("When a beautiful paragraph is gumming up the works, select it and drag it onto the Darlings tab below.") + "\n"
                                     + t("It leaves your manuscript but it is never lost."),
                                 m_darlingsList);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color: %1; font-style: italic; padding: 60px 0;").arg(theme().name == "night" ? "#5f5b52" : "#b9b4a8"));
        m_darlings->addWidget(empty);
        return;
    }
    for (const auto &v : s->darlings()) {
        const QJsonObject d = v.toObject();
        auto *card = new QFrame(m_darlingsList);
        card->setObjectName("darling");
        card->setStyleSheet(QStringLiteral("#darling { border: 1px solid %1; border-left: 3px solid %2; border-radius: 4px; } QLabel { color: %3; }")
                                .arg(theme().name == "night" ? "#3a3835" : "#e3ddcf", theme().accent.name(), theme().ink.name()));
        auto *col = new QVBoxLayout(card);
        col->setContentsMargins(18, 16, 18, 14);
        auto *content = new QLabel(card);
        content->setWordWrap(true);
        content->setTextFormat(Qt::RichText);
        content->setText(d.value("html").toString().isEmpty() ? escHtml(d.value("text").toString()) : d.value("html").toString());
        QFont f("Gelasio");
        f.setPixelSize(15);
        content->setFont(f);
        col->addWidget(content);
        auto *meta = new QHBoxLayout;
        const QString when = QLocale(I18n::locale()).toString(QDateTime::fromString(d.value("date").toString(), Qt::ISODateWithMs).date(), QLocale::ShortFormat);
        auto *info = new QLabel(t("from {label} · {date} · {n} words", {{"label", d.value("chapterLabel").toString()}, {"date", when}, {"n", countWords(d.value("text").toString())}}), card);
        info->setStyleSheet("color: #999999; font-size: 11px;");
        auto *restore = new QPushButton(t("Restore"), card);
        auto *del = new QPushButton(t("Delete forever"), card);
        for (QPushButton *b : {restore, del}) b->setStyleSheet("QPushButton { border: 1px solid #cccccc; border-radius: 4px; font-size: 11px; padding: 3px 10px; color: #777777; } QPushButton:hover { border-color: #c9a86a; }");
        meta->addWidget(info, 1);
        meta->addWidget(restore);
        meta->addWidget(del);
        col->addLayout(meta);
        const QString id = d.value("id").toString();
        connect(restore, &QPushButton::clicked, this, [this, id] { restoreDarling(id); });
        connect(del, &QPushButton::clicked, this, [this, id] {
            BookSession *s = m_view->app()->session();
            if (!s) return;
            m_view->snapshot("darling delete", false);
            QJsonArray kept;
            for (const auto &x : s->darlings())
                if (x.toObject().value("id").toString() != id) kept << x;
            s->darlings() = kept;
            s->saveDarlings();
            buildDarlings();
        });
        m_darlings->addWidget(card);
    }
}

void AuxPage::restoreDarling(const QString &id)
{
    BookSession *s = m_view->app()->session();
    if (!s) return;
    QJsonObject d;
    for (const auto &x : s->darlings())
        if (x.toObject().value("id").toString() == id) d = x.toObject();
    if (d.isEmpty()) return;
    m_view->snapshot("darling restore", false);
    m_view->showTab("manuscript");
    QString chId = d.value("chapterId").toString();
    if (!s->order().contains(chId)) chId = s->order().isEmpty() ? QString() : s->order().last();
    if (chId.isEmpty()) chId = s->createChapterAt(0), m_view->rebuildChapters();
    ChapterEdit *e = m_view->editorFor(chId);
    if (!e) return;
    // back where it was cut, found by the words around the cut
    QTextDocument *docu = e->document();
    const int at = darlings::findPosition(darlings::bodyPlain(*docu), d.value("anchorPrefix").toString(), d.value("anchorSuffix").toString());
    const int pos = at >= 0 ? darlings::toDocPos(*docu, at) : -1;
    const bool found = pos >= 0;
    QTextCursor c(docu);
    c.setPosition(found ? pos : docu->characterCount() - 1);
    const QList<Para> paras = parseChapter(d.value("html").toString().isEmpty() ? escHtml(d.value("text").toString()) : d.value("html").toString());
    const bool block = d.value("html").toString().contains("<p");
    c.beginEditBlock();
    if (block && !found) c.insertBlock();
    for (qsizetype i = 0; i < paras.size(); ++i) {
        if (i > 0 || (block && found && c.positionInBlock() > 0)) c.insertBlock();
        for (const Run &r : paras[i].runs) {
            QTextCharFormat f;
            if (r.mark) f = doc::markFormat(r.sid);
            else {
                f.setFontWeight(r.b ? QFont::Bold : QFont::Normal);
                f.setFontItalic(r.i);
                f.setFontUnderline(r.u);
                f.setFontStrikeOut(r.s);
            }
            c.insertText(r.text, f);
        }
    }
    c.endEditBlock();
    e->setFocus();
    e->setTextCursor(c);
    e->ensureCursorVisible();
    QJsonArray kept;
    for (const auto &x : s->darlings())
        if (x.toObject().value("id").toString() != id) kept << x;
    s->darlings() = kept;
    s->saveDarlings();
    emit m_view->app()->toast(found ? t("Darling restored to its original spot") : t("Darling restored at the end of its chapter"));
}

} // namespace neosea

#include "moc_auxpage.cpp"
