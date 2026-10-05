#include "app/app.h"

#include "core/bookmodel.h"
#include "core/chapter.h"
#include "core/i18n.h"
#include "core/importer.h"
#include "core/screenplay.h"
#include "core/typing.h"

#include <QDate>
#include <QJsonArray>

namespace neosea {

App::App(const QString &libraryDir, QObject *parent) : QObject(parent), m_lib(libraryDir)
{
    reloadLibrary();
}

void App::reloadLibrary()
{
    m_library = m_lib.readLibrary(t("Works in Progress"));
    shelves::currentAuthor(m_library, anonymous());
}

void App::writeLibrary()
{
    m_generation++;
    m_lib.writeLibrary(m_library);
}

bool App::refreshLibrary()
{
    const QJsonObject disk = m_lib.readLibrary(t("Works in Progress"));
    QJsonObject mine = m_library;
    if (!disk.value("firstRunDone").toBool() || disk == mine) return false;
    m_library = disk;
    shelves::currentAuthor(m_library, anonymous());
    m_metaCache.clear();
    emit libraryChanged();
    return true;
}

std::optional<QJsonObject> App::meta(const QString &bookId)
{
    if (auto it = m_metaCache.constFind(bookId); it != m_metaCache.constEnd()) return *it;
    auto m = m_lib.readBookMeta(bookId);
    if (m) m_metaCache.insert(bookId, *m);
    return m;
}

void App::writeMeta(const QString &bookId, QJsonObject meta)
{
    m_metaCache.remove(bookId);
    m_lib.writeBookMeta(bookId, meta);
}

shelves::KindOf App::kindOf()
{
    return [this](const QString &id) {
        auto m = meta(id);
        return m ? m->value("kind").toString() : QString();
    };
}

QString App::anonymous() const { return t("Anonymous"); }

QString App::authorId() { return shelves::currentAuthorId(m_library, anonymous()); }

QString App::authorName()
{
    const QString n = shelves::currentAuthor(m_library, anonymous()).value("name").toString();
    return n.isEmpty() ? anonymous() : n;
}

QList<QJsonObject> App::myShelves() { return shelves::shelvesFor(m_library, authorId()); }

QString App::createBook(const QString &shelfId, bool script)
{
    QJsonObject meta = m_lib.createBook({}, authorName());
    const QString id = meta.value("id").toString();
    const QJsonObject defaults = m_library.value("tabDefaults").toObject();
    meta.insert("tabNames", QJsonObject{{"notes", defaults.value("notes").toString("Notes")},
                                        {"outline", script ? QString("Outline") : defaults.value("outline").toString("Outline")}});
    if (script) {
        const QString chId = newId("ch-");
        m_lib.writeChapter(id, chId, "<p><br></p>");
        meta.insert("format", "screenplay");
        meta.insert("chapterOrder", QJsonArray{chId});
        meta.insert("credit", t("Written by"));
    }
    writeMeta(id, meta);
    shelves::placeTitle(m_library, shelfId, id, kindOf());
    writeLibrary();
    return id;
}

QString App::createPageBook(const QString &shelfId, const QString &kind)
{
    const QJsonObject shelf = shelves::shelf(m_library, shelfId);
    const QString shelfName = shelf.value("name").toString();
    const bool written = kPageWritten.contains(kind);
    const QString title = kind == "cover" ? shelfName : written ? pageKindName(kind) : pageKindName(kind) + " — " + shelfName;
    QJsonObject meta = m_lib.createBook(title, authorName());
    const QString id = meta.value("id").toString();
    meta.insert("title", title);
    meta.insert("kind", kind);
    meta.insert("shelfId", shelfId);
    if (written) meta.insert("subtitle", shelfName);
    if (kind == "cover") {
        meta.insert("coverSeed", "bound:" + shelfId);
        meta.insert("chapterOrder", QJsonArray{});
    } else {
        const QString chId = newId("ch-");
        QString html = "<p><br></p>";
        if (kind == "copyright")
            html = "<p>" + escHtml(t("Copyright © {year} {name}", {{"year", QString::number(QDate::currentDate().year())}, {"name", authorName()}}))
                + "</p><p>" + escHtml(t("All rights reserved.")) + "</p>";
        m_lib.writeChapter(id, chId, html);
        meta.insert("chapterOrder", QJsonArray{chId});
    }
    writeMeta(id, meta);
    return id;
}

App::ImportOutcome App::importFiles(const QStringList &paths, const QString &shelfIdIn)
{
    ImportOutcome out;
    QString shelfId = shelfIdIn;
    if (shelfId.isEmpty() && !myShelves().isEmpty()) shelfId = myShelves().first().value("id").toString();
    const typing::DashStyle dashes = typing::dashStyle(writingLanguage());
    const QJsonObject defaults = m_library.value("tabDefaults").toObject();
    for (const QString &path : paths) {
        const ImportResult r = importFile(path);
        if (!r.error.isEmpty()) {
            out.errors << r.name + ": " + r.error;
            continue;
        }
        if (!r.script.isEmpty()) {
            // a script: its lines sorted into elements, its title page read
            QList<sp::FdxLine> lines;
            sp::TitlePage tp;
            if (r.script == "fdx") {
                const sp::Fdx f = sp::fromFdx(r.source);
                lines = f.lines;
                tp = f.title;
            } else {
                for (const sp::Line &l : sp::fromFountain(r.source)) lines << sp::FdxLine{l.type, sp::runsFromFountain(l.text)};
                tp = sp::fountainTitle(r.source);
            }
            if (lines.isEmpty()) {
                out.errors << r.name + ": " + t("no script in it");
                continue;
            }
            const QString title = tp.title.isEmpty() ? r.name : tp.title;
            QJsonObject meta = m_lib.createBook(title, tp.author.isEmpty() ? authorName() : tp.author);
            const QString id = meta.value("id").toString();
            QList<Para> paras;
            int words = 0;
            for (const sp::FdxLine &l : lines) {
                Para p;
                sp::setType(p, l.type);
                p.runs = l.runs;
                p.normalize();
                words += countWords(p.text());
                paras << p;
            }
            const QString chId = newId("ch-");
            m_lib.writeChapter(id, chId, serializeChapter(paras));
            meta.insert("title", title);
            meta.insert("format", "screenplay");
            meta.insert("chapterOrder", QJsonArray{chId});
            meta.insert("credit", tp.credit.isEmpty() ? t("Written by") : tp.credit);
            if (!tp.draft.isEmpty()) meta.insert("draft", tp.draft);
            meta.insert("tabNames", QJsonObject{{"notes", defaults.value("notes").toString("Notes")}, {"outline", "Outline"}});
            meta.insert("wordCount", words);
            if (!tp.contact.isEmpty() && m_library.value("scriptContact").toString().isEmpty())
                m_library.insert("scriptContact", tp.contact);
            writeMeta(id, meta);
            shelves::placeTitle(m_library, shelfId, id, kindOf());
            out.scripts++;
            continue;
        }
        const QString title = r.title.isEmpty() ? r.name : r.title;
        QJsonObject meta = m_lib.createBook(title, r.author.isEmpty() ? authorName() : r.author);
        const QString id = meta.value("id").toString();
        meta.insert("title", title);
        meta.insert("tabNames", QJsonObject{{"notes", defaults.value("notes").toString("Notes")},
                                            {"outline", defaults.value("outline").toString("Outline")}});
        QJsonObject titles;
        QJsonArray order;
        int words = 0;
        for (const ImportChapter &ch : r.chapters) {
            const QString chId = newId("ch-");
            m_lib.writeChapter(id, chId, importedChapterHtml(ch, dashes));
            if (!ch.title.isEmpty()) titles.insert(chId, ch.title);
            if (!ch.role.isEmpty()) meta.insert(ch.role, chId); // settled into a kind on first open
            order.append(chId);
            for (const ImportPara &p : ch.paras) words += countWords(p.text);
        }
        meta.insert("chapterTitles", titles);
        meta.insert("chapterOrder", order);
        meta.insert("wordCount", words);
        writeMeta(id, meta);
        shelves::placeTitle(m_library, shelfId, id, kindOf());
        out.books++;
    }
    writeLibrary();
    emit libraryChanged();
    return out;
}

bool App::openBook(const QString &bookId)
{
    closeBook();
    auto s = std::make_unique<BookSession>(m_lib, bookId);
    if (!s->load()) return false;
    m_session = std::move(s);
    emit bookOpened();
    return true;
}

void App::closeBook()
{
    if (!m_session) return;
    m_session->flush();
    m_metaCache.remove(m_session->id());
    m_session.reset();
    emit bookClosed();
}

QString App::writingLanguage() const
{
    const QString chosen = m_library.value("spellLanguage").toString();
    return chosen.isEmpty() ? I18n::locale() : chosen;
}

} // namespace neosea

#include "moc_app.cpp"
