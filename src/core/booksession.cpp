#include "core/booksession.h"

#include "core/bookmodel.h"
#include "core/chapter.h"
#include "core/i18n.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace neosea {

QString metaSig(const QJsonObject &m)
{
    QJsonObject c;
    for (auto it = m.begin(); it != m.end(); ++it) {
        const QString &k = it.key();
        if (k == "lastPosition" || k == "modified" || k == "wordCount" || k == "dailyCounts") continue;
        const QJsonValue v = it.value();
        if (v.isNull() || v.isUndefined() || (v.isString() && v.toString().isEmpty())) continue;
        if (v.isObject() && v.toObject().isEmpty()) continue;
        if (v.isArray() && v.toArray().isEmpty()) continue;
        c.insert(k, v);
    }
    return QString::fromUtf8(QJsonDocument(c).toJson(QJsonDocument::Compact));
}

bool onlyDrops(const QString &page, const QString &disk)
{
    static const QRegularExpression tags("<[^>]*>");
    static const QRegularExpression ws("\\s+");
    auto bag = [&](QString html) {
        QHash<QString, int> m;
        html.replace(tags, " ");
        for (const QString &w : html.split(ws, Qt::SkipEmptyParts)) m[w]++;
        return m;
    };
    const auto here = bag(page);
    const auto there = bag(disk);
    for (auto it = there.begin(); it != there.end(); ++it)
        if (it.value() > here.value(it.key())) return false;
    for (auto it = here.begin(); it != here.end(); ++it)
        if (it.value() > there.value(it.key())) return true;
    return false;
}

BookSession::BookSession(const Library &lib, QString bookId) : m_lib(lib), m_id(std::move(bookId)) {}

bool BookSession::load()
{
    auto meta = m_lib.readBookMeta(m_id);
    if (!meta) return false;
    m_book = *meta;
    m_html.clear();
    m_saved.clear();
    m_stamps.clear();
    for (const QString &chId : order()) {
        const QString html = m_lib.readChapter(m_id, chId);
        m_html.insert(chId, html);
        m_saved.insert(chId, html);
    }
    m_savedSig = metaSig(m_book); // what disk holds; defaults filled in later don't count
    m_stickies = m_lib.readJson(m_id, "stickies", QJsonArray{}).toArray();
    m_darlings = m_lib.readJson(m_id, "darlings", QJsonArray{}).toArray();
    m_undo.clear();
    return true;
}

QStringList BookSession::order() const { return chapterOrder(m_book); }

void BookSession::setOrder(const QStringList &o) { setChapterOrder(m_book, o); }

void BookSession::setChapterHtml(const QString &chId, const QString &html) { m_html.insert(chId, html); }

bool BookSession::chapterDirty(const QString &chId) const
{
    return m_html.contains(chId) && m_html.value(chId) != m_saved.value(chId);
}

bool BookSession::persistChapter(const QString &chId) { return persistChapter(chId, m_html.value(chId)); }

bool BookSession::persistChapter(const QString &chId, const QString &html)
{
    const QString before = m_saved.value(chId);
    m_saved.insert(chId, html);
    if (!m_lib.writeChapter(m_id, chId, html)) {
        // it never reached the disk: unsaved again, so the next flush retries
        // and a look at the disk can't take the old file for news
        if (m_saved.value(chId) == html) m_saved.insert(chId, before);
        return false;
    }
    m_stamps.insert(chId, m_lib.chapterStamps(m_id).value(chId));
    return true;
}

bool BookSession::saveMeta()
{
    const QString sig = metaSig(m_book);
    const QString stamp = m_lib.writeBookMeta(m_id, m_book);
    if (stamp.isEmpty()) return false;
    m_book.insert("modified", stamp);
    m_savedSig = sig;
    return true;
}

bool BookSession::metaDirty() const { return metaSig(m_book) != m_savedSig; }

void BookSession::flush()
{
    for (const QString &chId : order())
        if (chapterDirty(chId)) persistChapter(chId);
    if (metaDirty()) saveMeta();
}

void BookSession::saveStickies() { m_lib.writeJson(m_id, "stickies", m_stickies); }
void BookSession::saveDarlings() { m_lib.writeJson(m_id, "darlings", m_darlings); }

