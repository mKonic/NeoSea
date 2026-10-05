#include "core/storage.h"

#include "core/i18n.h"
#include "core/json.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <zip.h>

#include <fcntl.h>
#include <unistd.h>

namespace neosea {

namespace {

QString base36(quint64 n)
{
    return QString::number(n, 36);
}

QByteArray readAll(const QString &file, bool *ok = nullptr)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return f.readAll();
}

std::optional<QJsonValue> parseJson(const QString &file)
{
    bool ok = false;
    const QByteArray data = readAll(file, &ok);
    if (!ok) return std::nullopt;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || doc.isNull()) return std::nullopt;
    if (doc.isArray()) return QJsonValue(doc.array());
    return QJsonValue(doc.object());
}

bool writePlain(const QString &file, const QByteArray &data)
{
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

} // namespace

QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newId(const QString &prefix)
{
    QString rnd;
    for (int i = 0; i < 4; ++i) rnd += base36(QRandomGenerator::global()->bounded(36));
    return prefix + base36(QDateTime::currentMSecsSinceEpoch()) + '-' + rnd;
}

bool isLibName(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) return false;
    for (QChar c : name)
        if (c == '/' || c == '\\' || c == QChar(0)) return false;
    return true;
}

QString slugOf(const QString &title, int max)
{
    QString s = title.normalized(QString::NormalizationForm_D);
    QString out;
    bool dash = false;
    for (QChar c : s) {
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_SpacingCombining
            || c.category() == QChar::Mark_Enclosing)
            continue;
        const QChar l = c.toLower();
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
            if (dash && !out.isEmpty()) out += '-';
            dash = false;
            out += l;
        } else {
            dash = true;
        }
    }
    out = out.left(max);
    while (out.endsWith('-')) out.chop(1);
    return out;
}

