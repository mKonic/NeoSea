#pragma once
// The running app's state: the library on disk, library.json in memory, the
// book metadata the shelf shows, and the one book open in the editor. Every
// library.json write goes through writeLibrary(), so a look at the disk can
// tell this window's writes from another device's.

#include "core/booksession.h"
#include "core/shelves.h"
#include "core/spell.h"
#include "core/storage.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <memory>

namespace neosea {

class App : public QObject {
    Q_OBJECT
public:
    explicit App(const QString &libraryDir, QObject *parent = nullptr);

    Library &lib() { return m_lib; }
    QJsonObject &library() { return m_library; }
    void reloadLibrary();
    void writeLibrary();
    // library.json changed underneath (another device): adopt it, unless a
    // write of ours is newer
    bool refreshLibrary();

    // what a book's tile shows; cached until the book is written
    std::optional<QJsonObject> meta(const QString &bookId);
    void writeMeta(const QString &bookId, QJsonObject meta);
    void forgetMeta(const QString &bookId) { m_metaCache.remove(bookId); }
    void forgetAllMeta() { m_metaCache.clear(); }
    shelves::KindOf kindOf();

    QString anonymous() const;
    QString authorId();
    QString authorName();
    QList<QJsonObject> myShelves();

    // a new book (or script) on a shelf; returns its id
    QString createBook(const QString &shelfId, bool script = false);
    QString createPageBook(const QString &shelfId, const QString &kind);

    struct ImportOutcome {
        int books = 0, scripts = 0;
        QStringList errors; // "name: why"
    };
    // manuscripts (.docx .txt .md) and scripts (.fountain .fdx) onto a shelf
    ImportOutcome importFiles(const QStringList &paths, const QString &shelfId);

    BookSession *session() { return m_session.get(); }
    bool openBook(const QString &bookId);
    void closeBook();

    // writing language: the spellcheck dictionary picked, else the interface's
    QString writingLanguage() const;

    // the spellcheck dictionary: the one picked in Edit → Spellcheck Language,
    // else the interface's. It loads off the main thread; until then every
    // word is fine.
    spell::Speller &speller() { return *m_speller; }
    QString spellLanguage() const;
    void loadSpeller();
    void setSpellLanguage(const QString &code);
    void learnWord(const QString &word);

signals:
    void libraryChanged();
    void toast(const QString &message, int ms = 4000);
    void bookOpened();
    void bookClosed();
    void spellerChanged();

private:
    Library m_lib;
    QJsonObject m_library;
    QHash<QString, QJsonObject> m_metaCache;
    std::unique_ptr<BookSession> m_session;
    int m_generation = 0;
    std::shared_ptr<spell::Speller> m_speller = std::make_shared<spell::Speller>();
};

} // namespace neosea
