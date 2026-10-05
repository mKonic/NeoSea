#include "app/outlineboard.h"

#include "app/app.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/i18n.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace neosea {

OutlineBoard::OutlineBoard(EditorView *view) : QWidget(view), m_view(view)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_content = new QWidget;
    m_content->setObjectName("room");
    auto *outer = new QVBoxLayout(m_content);
    outer->setContentsMargins(60, 40, 60, 90);
    m_lines = new QVBoxLayout;
    m_lines->setSpacing(6);
    outer->addLayout(m_lines);
    outer->addStretch();
    m_scroll->setWidget(m_content);
    lay->addWidget(m_scroll);
}

void OutlineBoard::render() { renderList(); }

void OutlineBoard::renderList()
{
    while (QLayoutItem *item = m_lines->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    BookSession *s = m_view->app()->session();
    if (!s) return;
    const QJsonObject notes = s->book().value("chapterNotes").toObject();
    for (const QString &chId : s->order()) {
        auto *row = new QWidget(m_content);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 8, 0, 0);
        auto *num = new QLabel(chapterMark(chId, s->book()), row);
        num->setMinimumWidth(28);
        num->setAlignment(Qt::AlignRight | Qt::AlignTop);
        num->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(theme().chapterHead.name()));
        auto *text = new QLineEdit(notes.value(chId).toString(), row);
        text->setFrame(false);
        text->setPlaceholderText(t("What happens in this chapter…"));
        text->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; border-bottom: 1px dashed transparent; } QLineEdit:focus { border-bottom-color: #4a4742; }")
                                .arg(theme().text.name()));
        connect(text, &QLineEdit::editingFinished, this, [this, chId, text] {
            BookSession *s = m_view->app()->session();
            if (!s) return;
            QJsonObject n = s->book().value("chapterNotes").toObject();
            if (n.value(chId).toString() == text->text()) return;
            n.insert(chId, text->text());
            s->book().insert("chapterNotes", n);
            s->saveMeta();
        });
        h->addWidget(num);
        h->addSpacing(12);
        h->addWidget(text, 1);
        m_lines->addWidget(row);
    }
}

QWidget *OutlineBoard::looseCards(EditorView *, QWidget *) { return nullptr; }

} // namespace neosea

#include "moc_outlineboard.cpp"
