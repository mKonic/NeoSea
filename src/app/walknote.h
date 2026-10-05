#pragma once
// The walking note: while the caret is in a section written from an outline
// card, the card's note rides along under the paragraph being written, small
// and grey, until it's dismissed (the card keeps it).

#include <QObject>
#include <QPointer>
#include <QTimer>

class QLabel;
class QPushButton;

namespace neosea {

class ChapterEdit;
class EditorView;

class WalkNote : public QObject {
    Q_OBJECT
public:
    explicit WalkNote(EditorView *view);
    void watch(ChapterEdit *e); // a new chapter editor on the page
    void queue();
    void hide();

private:
    void update();

    EditorView *m_view;
    QTimer m_timer;
    QPointer<ChapterEdit> m_edit;
    QPointer<QWidget> m_box;
    QLabel *m_text = nullptr;
    QString m_ch, m_sec;
};

} // namespace neosea
