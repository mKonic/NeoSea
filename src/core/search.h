#pragma once
// Find & replace over a chapter's document, as NEO does it on the page: any
// case, matches inside one run of type (a word half in italics isn't one),
// in reading order. A replacement takes the type of what it replaces.

#include <QList>
#include <QString>

class QTextDocument;

namespace neosea::search {

struct Match {
    int pos = 0, len = 0;
    bool operator==(const Match &) const = default;
};

QList<Match> find(const QTextDocument &d, const QString &q);
QList<Match> findInText(const QString &text, const QString &q);
// one undo step on the document; the number replaced
int replace(QTextDocument &d, const Match &m, const QString &with);
int replaceAll(QTextDocument &d, const QString &q, const QString &with);

} // namespace neosea::search
