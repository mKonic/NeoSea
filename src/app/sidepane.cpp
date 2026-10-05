#include "app/sidepane.h"

#include "app/app.h"
#include "app/auxpage.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/i18n.h"
#include "core/textdoc.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBlock>
#include <QVBoxLayout>

namespace neosea {

namespace {

// a sticky note: Enter goes back to the page, ⇧Enter is another line
class StickyEdit : public QPlainTextEdit {
public:
    std::function<void()> onReturn;
    std::function<void()> onChanged;
    explicit StickyEdit(QWidget *parent) : QPlainTextEdit(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setPlaceholderText(t("What needs doing here?"));
        connect(document(), &QTextDocument::contentsChanged, this, [this] {
            const int h = int(document()->size().height() * fontMetrics().lineSpacing()) + 14;
            setFixedHeight(std::clamp(h, 44, 260));
            if (onChanged) onChanged();
        });
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
            if (onReturn) onReturn();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
};

} // namespace

SidePane::SidePane(EditorView *view) : QFrame(view), m_view(view)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 40, 0, 20);
    col->setSpacing(10);
    auto *head = new QHBoxLayout;
    head->setContentsMargins(18, 0, 16, 6);
    m_title = new QLabel(t("Notes & Comments").toUpper(), this);
    m_title->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; letter-spacing: 2px;").arg(theme().muted.name()));
    m_pin = new QPushButton(QStringLiteral("☉"), this);
    m_pin->setToolTip(t("Keep this pane open"));
    m_pin->setCheckable(true);
    m_pin->setStyleSheet(QStringLiteral("QPushButton { color: #555555; font-size: 15px; padding: 0 4px; } QPushButton:checked { color: %1; }")
                             .arg(theme().accent.name()));
    connect(m_pin, &QPushButton::toggled, this, [this](bool on) { m_pinned = on; });
    head->addWidget(m_title);
    head->addStretch();
    head->addWidget(m_pin);
    col->addLayout(head);
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list = new QWidget;
    m_list->setStyleSheet("background: transparent;");
    m_items = new QVBoxLayout(m_list);
    m_items->setContentsMargins(14, 0, 14, 0);
    m_items->setSpacing(12);
    m_items->addStretch();
    scroll->setWidget(m_list);
    scroll->viewport()->setAutoFillBackground(false);
    col->addWidget(scroll, 1);
    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

bool SidePane::busy() const
{
    for (QWidget *w : findChildren<QWidget *>())
        if (w->hasFocus()) return true;
    return false;
}

void SidePane::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), theme().pane);
    p.setPen(QColor("#2c2c2c"));
    p.drawLine(rect().topLeft(), rect().bottomLeft());
}

void SidePane::slide(bool open)
{
    if (m_open == open) return;
    m_open = open;
    m_anim->stop();
    m_anim->setStartValue(pos());
    const int w = parentWidget()->width();
    m_anim->setEndValue(QPoint(open ? w - width() : w, 0));
    m_anim->start();
}

void SidePane::open() { slide(true); }
void SidePane::close() { slide(false); }

void SidePane::setOutlineMode(bool on)
{
    m_outline = on;
    m_title->setText((on ? t("Loose cards") : t("Notes & Comments")).toUpper());
    rebuild();
}

void SidePane::rebuild()
{
    while (m_items->count() > 1) {
        QLayoutItem *item = m_items->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    BookSession *s = m_view->app()->session();
    if (!s) return;
    if (m_outline) {
        if (QWidget *w = AuxPage::looseCards(m_view, m_list)) m_items->insertWidget(0, w);
        return;
    }
    int row = 0;
    for (const auto &v : s->stickies()) {
        const QJsonObject st = v.toObject();
        if (st.value("resolved").toBool()) continue;
        const QString sid = st.value("id").toString();
        const QString chId = st.value("chapterId").toString();
        auto *card = new QFrame(m_list);
        card->setObjectName("sticky");
        card->setStyleSheet("#sticky { background: #f6e3b8; border-radius: 3px; } QLabel { color: #7a6538; font-size: 11px; } "
                            "QPlainTextEdit { background: transparent; color: #1c1c1c; font-size: 13px; } "
                            "QPushButton { color: #7a6538; font-size: 11px; padding: 0 2px; } QPushButton:hover { color: #1c1c1c; }");
        card->setProperty("sid", sid);
        auto *col = new QVBoxLayout(card);
        col->setContentsMargins(10, 8, 10, 8);
        col->setSpacing(4);
        auto *top = new QHBoxLayout;
        auto *where = new QLabel(s->order().contains(chId) ? chapterName(chId, s->book()) : t("Unplaced"), card);
        auto *go = new QPushButton(t("Go to"), card);
        auto *done = new QPushButton(t("Resolve"), card);
        top->addWidget(where, 1);
        top->addWidget(go);
        top->addWidget(done);
        col->addLayout(top);
        auto *edit = new StickyEdit(card);
        edit->setPlainText(st.value("text").toString());
        edit->onChanged = [this, sid, edit] {
            BookSession *s = m_view->app()->session();
            if (!s) return;
            QJsonArray all = s->stickies();
            for (int i = 0; i < all.size(); ++i) {
                QJsonObject o = all[i].toObject();
                if (o.value("id").toString() != sid) continue;
                o.insert("text", edit->toPlainText());
                all[i] = o;
            }
            s->stickies() = all;
            s->saveStickies();
        };
        edit->onReturn = [this, sid] { returnToMark(sid); };
        col->addWidget(edit);
        connect(go, &QPushButton::clicked, this, [this, sid] { returnToMark(sid); });
        connect(done, &QPushButton::clicked, this, [this, sid] { m_view->markResolved(sid); });
        m_items->insertWidget(row++, card);
    }
    if (!row) {
        auto *empty = new QLabel(t("No notes yet.") + "\n\n" + t("Hit {key} while writing to drop a placeholder — a “come back to this” mark that never breaks your flow.", {{"key", "Ctrl+Shift+X"}}), m_list);
        empty->setWordWrap(true);
        empty->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-style: italic;").arg(theme().muted.name()));
        m_items->insertWidget(0, empty);
    }
}

void SidePane::focusSticky(const QString &sid, bool autoOpen)
{
    m_autoOpened = autoOpen && !m_open;
    open();
    for (QFrame *card : m_list->findChildren<QFrame *>("sticky"))
        if (card->property("sid").toString() == sid)
            if (auto *e = card->findChild<QPlainTextEdit *>()) {
                e->setFocus();
                e->moveCursor(QTextCursor::End);
            }
}

void SidePane::returnToMark(const QString &sid)
{
    // back to the manuscript, caret just past the flag
    if (m_view->tab() != "manuscript") m_view->showTab("manuscript");
    BookSession *s = m_view->app()->session();
    if (!s) return;
    for (const QString &chId : s->order()) {
        ChapterEdit *e = m_view->editorFor(chId);
        if (!e) continue;
        for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().property(doc::MarkSid).toString() == sid) {
                    QTextCursor c(e->document());
                    c.setPosition(std::min(it.fragment().position() + 2, b.position() + b.length() - 1));
                    e->setFocus();
                    e->setTextCursor(c);
                    e->ensureCursorVisible();
                    if (m_autoOpened) close();
                    m_autoOpened = false;
                    return;
                }
    }
}

} // namespace neosea

#include "moc_sidepane.cpp"
