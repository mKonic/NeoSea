#pragma once
// The writing room: the book as sheets of paper on a dark ground, a title page
// then a sheet per chapter, each chapter its own page of type. The chapters
// pane slides out from the left edge, the notes pane from the right; the
// bottom bar holds the tabs and the counters, faint until the pointer nears.

#include "app/chapteredit.h"

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QStackedWidget;

namespace neosea {

class App;
class ChapterSheet;
class NavPane;
class SidePane;
class AuxPage;
class TitleSheet;
class WalkNote;
class SearchBar;
class SpellPass;
class WritingModes;

class EditorView : public QWidget, public ChapterHost {
    Q_OBJECT
public:
    explicit EditorView(App *app, QWidget *parent = nullptr);

    void openSession(); // the app's session is the book to show
    void closeSession();
    void flush(bool exact = false); // every unsaved word to disk, and where the caret is
    void applyLook();   // fonts, sizes, page theme
    QString currentChapter() const { return m_current; }
    ChapterEdit *editorFor(const QString &chId) const;
    void focusChapter(const QString &chId, bool atEnd = false);
    void revealCaret(const QString &chId); // the page scrolls to the chapter's caret
    void gotoChapter(int step);
    void showTab(const QString &tab);
    QString tab() const { return m_tab; }
    void refreshFromDisk();

    // the structure changed in the session: draw the chapters again
    void rebuildChapters();
    void syncAll(); // every editor's text into the session

    // ChapterHost
    bool isStoryChapter(const QString &chId) const override;
    bool isScriptBook() const override;
    edit::TypingContext typingContext(ChapterEdit *e) const override;
    typing::QuoteStyle bookQuoteStyle(ChapterEdit *e) const override;
    void snapshot(const QString &label, bool rejoin) override;
    void splitChapter(ChapterEdit *e, int blockNumber) override;
    void removeEmptyChapter(ChapterEdit *e) override;
    void backspaceAtStart(ChapterEdit *e) override;
    bool structuralUndo() override;
    void markResolved(const QString &sid) override;
    void chapterEdited(ChapterEdit *e) override;
    void chapterFocused(ChapterEdit *e) override;
    bool scriptKey(ChapterEdit *e, QKeyEvent *k) override;
    bool spellMenu(ChapterEdit *e, QContextMenuEvent *ev) override;
    void toggleSpellcheck();

    // used by the panes and the menus
    App *app() const { return m_app; }
    AuxPage *auxPage() const { return m_aux; }
    QScrollArea *scrollArea() const { return m_scroll; }
    WritingModes *modes() const { return m_modes; }
    Q_INVOKABLE void openSearch();
    void selectionToDarlings(ChapterEdit *e); // the drag onto the tab, and Ctrl+Shift+D
    void darlingFromKeyboard();
    void newChapterAfter(const QString &chId);
    void deleteChapterToDarlings(const QString &chId);
    void chapterMenu(const QString &chId, QPoint globalPos);
    void insertPlaceholder();
    void scheduleCounters();
    void openSidePane();
    void refreshSidePane(); // deferred: the pane's own widgets may be asking
    void refreshOutline();
    bool zoomCards(int dir); // the menu's text size steps the cards while they're up
    QJsonObject captureCaret() const;
    void restoreCaret(const QJsonObject &caret);

signals:
    void backToShelf();
    void exportChapter(const QString &chId);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void layoutOverlays();
    void updateCounters();
    void saveChapterSoon(const QString &chId);
    int bookWords();
    int chapterWords(const QString &chId);
    void trackScroll();
    void deleteChapterQuiet(const QString &chId);
    void rejoinAtCaret();
    // scroll to a chapter's sheet once the page's height has caught up
    void scrollToSheet(const QString &chId);
    bool applyPendingScroll();
    QString m_pendingSheet;

    App *m_app;
    QWidget *m_room;          // the dark ground
    QScrollArea *m_scroll;
    QWidget *m_column;
    QVBoxLayout *m_sheets;
    TitleSheet *m_title = nullptr;
    QList<ChapterSheet *> m_chapters;
    QStackedWidget *m_stack;  // manuscript, or a tab's own page
    AuxPage *m_aux = nullptr;
    WalkNote *m_walk;
    SearchBar *m_search;
    SpellPass *m_spell;
    WritingModes *m_modes;
    NavPane *m_nav = nullptr;
    SidePane *m_side = nullptr;
    QWidget *m_bar;
    QHash<QString, QPushButton *> m_tabs;
    QLabel *m_goalCounter, *m_posCounter, *m_wordCounter;
    QString m_tab = "manuscript";
    QString m_current;
    QString m_wordMode = "book";
    QHash<QString, QTimer *> m_saveTimers;
    QHash<QString, int> m_wordCache;
    QTimer m_counterTimer, m_flushTimer, m_refreshTimer, m_scrollTimer;
    bool m_rebuilding = false;
};

} // namespace neosea