bool writeDurable(const QString &file, const QByteArray &data, QString *error)
{
    const QByteArray tmp = QFile::encodeName(file + QStringLiteral(".tmp"));
    const int fd = ::open(tmp.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (error) *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (error) *error = QString::fromLocal8Bit(strerror(errno));
            ::close(fd);
            return false;
        }
        done += n;
    }
    // without the push, a power cut right after the swap can leave the swap
    // done and the words not
    if (::fsync(fd) != 0) {
        if (error) *error = QString::fromLocal8Bit(strerror(errno));
        ::close(fd);
        return false;
    }
    ::close(fd);
    if (::rename(tmp.constData(), QFile::encodeName(file).constData()) != 0) {
        if (error) *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    // the swap itself
    const int dfd = ::open(QFile::encodeName(QFileInfo(file).absolutePath()).constData(), O_RDONLY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
}

std::optional<QJsonValue> readJsonFile(const QString &file, QString *recoveredFrom)
{
    if (auto v = parseJson(file)) return v;
    if (!QFile::exists(file) && !QFile::exists(file + QStringLiteral(".bak"))) return std::nullopt;
    for (const QString &spare : {file + QStringLiteral(".tmp"), file + QStringLiteral(".bak")}) {
        auto v = parseJson(spare);
        if (!v) continue;
        if (recoveredFrom) *recoveredFrom = QFileInfo(spare).fileName();
        writeDurable(file, toJson(*v));
        return v;
    }
    return std::nullopt;
}

bool writeJsonFile(const QString &file, const QJsonValue &v, QString *error)
{
    // the version on disk, while it reads whole, becomes the .bak
    if (parseJson(file)) {
        QFile::remove(file + QStringLiteral(".bak"));
        QFile::copy(file, file + QStringLiteral(".bak"));
    }
    return writeDurable(file, toJson(v), error);
}

// ---------------------------------------------------------------------------

Library::Library(QString dir) : m_dir(std::move(dir)) {}

QString Library::libraryFile() const { return QDir(m_dir).filePath(QStringLiteral("library.json")); }

QString Library::bookDir(const QString &bookId) const
{
    if (!isLibName(bookId)) return {};
    return QDir(m_dir).filePath(bookId);
}

void Library::ensure(const QString &firstShelfName) const
{
    QDir().mkpath(m_dir);
    if (QFile::exists(libraryFile())) return;
    QJsonObject seed{
        {"authorName", ""},
        {"penNames", QJsonArray{}},
        {"firstRunDone", false},
        {"pageTheme", "night"},
        {"shelves", QJsonArray{QJsonObject{{"id", "shelf-1"}, {"name", firstShelfName}, {"bookIds", QJsonArray{}}}}},
    };
    writePlain(libraryFile(), toJson(seed));
}

QJsonObject Library::readLibrary(const QString &firstShelfName) const
{
    ensure(firstShelfName);
    QString from;
    if (auto v = readJsonFile(libraryFile(), &from); v && v->isObject()) {
        if (!from.isEmpty()) logError("recovered", libraryFile() + " was unreadable; restored from " + from);
        return v->toObject();
    }
    // lost with no copy to fall back on: every book in the folder goes onto
    // one shelf, so nothing disappears
    QJsonArray ids;
    for (const QString &d : QDir(m_dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (d.startsWith(QLatin1String("book-")) && QFileInfo::exists(QDir(m_dir).filePath(d + "/chapters")))
            ids.append(d);
    QJsonObject seed{
        {"authorName", ""},
        {"penNames", QJsonArray{}},
        {"firstRunDone", !ids.isEmpty()},
        {"pageTheme", "night"},
        {"shelves", QJsonArray{QJsonObject{{"id", "shelf-1"}, {"name", firstShelfName}, {"bookIds", ids}}}},
    };
    logError("recovered", QStringLiteral("library.json was lost; %1 books put back on one shelf").arg(ids.size()));
    writeJsonFile(libraryFile(), seed);
    return seed;
}

bool Library::writeLibrary(const QJsonObject &lib) const
{
    QDir().mkpath(m_dir);
    QString err;
    if (!writeJsonFile(libraryFile(), lib, &err)) {
        logError("library write", err);
        return false;
    }
    writeCatalog();
    return true;
}

void Library::writeCatalog() const
{
    const QJsonObject lib = readJsonFile(libraryFile()).value_or(QJsonObject{}).toObject();
    QHash<QString, QString> onShelf;
    for (const auto &sv : lib.value("shelves").toArray()) {
        const QJsonObject s = sv.toObject();
        for (const auto &id : s.value("bookIds").toArray()) onShelf.insert(id.toString(), s.value("name").toString());
    }
    QStringList lines;
    for (const QString &d : QDir(m_dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!d.startsWith(QLatin1String("book-"))) continue;
        auto m = parseJson(QDir(m_dir).filePath(d + "/book.json"));
        if (!m || !m->isObject()) continue;
        const QJsonObject o = m->toObject();
        const QString title = o.value("title").toString();
        const QString shelf = onShelf.value(o.value("id").toString());
        lines << QStringLiteral("%1  —  %2  —  %3 %4")
                     .arg(title.isEmpty() ? t("Untitled") : title, d, t("shelf:"),
                          shelf.isEmpty() ? t("(none — removed from shelves)") : shelf);
    }
    std::sort(lines.begin(), lines.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    const QString text = t("NEO LIBRARY CATALOG — which folder is which book") + '\n'
        + t("(regenerated automatically; edits here do nothing)") + "\n\n" + lines.join('\n') + '\n';
    writePlain(QDir(m_dir).filePath("_catalog.txt"), text.toUtf8());
}

QJsonObject Library::createBook(const QString &title, const QString &author) const
{
    QDir().mkpath(m_dir);
    // folders carry a slug of the title when it's known at creation (imports),
    // so the library reads like a bookshelf in a file manager too
    const QString slug = slugOf(title);
    QString rnd;
    for (int i = 0; i < 5; ++i) rnd += base36(QRandomGenerator::global()->bounded(36));
    const QString id = QStringLiteral("book-") + (slug.isEmpty() ? QString() : slug + '-')
        + base36(QDateTime::currentMSecsSinceEpoch()) + '-' + rnd;
    const QString dir = bookDir(id);
    QDir().mkpath(dir + "/chapters");
    const QString now = isoNow();
    QJsonObject book{
        {"id", id},
        {"title", title.isEmpty() ? t("Untitled") : title},
        {"subtitle", ""},
        {"series", ""},
        {"author", author.isEmpty() ? t("Anonymous") : author},
        {"wordGoal", 0},
        {"created", now},
        {"modified", now},
        {"chapterOrder", QJsonArray{}},
        {"tabNames", QJsonObject{{"notes", "Notes"}, {"outline", "Outline"}}},
    };
    writeJsonFile(dir + "/book.json", book);
    writePlain(dir + "/notes.html", {});
    writePlain(dir + "/outline.html", {});
    writeJsonFile(dir + "/darlings.json", QJsonArray{});
    writeJsonFile(dir + "/stickies.json", QJsonArray{});
    return book;
}

QList<BookListing> Library::listBooks() const
{
    QList<BookListing> out;
    for (const QString &d : QDir(m_dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!d.startsWith(QLatin1String("book-"))) continue;
        auto m = readJsonFile(QDir(m_dir).filePath(d + "/book.json"));
        if (!m || !m->isObject()) continue;
        const QJsonObject o = m->toObject();
        if (o.value("id").toString().isEmpty()) continue;
        QString title = o.value("title").toString();
        out.push_back({o.value("id").toString(), title.isEmpty() ? t("Untitled") : title,
                       o.value("author").toString(), o.value("modified").toString(), o.value("kind").toString()});
    }
    return out;
}

std::optional<QJsonObject> Library::readBookMeta(const QString &bookId) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty()) return std::nullopt;
    QString from;
    if (auto v = readJsonFile(dir + "/book.json", &from); v && v->isObject()) {
        if (!from.isEmpty()) logError("recovered", dir + "/book.json was unreadable; restored from " + from);
        return v->toObject();
    }
    return rebuildBookMeta(bookId);
}

QString Library::writeBookMeta(const QString &bookId, QJsonObject meta) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty()) return {};
    const QString now = isoNow();
    meta.insert("modified", now);
    QString err;
    if (!writeJsonFile(dir + "/book.json", meta, &err)) {
        logError("book write", err);
        return {};
    }
    writeCatalog();
    return now;
}

