#pragma once
// The bookshelf, as library.json keeps it: authors (pen names), each owning
// shelves; each shelf an ordered list of book ids. A bound shelf is one book:
// its cover and the pages a published book carries are small books of their
// own (book.json "kind"), kept in their places around the titles.
//
// Plain functions of the library object, so the rules can be tested: the
// caller reads and writes library.json.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace neosea::shelves {

// a book's kind ("" for a title, "cover", "part", …), read from its book.json
using KindOf = std::function<QString(const QString &bookId)>;

QJsonObject currentAuthor(QJsonObject &lib, const QString &anonymous);
QString currentAuthorId(QJsonObject &lib, const QString &anonymous);
QList<QJsonObject> shelvesFor(const QJsonObject &lib, const QString &authorId);
QStringList bookIds(const QJsonObject &shelf);
int indexOfShelf(const QJsonObject &lib, const QString &shelfId);
QJsonObject shelf(const QJsonObject &lib, const QString &shelfId);
void putShelf(QJsonObject &lib, const QJsonObject &shelf);
QString shelfOf(const QJsonObject &lib, const QString &bookId);
bool isBound(const QJsonObject &shelf);

QString addShelf(QJsonObject &lib, const QString &name, const QString &authorId);
// books move to another shelf of the same author; refused (false) for the last one
bool deleteShelf(QJsonObject &lib, const QString &shelfId, const KindOf &kindOf);
void moveShelf(QJsonObject &lib, const QString &shelfId, int toIndex);

// where titles may sit on a bound shelf: after the cover and front pages,
// before the back pages
std::pair<int, int> bodyRange(const QStringList &ids, const KindOf &kindOf);
// last in line (or first); on a bound shelf, the end (or start) of its body
void placeTitle(QJsonObject &lib, const QString &shelfId, const QString &bookId, const KindOf &kindOf,
                bool atStart = false);
// takes the book off every shelf and puts it at index (kept within the body of a bound shelf)
void moveBook(QJsonObject &lib, const QString &bookId, const QString &toShelf, int index, const KindOf &kindOf);
void removeBook(QJsonObject &lib, const QString &bookId);

// pen names: the first author is the home name
QString addAuthor(QJsonObject &lib, const QString &name, const QString &firstShelfName);
// its shelves go to the other name; refused for the last one
bool removeAuthor(QJsonObject &lib, const QString &authorId);
void renameAuthor(QJsonObject &lib, const QString &authorId, const QString &name);

// binding: the pages a bound shelf had come back in their places; unbinding
// parks them (each part remembering the title it opened)
void bind(QJsonObject &lib, const QString &shelfId, const QString &coverId);
void unbind(QJsonObject &lib, const QString &shelfId, const KindOf &kindOf);
// where a new page of this kind goes on the shelf
int pageIndex(const QStringList &ids, const QString &kind, const QString &beforeId, const KindOf &kindOf);

} // namespace neosea::shelves
