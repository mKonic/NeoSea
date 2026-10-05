#pragma once
// Vim keys (View → Vim Keys, off unless chosen): the small part of vim that
// writers use to move around a page. Esc puts the page in moving mode (the
// caret turns gold), and a book opens in it; letters then move instead of
// type, and i, a, o and friends go back to writing. Keys with Ctrl keep
// their usual jobs (but Ctrl-d and Ctrl-u, half a screen), so none of the
// app's shortcuts change.
//
//   h j k l   left, down, up, right      w b e   by word
//   0 ^ $     start / end of the line    ( )     by sentence
//   { }       by paragraph               gg G    top / end of chapter
//   [[ ]]     previous / next chapter    Ctrl-d Ctrl-u  half a screen
//   i a I A   write here / after / at the start / at the end of line
//   o O       new paragraph below / above
//   v         select: motions stretch it, y copies, d or x cuts
//   x         delete the letter under the caret
//   /         find (Esc goes back to moving, at the match)
//   n N       next / previous match    a number first repeats: 3w

#include <QObject>
#include <QPointer>
#include <QTextCursor>

class QKeyEvent;
class QTextEdit;

namespace neosea {

class EditorView;

class VimKeys : public QObject {
    Q_OBJECT
public:
    explicit VimKeys(EditorView *view);
    bool enabled() const { return m_enabled; }
    void toggle();
    void watch(QTextEdit *e);
    void rest(); // a book opens, or the page comes back: moving, as vim starts

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void setNav(bool on, QTextEdit *e = nullptr);
    void key(QTextEdit *e, QKeyEvent *k);
    bool move(QTextEdit *e, QTextCursor::MoveOperation op, int times = 1);
    void line(QTextEdit *e, bool down, bool paragraph);
    void word(QTextEdit *e, QChar k);
    void sentence(QTextEdit *e, bool forward);
    void halfPage(QTextEdit *e, int dir);
    void searchAgain(QTextEdit *e, int dir, int times);
    void inclusive(QTextEdit *e);
    void paint();

    EditorView *m_view;
    bool m_enabled = false;
    bool m_nav = false;
    bool m_visual = false;
    QString m_count, m_pending;
    QPointer<QTextEdit> m_page;
};

} // namespace neosea
