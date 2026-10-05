#pragma once
// Two ways of seeing the page while writing. Focus mode fades everything
// but the paragraph or the sentence the caret is in (Ctrl+Shift+O steps
// off → paragraph → sentence). Typewriter scrolling keeps the line being
// written near the middle of the window, but only while the writer types or
// moves by keyboard: a click to think about a sentence leaves the screen be.

#include <QObject>
#include <QPointer>
#include <QTimer>

class QPropertyAnimation;

namespace neosea {

class ChapterEdit;
class EditorView;

class WritingModes : public QObject {
    Q_OBJECT
public:
    explicit WritingModes(EditorView *view);
    void watch(ChapterEdit *e);
    QString focusLevel() const { return m_focus; }
    void setFocusLevel(const QString &level);
    void cycleFocus();
    bool typewriter() const { return m_typewriter; }
    void toggleTypewriter();
    void refresh(); // the chapters were drawn again

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void updateFocus();
    void follow(ChapterEdit *e);
    void typewriterRoom();

    EditorView *m_view;
    QString m_focus = "off";
    bool m_typewriter = false;
    bool m_byKeyboard = false;
    QPointer<ChapterEdit> m_last;
    QTimer m_soon;
    QPropertyAnimation *m_glide = nullptr;
};

} // namespace neosea
