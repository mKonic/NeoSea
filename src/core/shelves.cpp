#include "core/shelves.h"

#include "core/bookmodel.h"

#include <QDateTime>
#include <QJsonArray>

namespace neosea::shelves {

namespace {

QJsonArray shelvesArr(const QJsonObject &lib) { return lib.value("shelves").toArray(); }

QString base36Now() { return QString::number(QDateTime::currentMSecsSinceEpoch(), 36); }

void setIds(QJsonObject &s, const QStringList &ids) { s.insert("bookIds", QJsonArray::fromStringList(ids)); }

} // namespace

QJsonObject currentAuthor(QJsonObject &lib, const QString &anonymous)
{
    QJsonArray authors = lib.value("authors").toArray();
    if (authors.isEmpty()) {
        QString name = lib.value("authorName").toString();
        if (name.isEmpty()) name = lib.value("penNames").toArray().at(0).toString();
        if (name.isEmpty()) name = anonymous;
        authors.append(QJsonObject{{"id", "a1"}, {"name", name}});
        lib.insert("authors", authors);
    }
    const QString cur = lib.value("currentAuthorId").toString();
    for (const auto &a : authors)
        if (a.toObject().value("id").toString() == cur) return a.toObject();
    return authors.first().toObject();
}

QString currentAuthorId(QJsonObject &lib, const QString &anonymous)
{
    return currentAuthor(lib, anonymous).value("id").toString();
}

QList<QJsonObject> shelvesFor(const QJsonObject &lib, const QString &authorId)
{
    const QJsonArray authors = lib.value("authors").toArray();
    const QString home = authors.isEmpty() ? QStringLiteral("a1") : authors.first().toObject().value("id").toString();
    QList<QJsonObject> out;
    for (const auto &v : shelvesArr(lib)) {
        const QJsonObject s = v.toObject();
        const QString owner = s.value("authorId").toString();
        if ((owner.isEmpty() ? home : owner) == authorId) out << s;
    }
    return out;
}

QStringList bookIds(const QJsonObject &s)
{
    QStringList out;
    for (const auto &v : s.value("bookIds").toArray()) out << v.toString();
    return out;
}

int indexOfShelf(const QJsonObject &lib, const QString &shelfId)
{
    const QJsonArray a = shelvesArr(lib);
    for (int i = 0; i < a.size(); ++i)
        if (a[i].toObject().value("id").toString() == shelfId) return i;
    return -1;
}

QJsonObject shelf(const QJsonObject &lib, const QString &shelfId)
{
    const int i = indexOfShelf(lib, shelfId);
    return i < 0 ? QJsonObject{} : shelvesArr(lib)[i].toObject();
}

void putShelf(QJsonObject &lib, const QJsonObject &s)
{
    QJsonArray a = shelvesArr(lib);
    const int i = indexOfShelf(lib, s.value("id").toString());
    if (i < 0) a.append(s);
    else a[i] = s;
    lib.insert("shelves", a);
}

QString shelfOf(const QJsonObject &lib, const QString &bookId)
{
    for (const auto &v : shelvesArr(lib))
        if (bookIds(v.toObject()).contains(bookId)) return v.toObject().value("id").toString();
    return {};
}

bool isBound(const QJsonObject &s) { return s.value("binding").toObject().value("bound").toBool(); }

QString addShelf(QJsonObject &lib, const QString &name, const QString &authorId)
{
    QString id = "shelf-" + base36Now();
    while (indexOfShelf(lib, id) >= 0) id += "x";
    QJsonArray a = shelvesArr(lib);
    a.append(QJsonObject{{"id", id}, {"name", name}, {"bookIds", QJsonArray{}}, {"authorId", authorId}});
    lib.insert("shelves", a);
    return id;
}

bool deleteShelf(QJsonObject &lib, const QString &shelfId, const KindOf &kindOf)
{
    QJsonObject gone = shelf(lib, shelfId);
    if (gone.isEmpty()) return false;
    const QJsonArray authors = lib.value("authors").toArray();
    const QString home = authors.isEmpty() ? QStringLiteral("a1") : authors.first().toObject().value("id").toString();
    QString owner = gone.value("authorId").toString();
    if (owner.isEmpty()) owner = home;
    const QList<QJsonObject> mine = shelvesFor(lib, owner);
    if (mine.size() <= 1) return false;
    QString otherId;
    for (const auto &s : mine)
        if (s.value("id").toString() != shelfId) { otherId = s.value("id").toString(); break; }
    for (const QString &id : bookIds(gone)) {
        if (kPageKinds.contains(kindOf(id))) continue; // a bound book's pages stay in the folder
        if (!bookIds(shelf(lib, otherId)).contains(id)) placeTitle(lib, otherId, id, kindOf);
    }
    QJsonArray a = shelvesArr(lib);
    a.removeAt(indexOfShelf(lib, shelfId));
    lib.insert("shelves", a);
    return true;
}

void moveShelf(QJsonObject &lib, const QString &shelfId, int toIndex)
{
    QJsonArray a = shelvesArr(lib);
    const int from = indexOfShelf(lib, shelfId);
    if (from < 0) return;
    const QJsonValue v = a.takeAt(from);
    a.insert(std::clamp(toIndex, 0, int(a.size())), v);
    lib.insert("shelves", a);
}

std::pair<int, int> bodyRange(const QStringList &ids, const KindOf &kindOf)
{
    int start = 0;
    while (start < ids.size() && kPageLead.contains(kindOf(ids[start]))) start++;
    int end = int(ids.size());
    while (end > start && kPageTail.contains(kindOf(ids[end - 1]))) end--;
    return {start, end};
}

void placeTitle(QJsonObject &lib, const QString &shelfId, const QString &bookId, const KindOf &kindOf, bool atStart)
{
    QJsonObject s = shelf(lib, shelfId);
    if (s.isEmpty()) return;
    QStringList ids = bookIds(s);
    ids.removeAll(bookId);
    if (!isBound(s)) {
        if (atStart) ids.prepend(bookId);
        else ids.append(bookId);
    } else {
        const auto [start, end] = bodyRange(ids, kindOf);
        ids.insert(atStart ? start : end, bookId);
    }
    setIds(s, ids);
    putShelf(lib, s);
}

void removeBook(QJsonObject &lib, const QString &bookId)
{
    QJsonArray a = shelvesArr(lib);
    for (int i = 0; i < a.size(); ++i) {
        QJsonObject s = a[i].toObject();
        QStringList ids = bookIds(s);
        if (ids.removeAll(bookId)) {
            setIds(s, ids);
            a[i] = s;
        }
    }
    lib.insert("shelves", a);
}

void moveBook(QJsonObject &lib, const QString &bookId, const QString &toShelf, int index, const KindOf &kindOf)
{
    QJsonObject target = shelf(lib, toShelf);
    if (target.isEmpty()) return;
    removeBook(lib, bookId);
    target = shelf(lib, toShelf);
    QStringList ids = bookIds(target);
    int at = std::clamp(index, 0, int(ids.size()));
    if (isBound(target)) {
        if (kPageKinds.contains(kindOf(bookId))) return; // a page keeps its place
        const auto [start, end] = bodyRange(ids, kindOf);
        at = std::clamp(at, start, end);
    }
    ids.insert(at, bookId);
    setIds(target, ids);
    putShelf(lib, target);
}

QString addAuthor(QJsonObject &lib, const QString &name, const QString &firstShelfName)
{
    QJsonArray authors = lib.value("authors").toArray();
    const QString id = "a-" + base36Now();
    authors.append(QJsonObject{{"id", id}, {"name", name}});
    lib.insert("authors", authors);
    lib.insert("currentAuthorId", id);
    addShelf(lib, firstShelfName, id);
    return id;
}

bool removeAuthor(QJsonObject &lib, const QString &authorId)
{
    QJsonArray authors = lib.value("authors").toArray();
    if (authors.size() < 2) return false;
    const QString home = authors.first().toObject().value("id").toString();
    QJsonArray rest;
    for (const auto &a : authors)
        if (a.toObject().value("id").toString() != authorId) rest.append(a);
    if (rest.size() == authors.size()) return false;
    const QString target = rest.first().toObject().value("id").toString();
    QJsonArray a = shelvesArr(lib);
    for (int i = 0; i < a.size(); ++i) {
        QJsonObject s = a[i].toObject();
        QString owner = s.value("authorId").toString();
        if (owner.isEmpty()) owner = home;
        if (owner == authorId) {
            s.insert("authorId", target);
            a[i] = s;
        }
    }
    lib.insert("shelves", a);
    lib.insert("authors", rest);
    lib.insert("currentAuthorId", target);
    lib.insert("authorName", rest.first().toObject().value("name").toString());
    return true;
}

void renameAuthor(QJsonObject &lib, const QString &authorId, const QString &name)
{
    QJsonArray authors = lib.value("authors").toArray();
    for (int i = 0; i < authors.size(); ++i) {
        QJsonObject a = authors[i].toObject();
        if (a.value("id").toString() != authorId) continue;
        a.insert("name", name);
        authors[i] = a;
    }
    lib.insert("authors", authors);
    // the legacy field follows the first name
    lib.insert("authorName", authors.first().toObject().value("name").toString());
}

void bind(QJsonObject &lib, const QString &shelfId, const QString &coverId)
{
    QJsonObject s = shelf(lib, shelfId);
    QJsonObject binding = s.value("binding").toObject();
    if (!binding.contains("numbering")) binding.insert("numbering", "through");
    binding.insert("bound", true);
    // the parked pages come back where they were
    const QJsonArray parked = binding.value("parked").toArray();
    QStringList ids = bookIds(s);
    if (!parked.isEmpty()) {
        QList<QJsonObject> front, back, parts;
        QStringList parkedIds;
        for (const auto &p : parked) {
            const QJsonObject o = p.toObject();
            parkedIds << o.value("id").toString();
            const QString k = o.value("kind").toString();
            if (kPageLead.contains(k)) front << o;
            else if (kPageTail.contains(k)) back << o;
            else if (k == "part") parts << o;
        }
        auto byOrder = [](const QStringList &order) {
            return [order](const QJsonObject &a, const QJsonObject &b) {
                return order.indexOf(a.value("kind").toString()) < order.indexOf(b.value("kind").toString());
            };
        };
        std::stable_sort(front.begin(), front.end(), byOrder(kPageLead));
        std::stable_sort(back.begin(), back.end(), byOrder(kPageTail));
        QStringList body;
        for (const QString &id : ids)
            if (!parkedIds.contains(id)) body << id;
        for (const auto &p : parts) {
            const int at = body.indexOf(p.value("before").toString());
            if (at >= 0) body.insert(at, p.value("id").toString());
            else body << p.value("id").toString();
        }
        ids.clear();
        for (const auto &p : front) ids << p.value("id").toString();
        ids << body;
        for (const auto &p : back) ids << p.value("id").toString();
        binding.insert("parked", QJsonArray{});
    }
    if (!coverId.isEmpty() && !ids.contains(coverId)) ids.prepend(coverId);
    s.insert("binding", binding);
    setIds(s, ids);
    putShelf(lib, s);
}

void unbind(QJsonObject &lib, const QString &shelfId, const KindOf &kindOf)
{
    QJsonObject s = shelf(lib, shelfId);
    const QStringList ids = bookIds(s);
    QJsonArray parked;
    QStringList titles;
    for (int i = 0; i < ids.size(); ++i) {
        const QString k = kindOf(ids[i]);
        if (!kPageKinds.contains(k)) {
            titles << ids[i];
            continue;
        }
        // a part page remembers the title it opens
        QString before;
        if (k == "part")
            for (int j = i + 1; j < ids.size(); ++j)
                if (!kPageKinds.contains(kindOf(ids[j]))) { before = ids[j]; break; }
        QJsonObject p{{"id", ids[i]}, {"kind", k}};
        p.insert("before", before.isEmpty() ? QJsonValue() : QJsonValue(before));
        parked.append(p);
    }
    QJsonObject binding = s.value("binding").toObject();
    binding.insert("bound", false);
    binding.insert("parked", parked);
    s.insert("binding", binding);
    setIds(s, titles);
    putShelf(lib, s);
}

int pageIndex(const QStringList &ids, const QString &kind, const QString &beforeId, const KindOf &kindOf)
{
    if (kind == "part") {
        int at = beforeId.isEmpty() ? -1 : int(ids.indexOf(beforeId));
        if (at < 0) at = bodyRange(ids, kindOf).second;
        return at;
    }
    if (kPageLead.contains(kind)) {
        int at = 0;
        for (int i = 0; i < ids.size(); ++i) {
            const QString k = kindOf(ids[i]);
            if (kPageLead.contains(k) && kPageLead.indexOf(k) < kPageLead.indexOf(kind)) at = i + 1;
            else break;
        }
        return at;
    }
    int at = int(ids.size());
    for (int i = int(ids.size()) - 1; i >= 0; --i) {
        const QString k = kindOf(ids[i]);
        if (kPageTail.contains(k) && kPageTail.indexOf(k) > kPageTail.indexOf(kind)) at = i;
        else break;
    }
    return at;
}

} // namespace neosea::shelves
