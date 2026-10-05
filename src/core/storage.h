#pragma once
// The library on disk: a folder of plain files the writer can inspect, sync
// and back up, in NEO's layout so either app opens the same library.
//
//   <library>/
//     library.json          shelves, authors, settings that travel with the books
//     _catalog.txt          regenerated map of folder -> title (edits ignored)
//     neo-errors.log
//     Backups/neo-backup-YYYY-MM-DD.zip   one a day, 14 kept
//     Exports/              emailed PDF snapshots
//     book-<slug>-<id>/
//       book.json           metadata and chapterOrder
//       chapters/<id>.html
//       notes.html outline.html darlings.json stickies.json
//       cover-<ts>.<ext>    the writer's own cover image
//
// Every library write goes through writeDurable (file.tmp, fsync, rename), so a
// power cut never leaves an empty file. JSON writes keep the last version that
// read whole as file.bak, and reads fall back on .tmp then .bak. Every name a
// caller passes is one plain path segment (libName): ".", ".." and separators
// never reach the disk.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace neosea {

// A JS-style timestamp: 2026-10-05T12:34:56.789Z
QString isoNow();
// Date.now().toString(36) + a few random base-36 letters, NEO's id shape
QString newId(const QString &prefix);
// A plain library name: non-empty, not "." or "..", no / \ or NUL
bool isLibName(const QString &name);
// Lower-case ASCII slug of a title, accents taken off first ("Capítulo" -> "capitulo")
QString slugOf(const QString &title, int max = 30);

// Writes file.tmp, pushes it to the disk, renames it over file, then syncs the
// folder. Returns false (and leaves the old file alone) on any failure.
bool writeDurable(const QString &file, const QByteArray &data, QString *error = nullptr);
// JSON reads that recover from .tmp, then .bak, putting what they find back
std::optional<QJsonValue> readJsonFile(const QString &file, QString *recoveredFrom = nullptr);
bool writeJsonFile(const QString &file, const QJsonValue &v, QString *error = nullptr);

struct BookListing {
    QString id, title, author, modified, kind;
};

class Library {
public:
    explicit Library(QString dir);

    const QString &dir() const { return m_dir; }
    QString libraryFile() const;
    QString bookDir(const QString &bookId) const; // empty when the id isn't a plain name

    // Creates the folder and a seed library.json when either is missing.
    void ensure(const QString &firstShelfName) const;
    // library.json; lost with no copy, every book folder goes onto one shelf
    QJsonObject readLibrary(const QString &firstShelfName) const;
    bool writeLibrary(const QJsonObject &lib) const;
    void writeCatalog() const;

    QJsonObject createBook(const QString &title, const QString &author) const;
    QList<BookListing> listBooks() const;
    std::optional<QJsonObject> readBookMeta(const QString &bookId) const;
    // stamps "modified" and returns it; empty on failure
    QString writeBookMeta(const QString &bookId, QJsonObject meta) const;
    // book.json gone for good: rebuilt from the chapter files
    std::optional<QJsonObject> rebuildBookMeta(const QString &bookId) const;

    // {chapterId: "mtimeMs:size"}: how a look at the disk tells what changed
    QHash<QString, QString> chapterStamps(const QString &bookId) const;
    QString readChapter(const QString &bookId, const QString &chapterId) const;
    bool writeChapter(const QString &bookId, const QString &chapterId, const QString &html) const;
    bool deleteChapter(const QString &bookId, const QString &chapterId) const;

    QString readAux(const QString &bookId, const QString &name) const;          // name.html
    bool writeAux(const QString &bookId, const QString &name, const QString &html) const;
    QJsonValue readJson(const QString &bookId, const QString &name, const QJsonValue &fallback) const;
    bool writeJson(const QString &bookId, const QString &name, const QJsonValue &v) const;

    // To the system trash; false (folder untouched) when there is none.
    bool trashBook(const QString &bookId, QString *error = nullptr) const;

    // Cover images: cover-<ts>.<ext> inside the book folder
    QString setCover(const QString &bookId, const QString &srcPath) const; // new file name or empty
    void removeCover(const QString &bookId) const;
    QString coverPath(const QString &bookId, const QString &fname) const;   // empty if not a cover name

    void logError(const QString &source, const QString &message) const;
    // One zip of the whole library per day, the last 14 kept. Returns the
    // zip's path when one was written today by this call.
    QString dailyBackup(const QString &today = {}) const;

private:
    QString m_dir;
};

} // namespace neosea
