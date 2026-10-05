#pragma once
// The other tabs, on the same paper: Notes (the writer's scratch paper),
// the Outline (index cards, or a list), and Darlings (cut words, kept).

#include <QWidget>

class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QVBoxLayout;
class QLabel;

namespace neosea {

class EditorView;
class OutlineBoard;

class AuxPage : public QWidget {
    Q_OBJECT
public:
    explicit AuxPage(EditorView *view);
    void show(const QString &tab);
    void leave();
    void flush();
    void refresh();
    OutlineBoard *outline() const { return m_outline; }
    // the loose cards the side pane holds while the Outline is up
    static QWidget *looseCards(EditorView *view, QWidget *parent);

    // Darlings: words dropped on the tab, kept with where they came from
    void addDarling(const QString &chId, const QString &html, const QString &text, const QString &before, const QString &after);

private:
    void buildDarlings();
    void restoreDarling(const QString &id);

    EditorView *m_view;
    QString m_tab;
    QStackedWidget *m_stack;
    QScrollArea *m_notesScroll;
    QTextEdit *m_notes;
    QLabel *m_notesTitle;
    QScrollArea *m_darlingsScroll;
    QWidget *m_darlingsList;
    QVBoxLayout *m_darlings;
    OutlineBoard *m_outline;
    bool m_notesDirty = false;
};

} // namespace neosea