QString BookSession::createChapterAt(qsizetype index, const QString &html)
{
    const QString chId = newId("ch-");
    QStringList o = order();
    o.insert(std::clamp<qsizetype>(index, 0, o.size()), chId);
    setOrder(o);
    m_html.insert(chId, html);
    persistChapter(chId, html);
    saveMeta();
    return chId;
}

void BookSession::deleteChapter(const QString &chId)
{
    QStringList o = order();
    o.removeAll(chId);
    setOrder(o);
    m_html.remove(chId);
    m_saved.remove(chId);
    m_stamps.remove(chId);
    for (const char *key : {"sectionNotes", "chapterNotes", "chapterKinds"}) {
        if (!m_book.contains(key)) continue;
        QJsonObject x = m_book.value(key).toObject();
        x.remove(chId);
        m_book.insert(key, x);
    }
    QJsonArray kept;
    for (const auto &s : m_stickies)
        if (s.toObject().value("chapterId").toString() != chId) kept << s;
    m_stickies = kept;
    saveStickies();
    m_lib.deleteChapter(m_id, chId);
    saveMeta();
}

RefreshResult BookSession::refresh(const QString &otherDeviceTitleSuffix)
{
    RefreshResult res;
    auto metaOpt = m_lib.readBookMeta(m_id);
    if (!metaOpt) return res;
    const QJsonObject meta = *metaOpt;

    // first read everything that changed; decide it all in one go after
    const bool theirs = metaSig(meta) != m_savedSig && meta.value("chapterOrder").isArray();
    const QString sigHere = metaSig(m_book);
    const bool mine = sigHere != m_savedSig;
    QHash<QString, QString> incoming;
    QJsonArray sideStickies = m_stickies, sideDarlings = m_darlings;
    if (theirs) {
        for (const QString &chId : chapterOrder(meta))
            if (!m_html.contains(chId)) incoming.insert(chId, m_lib.readChapter(m_id, chId));
        sideStickies = m_lib.readJson(m_id, "stickies", m_stickies).toArray();
        sideDarlings = m_lib.readJson(m_id, "darlings", m_darlings).toArray();
    }
    const QHash<QString, QString> stamps = m_lib.chapterStamps(m_id);
    struct Fresh {
        QString chId, st, disk;
    };
    QList<Fresh> fresh;
    for (const QString &chId : order()) {
        const QString st = stamps.value(chId);
        if (stamps.contains(chId) && st == m_stamps.value(chId)) continue;
        if (!stamps.contains(chId) && !m_stamps.contains(chId) && m_saved.contains(chId)) continue;
        fresh << Fresh{chId, st, m_lib.readChapter(m_id, chId)};
    }

    if (theirs && metaSig(m_book) == sigHere) {
        // whichever book.json stands, no chapter holding words is dropped
        QStringList o = mine ? order() : chapterOrder(meta);
        const QStringList other = mine ? chapterOrder(meta) : order();
        static const QRegularExpression tags("<[^>]*>");
        for (qsizetype i = 0; i < other.size(); ++i) {
            const QString chId = other[i];
            if (o.contains(chId)) continue;
            if (mine) {
                QString text = incoming.value(chId);
                text.replace(tags, "");
                if (text.trimmed().isEmpty()) continue;
            } else if (m_html.value(chId) == m_saved.value(chId)) {
                continue;
            }
            qsizetype at = 0;
            for (qsizetype j = i - 1; j >= 0; --j)
                if (o.contains(other[j])) { at = o.indexOf(other[j]) + 1; break; }
            o.insert(at, chId);
        }
        if (!mine || o.size() != order().size()) {
            for (const QString &chId : o) {
                if (!incoming.contains(chId)) continue;
                m_html.insert(chId, incoming.value(chId));
                m_saved.insert(chId, incoming.value(chId));
            }
            if (mine) {
                setOrder(o);
            } else {
                const QJsonValue lastPosition = m_book.value("lastPosition");
                m_book = meta;
                setOrder(o);
                if (!lastPosition.isUndefined()) m_book.insert("lastPosition", lastPosition);
                m_savedSig = metaSig(meta);
                m_stickies = sideStickies;
                m_darlings = sideDarlings;
            }
            if (metaDirty()) saveMeta();
            m_undo.clear(); // snapshots of the old structure must not replay over the new one
            res.restructured = true;
        }
    }

    QJsonArray replaced;
    for (const Fresh &f : fresh) {
        if (!order().contains(f.chId)) continue;
        if (f.disk.isEmpty() && !m_saved.value(f.chId).isEmpty()) continue; // unreadable or still syncing
        m_stamps.insert(f.chId, f.st);
        if (f.disk == m_saved.value(f.chId)) continue;
        if (m_html.value(f.chId) == m_saved.value(f.chId)) {
            if (onlyDrops(m_html.value(f.chId), f.disk)) {
                QStringList texts;
                for (const Para &p : parseChapter(m_html.value(f.chId))) texts << p.text();
                replaced.append(QJsonObject{
                    {"id", newId("d-")},
                    {"html", m_html.value(f.chId)},
                    {"text", texts.join("\n\n").left(2000)},
                    {"chapterId", f.chId},
                    {"chapterLabel", t("Chapter {n}", {{"n", int(order().indexOf(f.chId)) + 1}})},
                    {"date", isoNow()},
                });
            }
            m_html.insert(f.chId, f.disk);
            m_saved.insert(f.chId, f.disk);
            res.adopted++;
        } else {
            m_saved.insert(f.chId, f.disk); // what's on disk now; ours goes over it next
            QStringList o = order();
            const QString twin = newId("ch-");
            o.insert(o.indexOf(f.chId) + 1, twin);
            setOrder(o);
            QJsonObject titles = m_book.value("chapterTitles").toObject();
            titles.insert(twin, (titles.value(f.chId).toString() + " " + otherDeviceTitleSuffix).trimmed());
            m_book.insert("chapterTitles", titles);
            m_html.insert(twin, f.disk);
            persistChapter(twin, f.disk);
            persistChapter(f.chId);
            saveMeta();
            res.conflicts++;
        }
    }
    if (!replaced.isEmpty()) {
        QJsonArray all = replaced;
        for (const auto &d : m_darlings) all.append(d);
        m_darlings = all;
        saveDarlings();
        res.displaced = int(replaced.size());
    }
    return res;
}

