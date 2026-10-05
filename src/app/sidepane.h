#pragma once
// The notes pane: slides out at the right margin. The placeholders' sticky
// notes, each a card; Enter in a note goes back to the page just past its ⚑.
// While the Outline is up, it holds the loose cards instead.

#include <QFrame>

class QPropertyAnimation;
class QPushButton;
class QVBoxLayout;
class QLabel;

namespace neosea {

class EditorView;

class SidePane : public QFrame {
    Q_OBJECT
public:
    explicit SidePane(EditorView *view);
    bool isOpen() const { return m_open; }
    bool pinned() const { return m_pinned; }
    bool busy() const;
    void open();
    void close();
    void rebuild();
    void focusSticky(const QString &sid, bool autoOpen = false);
    void setOutlineMode(bool on);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void slide(bool open);
    void returnToMark(const QString &sid);

    EditorView *m_view;
    QWidget *m_list;
    QVBoxLayout *m_items;
    QLabel *m_title;
    QPushButton *m_pin;
    QPropertyAnimation *m_anim;
    bool m_open = false, m_pinned = false, m_autoOpened = false, m_outline = false;
};

} // namespace neosea
