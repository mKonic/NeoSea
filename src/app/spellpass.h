#pragma once
// The spellcheck pass (Ctrl+;): squiggles under the words the dictionary
// doesn't know, and under capitals it can't see (a sentence that starts
// small, English "i"). A page is checked when the caret comes to it and again
// a moment after it's edited. Right-click a flagged word for suggestions.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QTextEdit;
class QContextMenuEvent;

namespace neosea {

class EditorView;

class SpellPass : public QObject {
    Q_OBJECT
public:
    explicit SpellPass(EditorView *view);
    bool isOn() const { return m_on; }
    void toggle();
    void watch(QTextEdit *e);   // a page that can be checked
    void here(QTextEdit *e);    // the caret came to this page
    void recheckAll();          // the dictionary changed
    // a right-click on a flagged word: the menu; false when it isn't one
    bool menu(QTextEdit *e, QContextMenuEvent *ev);

private:
    void scan(QTextEdit *e);
    void clear(QTextEdit *e);
    bool correct(const QString &word);

    EditorView *m_view;
    bool m_on = false;
    QSet<QTextEdit *> m_scanned;
    QHash<QTextEdit *, QSet<int>> m_caps; // positions of capital slips, per page
    QHash<QTextEdit *, QTimer *> m_rescan;
    QHash<QString, bool> m_cache; // word → correct?
};

} // namespace neosea
