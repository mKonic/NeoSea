#pragma once
// The Outline: the book laid out as index cards, set like a page (left to
// right, line after line), or as a list. Every chapter and every *** section
// is a card; a section's note stands in the manuscript as a grey ghost until
// the section is written. Dragging a card moves the writing with it; dropping
// a chapter on the middle of another makes it that chapter's section.

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QTextEdit;
class QLabel;

namespace neosea {

class EditorView;
class CardView;

class OutlineBoard : public QWidget {
    Q_OBJECT
public:
    explicit OutlineBoard(EditorView *view);
    void render();
    void stepZoom(int dir); // ⌘− / ⌘+ on the cards
    bool showingCards() const;
    static QWidget *looseCards(EditorView *view, QWidget *parent);

    EditorView *view() const { return m_view; }
    // the structure changed: the manuscript and the board follow
    void changed();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void renderList();
    void setMode(const QString &mode);

    EditorView *m_view;
    QWidget *m_switch;
    QPushButton *m_listBtn, *m_cardsBtn;
    QScrollArea *m_scroll;
    CardView *m_cards;
    QWidget *m_list;
    QVBoxLayout *m_lines;
    QLabel *m_hint;
};

} // namespace neosea