std::optional<QJsonObject> Library::rebuildBookMeta(const QString &bookId) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !QFileInfo::exists(dir + "/chapters")) return std::nullopt;
    QString title;
    const QString cat = QString::fromUtf8(readAll(QDir(m_dir).filePath("_catalog.txt")));
    for (const QString &line : cat.split('\n'))
        if (line.contains(QStringLiteral("  —  ") + bookId + QStringLiteral("  —  "))) {
            title = line.section(QStringLiteral("  —  "), 0, 0).trimmed();
            break;
        }
    QStringList order;
    for (const QString &f : QDir(dir + "/chapters").entryList({"*.html"}, QDir::Files, QDir::Name))
        order << f.chopped(5);
    const QString now = isoNow();
    QJsonObject meta{
        {"id", bookId},
        {"title", title.isEmpty() ? t("Untitled") : title},
        {"subtitle", ""},
        {"series", ""},
        {"author", t("Anonymous")},
        {"wordGoal", 0},
        {"created", now},
        {"modified", now},
        {"chapterOrder", QJsonArray::fromStringList(order)},
        {"tabNames", QJsonObject{{"notes", "Notes"}, {"outline", "Outline"}}},
    };
    logError("recovered", QStringLiteral("%1/book.json was lost; rebuilt from %2 chapter files").arg(bookId).arg(order.size()));
    writeJsonFile(dir + "/book.json", meta);
    return meta;
}

QHash<QString, QString> Library::chapterStamps(const QString &bookId) const
{
    QHash<QString, QString> out;
    const QString dir = bookDir(bookId);
    if (dir.isEmpty()) return out;
    const QDir chapters(dir + "/chapters");
    for (const QFileInfo &fi : chapters.entryInfoList({"*.html"}, QDir::Files)) {
        // the size too: sync tools hand over the other device's mtime, and on
        // disks that keep whole seconds two saves a second apart look the same
        out.insert(fi.completeBaseName(),
                   QString::number(fi.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(fi.size()));
    }
    return out;
}

QString Library::readChapter(const QString &bookId, const QString &chapterId) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(chapterId)) return {};
    return QString::fromUtf8(readAll(dir + "/chapters/" + chapterId + ".html"));
}

bool Library::writeChapter(const QString &bookId, const QString &chapterId, const QString &html) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(chapterId)) return false;
    QDir().mkpath(dir + "/chapters");
    QString err;
    if (!writeDurable(dir + "/chapters/" + chapterId + ".html", html.toUtf8(), &err)) {
        logError("chapter write", err);
        return false;
    }
    return true;
}

bool Library::deleteChapter(const QString &bookId, const QString &chapterId) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(chapterId)) return false;
    QFile::remove(dir + "/chapters/" + chapterId + ".html");
    return true;
}

QString Library::readAux(const QString &bookId, const QString &name) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(name)) return {};
    return QString::fromUtf8(readAll(dir + '/' + name + ".html"));
}

bool Library::writeAux(const QString &bookId, const QString &name, const QString &html) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(name)) return false;
    return writeDurable(dir + '/' + name + ".html", html.toUtf8());
}

QJsonValue Library::readJson(const QString &bookId, const QString &name, const QJsonValue &fallback) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(name)) return fallback;
    return readJsonFile(dir + '/' + name + ".json").value_or(fallback);
}

bool Library::writeJson(const QString &bookId, const QString &name, const QJsonValue &v) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !isLibName(name)) return false;
    return writeJsonFile(dir + '/' + name + ".json", v);
}

bool Library::trashBook(const QString &bookId, QString *error) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !QFileInfo::exists(dir)) return false;
    // words are never lost: no trash on this filesystem leaves the folder alone
    if (!QFile::moveToTrash(dir)) {
        logError("trash", "could not move " + dir + " to the trash");
        if (error) *error = dir;
        return false;
    }
    return true;
}

