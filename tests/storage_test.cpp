#include "core/storage.h"
#include "core/json.h"

#include "print.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace neosea;

namespace {

QByteArray slurp(const QString &f)
{
    QFile file(f);
    (void)file.open(QIODevice::ReadOnly);
    return file.readAll();
}

void spit(const QString &f, const QByteArray &data)
{
    QFile file(f);
    (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(data);
}

} // namespace

TEST(Json, WritesLikeJavaScript)
{
    QJsonObject o{{"a", 80000}, {"b", 0.5}, {"c", QJsonArray{}}, {"d", QJsonObject{}}, {"e", QJsonArray{1, "x"}}};
    EXPECT_EQ(QString::fromUtf8(toJson(o)),
              "{\n  \"a\": 80000,\n  \"b\": 0.5,\n  \"c\": [],\n  \"d\": {},\n  \"e\": [\n    1,\n    \"x\"\n  ]\n}");
    EXPECT_EQ(QString::fromUtf8(toJson(QJsonValue("é\n\"q\""))), "\"é\\n\\\"q\\\"\"");
}

TEST(Storage, LibNameRejectsPaths)
{
    EXPECT_TRUE(isLibName("book-abc"));
    EXPECT_FALSE(isLibName(""));
    EXPECT_FALSE(isLibName("."));
    EXPECT_FALSE(isLibName(".."));
    EXPECT_FALSE(isLibName("a/b"));
    EXPECT_FALSE(isLibName("a\\b"));
    EXPECT_FALSE(isLibName(QString("a") + QChar(0)));
}

TEST(Storage, SlugTakesAccentsOff)
{
    EXPECT_EQ(slugOf("Capítulo Uno!"), "capitulo-uno");
    EXPECT_EQ(slugOf("  --Hello, World--  "), "hello-world");
    EXPECT_EQ(slugOf("Война и мир"), "");
}

TEST(Storage, JsonFallsBackOnBakThenPutsItBack)
{
    QTemporaryDir tmp;
    const QString f = tmp.filePath("x.json");
    ASSERT_TRUE(writeJsonFile(f, QJsonObject{{"v", 1}}));
    ASSERT_TRUE(writeJsonFile(f, QJsonObject{{"v", 2}}));
    EXPECT_EQ(readJsonFile(f + ".bak")->toObject().value("v").toInt(), 1);
    spit(f, ""); // a power cut's empty file
    QString from;
    auto v = readJsonFile(f, &from);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->toObject().value("v").toInt(), 1);
    EXPECT_EQ(from, "x.json.bak");
    EXPECT_EQ(readJsonFile(f)->toObject().value("v").toInt(), 1); // restored in place
}

TEST(Storage, TmpWinsOverBak)
{
    QTemporaryDir tmp;
    const QString f = tmp.filePath("x.json");
    spit(f, "{broken");
    spit(f + ".tmp", "{\"v\": 3}");
    spit(f + ".bak", "{\"v\": 1}");
    EXPECT_EQ(readJsonFile(f)->toObject().value("v").toInt(), 3);
}

TEST(Storage, CreateBookWritesTheFolder)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    lib.ensure("Works in Progress");
    const QJsonObject book = lib.createBook("Capítulo", "Jane");
    const QString id = book.value("id").toString();
    EXPECT_TRUE(id.startsWith("book-capitulo-"));
    EXPECT_TRUE(QFile::exists(lib.bookDir(id) + "/book.json"));
    EXPECT_TRUE(QFile::exists(lib.bookDir(id) + "/chapters"));
    EXPECT_EQ(lib.readJson(id, "darlings", QJsonValue()).toArray().size(), 0);
    auto meta = lib.readBookMeta(id);
    ASSERT_TRUE(meta);
    EXPECT_EQ(meta->value("author").toString(), "Jane");
    EXPECT_EQ(meta->value("tabNames").toObject().value("notes").toString(), "Notes");
}

