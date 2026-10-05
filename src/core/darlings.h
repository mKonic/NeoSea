#pragma once
// Darlings: words cut from the manuscript and kept. A darling remembers where
// it came from by the words around the cut (no marker is left in the text),
// as NEO does: the 60 characters before it and the 60 after, counted the way
// the page's text runs, paragraphs end to end.

#include <QJsonObject>
#include <QString>

class QTextCursor;
class QTextDocument;

namespace neosea::darlings {

QString bodyPlain(const QTextDocument &d);
int toDocPos(const QTextDocument &d, int plainPos);
int toPlainPos(const QTextDocument &d, int docPos);
// where a darling goes back: between its two anchors, else after the first,
// else before the second; -1 when the page no longer has either
int findPosition(const QString &plain, const QString &prefix, const QString &suffix);

// Cut the selection out of its chapter and make the darling of it. A
// paragraph cut whole leaves no empty line behind; the caret is left where
// the words were.
QJsonObject cut(QTextCursor &c, const QString &chId, const QString &chapterLabel);

} // namespace neosea::darlings