void BookSession::snapshot(const QString &label, bool rejoin, const QJsonObject &caret)
{
    Snapshot s;
    s.label = label;
    s.rejoin = rejoin;
    s.order = order();
    s.kinds = m_book.value("chapterKinds").toObject();
    s.titles = m_book.value("chapterTitles").toObject();
    s.notes = m_book.value("chapterNotes").toObject();
    s.sectionNotes = m_book.value("sectionNotes").toObject();
    s.sceneNotes = m_book.value("sceneNotes").toObject();
    s.looseCards = m_book.value("looseCards").toArray();
    s.darlings = m_darlings;
    s.stickies = m_stickies;
    s.html = m_html;
    s.caret = caret;
    m_undo << s;
    if (m_undo.size() > 10) m_undo.removeFirst();
}

std::optional<BookSession::Snapshot> BookSession::undoStructure()
{
    if (m_undo.isEmpty()) return std::nullopt;
    Snapshot s = m_undo.takeLast();
    setOrder(s.order);
    auto put = [&](const char *key, const QJsonObject &v) {
        if (v.isEmpty() && !m_book.contains(key)) return;
        m_book.insert(key, v);
    };
    put("chapterKinds", s.kinds);
    put("chapterTitles", s.titles);
    put("chapterNotes", s.notes);
    put("sectionNotes", s.sectionNotes);
    put("sceneNotes", s.sceneNotes);
    if (!s.looseCards.isEmpty() || m_book.contains("looseCards")) m_book.insert("looseCards", s.looseCards);
    m_html = s.html;
    m_darlings = s.darlings;
    m_stickies = s.stickies;
    // resurrect any chapter files the action deleted
    for (const QString &chId : s.order) {
        QString html = m_html.value(chId);
        if (html.isEmpty()) html = "<p><br></p>";
        m_html.insert(chId, html);
        persistChapter(chId, html);
    }
    saveDarlings();
    saveStickies();
    saveMeta();
    return s;
}

} // namespace neosea
