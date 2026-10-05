#pragma once
// Read aloud (Ctrl+Shift+U): the computer's own voice reads from the caret,
// a sentence at a time, each one lit as it's read, on into the chapters
// after. Ctrl+Shift+U again, Esc or a keystroke stops it, and the caret is
// left at the sentence it reached, so Ctrl+Shift+U carries on from there.

#include <QList>
#include <QObject>
#include <QPointer>

class QTextEdit;
class QTextToSpeech;

namespace neosea {

class EditorView;

class ReadAloud : public QObject {
    Q_OBJECT
public:
    explicit ReadAloud(EditorView *view);
    bool reading() const { return m_on; }
    void toggle();
    void stop(bool leaveCaret);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Item {
        QPointer<QTextEdit> page;
        int block = 0, a = 0, b = 0;
    };
    void next();
    void light(const Item &it);

    EditorView *m_view;
    QTextToSpeech *m_voice = nullptr;
    QList<Item> m_queue;
    Item m_now;
    bool m_on = false;
    int m_gen = 0;
};

} // namespace neosea
