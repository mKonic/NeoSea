#pragma once
// Find & replace: a bar at the top of the writing room. Find searches the tab
// that's open (the whole manuscript, first chapter to last, or the Notes);
// Replace stays with the manuscript, where Ctrl+Z takes a Replace All back.
// Matches are highlighted, not selected; Enter walks through them.

#include "core/search.h"

#include <QFrame>
#include <QPointer>
#include <QTimer>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace neosea {

class EditorView;

class SearchBar : public QFrame {
    Q_OBJECT
public:
    explicit SearchBar(EditorView *view);
    void open();
    void close();
    void watch(QTextEdit *e); // a page whose edits move the matches
    void place();             // centred at the top of the room
    void retab();             // another tab: Replace only on the manuscript, and find again
    QString query() const;    // what was last looked for (vim's n and N)
    void openFromVim();       // /: Esc goes back to moving, at the match
signals:
    void returnedToPage(bool fromVim);
public:

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Hit {
        QPointer<QTextEdit> ed;
        search::Match m;
    };
    QList<QTextEdit *> roots() const;
    void run();
    void runSoon() { m_debounce.start(); }
    void freshIfStale();
    void gotoMatch(int i);
    void paint();
    void replaceCurrent();
    void replaceAll();
    void returnToPage();
    void reveal(QTextEdit *ed, int pos);

    EditorView *m_view;
    QLineEdit *m_find, *m_replace;
    QLabel *m_count;
    QPushButton *m_one, *m_all;
    QList<Hit> m_hits;
    int m_idx = -1;
    QString m_query, m_tab;
    QPointer<QTextEdit> m_homeEd;
    int m_homePos = 0;
    QTimer m_debounce, m_rerun;
    bool m_replacing = false;
    bool m_fromVim = false;
};

} // namespace neosea
