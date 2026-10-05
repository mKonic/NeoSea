#include "app/navpane.h"

#include "app/app.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/chapter.h"
#include "core/i18n.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>

namespace neosea {

namespace {

class NavItem : public QWidget {
public:
    QString chId;
    QLabel *label, *words, *flag;
    QLineEdit *note;

    NavItem(const QString &id, QWidget *parent) : QWidget(parent), chId(id)
    {
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(18, 8, 16, 9);
        col->setSpacing(4);
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        label = new QLabel(this);
        label->setStyleSheet("color: #aaaaaa; font-size: 13px;");
        words = new QLabel(this);
        words->setStyleSheet("color: #5d5d5d; font-size: 11px;");
        flag = new QLabel(this);
        flag->setFixedSize(8, 8);
        flag->setStyleSheet("background: #c0392b; border-radius: 4px;");
        row->addWidget(label, 1);
        row->addWidget(flag);
        row->addWidget(words);
        col->addLayout(row);
        note = new QLineEdit(this);
        note->setFrame(false);
        note->setPlaceholderText(t("What happens here…"));
        note->setStyleSheet("QLineEdit { background: transparent; color: #777777; font-size: 12px; border: none; padding: 0; }"
                            "QLineEdit:focus { color: #cccccc; }");
        col->addWidget(note);
    }
};

} // namespace

NavPane::NavPane(EditorView *view) : QFrame(view), m_view(view)
{
    setAutoFillBackground(false);
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 40, 0, 20);
    col->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->setContentsMargins(18, 0, 16, 6);
    auto *title = new QLabel(t("Chapters").toUpper(), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; letter-spacing: 2px;").arg(theme().muted.name()));
    m_pin = new QPushButton(QStringLiteral("☉"), this);
    m_pin->setToolTip(t("Keep this pane open"));
    m_pin->setCheckable(true);
    m_pin->setStyleSheet(QStringLiteral("QPushButton { color: #555555; font-size: 15px; padding: 0 4px; } QPushButton:checked { color: %1; }")
                             .arg(theme().accent.name()));
    connect(m_pin, &QPushButton::toggled, this, [this](bool on) { m_pinned = on; });
    head->addWidget(title);
    head->addStretch();
    head->addWidget(m_pin);
    col->addLayout(head);
    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; outline: none; } QListWidget::item { border-left: 3px solid transparent; } "
                                         "QListWidget::item:hover { background: #282828; } QListWidget::item:selected { background: transparent; border-left-color: %1; }")
                              .arg(theme().accent.name()));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](QPoint p) {
        if (QListWidgetItem *it = m_list->itemAt(p)) m_view->chapterMenu(it->data(Qt::UserRole).toString(), m_list->viewport()->mapToGlobal(p));
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { m_view->focusChapter(it->data(Qt::UserRole).toString()); });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] { QTimer::singleShot(0, this, &NavPane::applyOrder); });
    col->addWidget(m_list, 1);
    auto *add = new QPushButton(QStringLiteral("+ ") + t("Chapter"), this);
    add->setToolTip(t("Add a chapter at the end"));
    add->setStyleSheet(QStringLiteral("QPushButton { border: 1px dashed #3a3a3a; border-radius: 6px; color: #666666; padding: 8px; margin: 8px 18px 0 18px; font-size: 12px; } "
                                      "QPushButton:hover { color: %1; border-color: %1; }")
                           .arg(theme().accent.name()));
    connect(add, &QPushButton::clicked, this, [this] { m_view->newChapterAfter({}); });
    col->addWidget(add);
    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_refresh.setSingleShot(true);
    connect(&m_refresh, &QTimer::timeout, this, &NavPane::rebuild);
}

bool NavPane::busy() const
{
    if (QApplication::mouseButtons() != Qt::NoButton && underMouse()) return true; // a drag in progress
    for (QLineEdit *e : findChildren<QLineEdit *>())
        if (e->hasFocus()) return true;
    return false;
}

void NavPane::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), theme().pane);
    p.setPen(QColor("#2c2c2c"));
    p.drawLine(rect().topRight(), rect().bottomRight());
}

void NavPane::slide(bool open)
{
    if (m_open == open) return;
    m_open = open;
    m_anim->stop();
    m_anim->setStartValue(pos());
    m_anim->setEndValue(QPoint(open ? 0 : -width(), 0));
    m_anim->start();
}

void NavPane::open() { slide(true); }
void NavPane::close() { slide(false); }

void NavPane::rebuild()
{
    App *app = m_view->app();
    BookSession *s = app->session();
    m_list->clear();
    if (!s) return;
    QSet<QString> flagged;
    for (const auto &v : s->stickies())
        if (!v.toObject().value("resolved").toBool()) flagged.insert(v.toObject().value("chapterId").toString());
    const QJsonObject notes = s->book().value("chapterNotes").toObject();
    for (const QString &chId : s->order()) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, chId);
        auto *w = new NavItem(chId, m_list);
        const QString title = chapterTitle(chId, s->book());
        const QString kind = chapterKind(chId, s->book());
        w->label->setText(kind == "unnumbered" ? title : title.isEmpty() ? chapterName(chId, s->book()) : chapterName(chId, s->book()) + " · " + title);
        ChapterEdit *e = m_view->editorFor(chId);
        const int n = countWords(e ? e->document()->toPlainText() : chapterPlainText(s->chapterHtml(chId)));
        w->words->setText(I18n::formatNumber(n));
        w->flag->setVisible(flagged.contains(chId));
        w->note->setText(notes.value(chId).toString());
        connect(w->note, &QLineEdit::editingFinished, this, [this, chId, w] {
            BookSession *s = m_view->app()->session();
            if (!s) return;
            QJsonObject n = s->book().value("chapterNotes").toObject();
            if (n.value(chId).toString() == w->note->text()) return;
            n.insert(chId, w->note->text());
            s->book().insert("chapterNotes", n);
            s->saveMeta();
        });
        item->setSizeHint(w->sizeHint());
        m_list->setItemWidget(item, w);
    }
    highlight(m_view->currentChapter());
}

void NavPane::highlight(const QString &chId)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == chId) {
            m_list->setCurrentRow(i);
            return;
        }
    m_list->clearSelection();
}

void NavPane::applyOrder()
{
    BookSession *s = m_view->app()->session();
    if (!s) return;
    QStringList order;
    for (int i = 0; i < m_list->count(); ++i) order << m_list->item(i)->data(Qt::UserRole).toString();
    if (order == s->order()) return;
    m_view->snapshot("chapter moved", false);
    s->setOrder(order);
    s->saveMeta();
    m_view->rebuildChapters();
}

} // namespace neosea

#include "moc_navpane.cpp"