TEST(Storage, ChaptersRoundTripAndStampChanges)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    const QString id = lib.createBook("", "").value("id").toString();
    ASSERT_TRUE(lib.writeChapter(id, "ch-1", "<p>Hello</p>"));
    EXPECT_EQ(lib.readChapter(id, "ch-1"), "<p>Hello</p>");
    const QString before = lib.chapterStamps(id).value("ch-1");
    ASSERT_TRUE(lib.writeChapter(id, "ch-1", "<p>Hello, world</p>"));
    EXPECT_NE(lib.chapterStamps(id).value("ch-1"), before); // size moved
    EXPECT_FALSE(lib.writeChapter(id, "../escape", "x"));
    EXPECT_FALSE(QFile::exists(tmp.filePath("escape.html")));
    lib.deleteChapter(id, "ch-1");
    EXPECT_TRUE(lib.chapterStamps(id).isEmpty());
}

TEST(Storage, LostBookJsonIsRebuiltFromChaptersAndCatalog)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    lib.ensure("Shelf");
    QJsonObject book = lib.createBook("My Novel", "Jane");
    const QString id = book.value("id").toString();
    QJsonObject libj = lib.readLibrary("Shelf");
    QJsonArray shelves = libj.value("shelves").toArray();
    QJsonObject s0 = shelves[0].toObject();
    s0["bookIds"] = QJsonArray{id};
    shelves[0] = s0;
    libj["shelves"] = shelves;
    lib.writeLibrary(libj); // writes the catalog
    lib.writeChapter(id, "ch-b", "<p>2</p>");
    lib.writeChapter(id, "ch-a", "<p>1</p>");
    QFile::remove(lib.bookDir(id) + "/book.json");
    QFile::remove(lib.bookDir(id) + "/book.json.bak");
    auto meta = lib.readBookMeta(id);
    ASSERT_TRUE(meta);
    EXPECT_EQ(meta->value("title").toString(), "My Novel");
    EXPECT_EQ(meta->value("chapterOrder").toArray(), (QJsonArray{"ch-a", "ch-b"}));
    EXPECT_TRUE(slurp(lib.dir() + "/neo-errors.log").contains("rebuilt from 2 chapter files"));
}

TEST(Storage, LostLibraryPutsEveryBookOnOneShelf)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    lib.ensure("Shelf");
    const QString a = lib.createBook("A", "").value("id").toString();
    const QString b = lib.createBook("B", "").value("id").toString();
    QFile::remove(lib.libraryFile());
    spit(lib.libraryFile(), "garbage"); // no .bak exists: the seed was a plain write
    const QJsonObject l = lib.readLibrary("Shelf");
    const QJsonArray ids = l.value("shelves").toArray()[0].toObject().value("bookIds").toArray();
    EXPECT_EQ(ids.size(), 2);
    EXPECT_TRUE(ids.contains(a) && ids.contains(b));
    EXPECT_TRUE(l.value("firstRunDone").toBool());
}

TEST(Storage, DailyBackupOncePerDayKeepsFourteen)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    lib.ensure("Shelf");
    lib.createBook("A", "");
    const QString zip = lib.dailyBackup("2026-01-01");
    ASSERT_FALSE(zip.isEmpty());
    EXPECT_TRUE(QFile::exists(zip));
    EXPECT_TRUE(lib.dailyBackup("2026-01-01").isEmpty()); // already done today
    for (int d = 2; d <= 20; ++d) lib.dailyBackup(QString("2026-01-%1").arg(d, 2, 10, QChar('0')));
    const QStringList left = QDir(tmp.filePath("Backups")).entryList({"neo-backup-*.zip"});
    EXPECT_EQ(left.size(), 14);
    EXPECT_EQ(left.first(), "neo-backup-2026-01-07.zip");
}

TEST(Storage, CoverNamesStayInsideTheBook)
{
    QTemporaryDir tmp;
    Library lib(tmp.path());
    const QString id = lib.createBook("A", "").value("id").toString();
    EXPECT_TRUE(lib.coverPath(id, "../../etc/passwd").isEmpty());
    EXPECT_FALSE(lib.coverPath(id, "cover-123.png").isEmpty());
    spit(tmp.filePath("pic.JPEG"), "x");
    const QString f = lib.setCover(id, tmp.filePath("pic.JPEG"));
    EXPECT_TRUE(f.startsWith("cover-") && f.endsWith(".jpg"));
    lib.removeCover(id);
    EXPECT_FALSE(QFile::exists(lib.bookDir(id) + '/' + f));
}
