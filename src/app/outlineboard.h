#pragma once
// The Outline: the book laid out as index cards, set like a page, or as a
// list. Cards come from the manuscript itself: every chapter and every ***
// section is a card; a note written on a section's card stands in the
// manuscript as a grey ghost paragraph until the section is written.

#include <QWidget>

class QScrollArea;
class QVBoxLayout;

namespace neosea {

class EditorView;

class OutlineBoard : public QWidget {
    Q_OBJECT
public:
    explicit OutlineBoard(EditorView *view);
    void render();
    static QWidget *looseCards(EditorView *view, QWidget *parent);

private:
    void renderList();

    EditorView *m_view;
    QScrollArea *m_scroll;
    QWidget *m_content;
    QVBoxLayout *m_lines;
};

} // namespace neosea