namespace {
const QStringList kCoverExts{"png", "jpg", "jpeg", "webp"};
const QRegularExpression kCoverName(QStringLiteral("^cover-\\d+\\."));
} // namespace

QString Library::setCover(const QString &bookId, const QString &srcPath) const
{
    QString ext = QFileInfo(srcPath).suffix().toLower();
    if (!kCoverExts.contains(ext)) return {};
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !QFileInfo::exists(dir)) return {};
    removeCover(bookId);
    if (ext == QLatin1String("jpeg")) ext = "jpg";
    const QString fname = "cover-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + '.' + ext;
    if (!QFile::copy(srcPath, dir + '/' + fname)) return {};
    return fname;
}

void Library::removeCover(const QString &bookId) const
{
    const QString dir = bookDir(bookId);
    if (dir.isEmpty()) return;
    for (const QString &f : QDir(dir).entryList(QDir::Files))
        if (kCoverName.match(f).hasMatch()) QFile::remove(dir + '/' + f);
}

QString Library::coverPath(const QString &bookId, const QString &fname) const
{
    static const QRegularExpression re(QStringLiteral("^(cover|art)-\\d+\\.(png|jpg|webp)$"));
    const QString dir = bookDir(bookId);
    if (dir.isEmpty() || !re.match(fname).hasMatch()) return {};
    return dir + '/' + fname;
}

void Library::logError(const QString &source, const QString &message) const
{
    const QString line = QStringLiteral("[%1] [%2] %3\n").arg(isoNow(), source, message);
    QDir().mkpath(m_dir);
    QFile f(QDir(m_dir).filePath("neo-errors.log"));
    if (f.open(QIODevice::Append)) f.write(line.toUtf8());
}

QString Library::dailyBackup(const QString &todayIn) const
{
    const QString today = todayIn.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : todayIn;
    const QString backups = QDir(m_dir).filePath("Backups");
    QDir().mkpath(backups);
    const QString target = backups + "/neo-backup-" + today + ".zip";
    if (QFileInfo::exists(target)) return {};

    int zerr = 0;
    const QString tmp = target + ".tmp";
    QFile::remove(tmp);
    zip_t *z = zip_open(QFile::encodeName(tmp).constData(), ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!z) {
        logError("backup", "could not create " + tmp);
        return {};
    }
    // One file the system won't hand over (a sync tool holding it, a cloud
    // stand-in not downloaded) is left out and named, never the whole day's backup.
    QStringList missed;
    std::function<void(const QString &, const QString &)> walk = [&](const QString &dir, const QString &rel) {
        const QDir d(dir);
        if (!d.exists()) { missed << (rel.isEmpty() ? "." : rel); return; }
        for (const QFileInfo &fi : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            const QString name = fi.fileName();
            if (rel.isEmpty() && (name == "Backups" || name == "Exports")) continue;
            if (name == ".DS_Store" || (name.startsWith('.') && name.endsWith(".icloud"))) continue;
            const QString relPath = rel.isEmpty() ? name : rel + '/' + name;
            if (fi.isDir()) {
                walk(fi.filePath(), relPath);
                continue;
            }
            bool ok = false;
            const QByteArray data = readAll(fi.filePath(), &ok);
            if (!ok) { missed << relPath; continue; }
            void *copy = malloc(std::max<qsizetype>(1, data.size()));
            memcpy(copy, data.constData(), data.size());
            zip_source_t *src = zip_source_buffer(z, copy, data.size(), 1);
            if (!src || zip_file_add(z, relPath.toUtf8().constData(), src, ZIP_FL_ENC_UTF_8) < 0) {
                if (src) zip_source_free(src);
                missed << relPath;
            }
        }
    };
    walk(m_dir, QString());
    if (!missed.isEmpty()) {
        const QByteArray note = (missed.join('\n') + '\n').toUtf8();
        void *copy = malloc(note.size());
        memcpy(copy, note.constData(), note.size());
        zip_source_t *src = zip_source_buffer(z, copy, note.size(), 1);
        if (src && zip_file_add(z, "_left-out-of-this-backup.txt", src, ZIP_FL_ENC_UTF_8) < 0) zip_source_free(src);
        logError("backup", "left out of today's backup: " + missed.join(", "));
    }
    if (zip_close(z) != 0) {
        logError("backup", QString::fromUtf8(zip_strerror(z)));
        zip_discard(z);
        QFile::remove(tmp);
        return {};
    }
    QFile::rename(tmp, target);

    QStringList all = QDir(backups).entryList({"neo-backup-*.zip"}, QDir::Files, QDir::Name);
    while (all.size() > 14) QFile::remove(backups + '/' + all.takeFirst());
    return target;
}

} // namespace neosea
