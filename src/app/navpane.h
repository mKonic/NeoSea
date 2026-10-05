#pragma once
// The chapters pane: slides out when the pointer reaches the left margin.
// Every chapter, its words, a red dot where a placeholder waits, and a line
// to note what happens in it. Drag to reorder; right-click for what it is.

#include <QFrame>
#include <QTimer>

class QListWidget;
class QPropertyAnimation;
class QPushButton;

namespace neosea {

class EditorView;

class NavPane : public QFrame {
    Q_OBJECT
public:
    explicit NavPane(EditorView *view);
    bool isOpen() const { return m_open; }
    bool pinned() const { return m_pinned; }
    bool busy() const;
    void open();
    void close();
    void rebuild();
    void highlight(const QString &chId);
    void scheduleRefresh() { m_refresh.start(400); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void slide(bool open);
    void applyOrder();

    EditorView *m_view;
    QListWidget *m_list;
    QPushButton *m_pin;
    QPropertyAnimation *m_anim;
    QTimer m_refresh;
    bool m_open = false, m_pinned = false, m_dropping = false;
};

} // namespace neosea
