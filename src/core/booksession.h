#pragma once
// The open book: its book.json, every chapter's HTML, the stickies and
// darlings, and what was last read from or written to disk. That knowledge is
// what lets the app write only what changed (a library shared over a sync
// tool must not be rewritten every twenty seconds) and, in refresh(), tell
// another device's edits from its own:
//
//   - a chapter unchanged here but changed on disk is adopted; when the disk
//     copy only has fewer words, the page's text goes to Darlings first
//   - a chapter changed in both places keeps the page's text and gets the
//     disk's version as the next chapter, titled as from the other device
//   - an empty read never wipes a chapter that has words
//   - a new book.json from the other device never drops a chapter holding
//     words that aren't saved here
//
// Not last-write-wins: the cases that look redundant are each a way words
// were once lost.

#include "core/storage.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <optional>

namespace neosea {

// book.json minus what every device changes constantly, and minus empty
// defaults: the same book either way
QString metaSig(const QJsonObject &m);
// The disk copy has no word the page lacks, but the page has words it lacks:
// an older copy, not an edit made somewhere else
bool onlyDrops(const QString &pageHtml, const QString &diskHtml);

struct RefreshResult {
    bool restructured = false;
    int adopted = 0;
    int conflicts = 0;
    int displaced = 0; // page text that went to Darlings
    bool changed() const { return restructured || adopted || conflicts; }
};

class BookSession {
public:
    BookSession(const Library &lib, QString bookId);

    bool load(); // false when the book can't be read
    const QString &id() const { return m_id; }
    const Library &library() const { return m_lib; }

    QJsonObject &book() { return m_book; }
    const QJsonObject &book() const { return m_book; }
    QStringList order() const;
    void setOrder(const QStringList &order);

    QString chapterHtml(const QString &chId) const { return m_html.value(chId); }
    QHash<QString, QString> allChapters() const { return m_html; }
    // what the page now holds for a chapter (the editor calls this as it types)
    void setChapterHtml(const QString &chId, const QString &html);
    bool chapterDirty(const QString &chId) const;

    // One door for chapter writes. Returns false (and leaves the chapter
    // marked unsaved, so the next flush retries) when the disk refused.
    bool persistChapter(const QString &chId);
    bool persistChapter(const QString &chId, const QString &html);
    // book.json: stamps modified, remembers the signature
    bool saveMeta();
    bool metaDirty() const;
    // every unsaved chapter, then book.json if it changed
    void flush();

    QJsonArray &stickies() { return m_stickies; }
    QJsonArray &darlings() { return m_darlings; }
    void saveStickies();
    void saveDarlings();

    // a new, empty chapter at index, saved; returns its id
    QString createChapterAt(qsizetype index, const QString &html = QStringLiteral("<p><br></p>"));
    // gone from the book and the disk, with its notes, kind and stickies
    void deleteChapter(const QString &chId);

    // Look at the disk. Chapters the editor is showing are taken as the page.
    RefreshResult refresh(const QString &otherDeviceTitleSuffix);

    // structural undo: snapshots of the whole structure, ten deep
    struct Snapshot {
        QString label;
        bool rejoin = false;
        QStringList order;
        QJsonObject kinds, titles, notes, sectionNotes, sceneNotes;
        QJsonArray looseCards, darlings, stickies;
        QHash<QString, QString> html;
        QJsonObject caret; // where the caret was, for the editor to put back
    };
    void snapshot(const QString &label, bool rejoin = false, const QJsonObject &caret = {});
    bool canUndoStructure() const { return !m_undo.isEmpty(); }
    // restores the newest snapshot and writes it all back; returns it
    std::optional<Snapshot> undoStructure();
    void clearUndo() { m_undo.clear(); }

private:
    const Library &m_lib;
    QString m_id;
    QJsonObject m_book;
    QHash<QString, QString> m_html;   // chapterId -> html as the page holds it
    QHash<QString, QString> m_saved;  // chapterId -> html as last read or written
    QHash<QString, QString> m_stamps; // chapterId -> "mtime:size" as last looked at
    QString m_savedSig;
    QJsonArray m_stickies, m_darlings;
    QList<Snapshot> m_undo;
};

} // namespace neosea
