#pragma once
// A chapter in a QTextDocument, and back. One block per paragraph; what NEO
// keeps on the <p> (its class, its attributes, in order) rides on the block as
// user properties, its alignment as the block's own. Bold, italic, underline
// and strikethrough are the characters' own formats; a placeholder's ⚑ carries
// its sticky id. Nothing the screen adds (drop caps, highlights, ghost grey) is
// stored in the document: the layout and extra selections draw those.

#include "core/chapter.h"

#include <QTextCharFormat>
#include <QTextFormat>

class QTextDocument;
class QTextBlock;

namespace neosea::doc {

enum Property {
    ParaClass = QTextFormat::UserProperty + 1, // QString, space-separated
    ParaAttrs = QTextFormat::UserProperty + 2, // QStringList of name, value pairs, in order
    MarkSid = QTextFormat::UserProperty + 10,  // QString on a ⚑
};

void load(QTextDocument &d, const QList<Para> &paras);
QList<Para> paragraphs(const QTextDocument &d);
inline void loadHtml(QTextDocument &d, const QString &html) { load(d, parseChapter(html)); }
inline QString html(const QTextDocument &d) { return serializeChapter(paragraphs(d)); }

QStringList classes(const QTextBlock &b);
bool hasClass(const QTextBlock &b, const QString &cls);
// sets the block's class list (through a cursor, so it's one undo step)
void setClasses(QTextBlock b, const QStringList &classes);
QString attr(const QTextBlock &b, const QString &name);
void setAttr(QTextBlock b, const QString &name, const QString &value); // empty removes

// The format a placeholder's ⚑ is set in
QTextCharFormat markFormat(const QString &sid);
// The plain text of a block, without ⚑ marks
QString blockText(const QTextBlock &b);

} // namespace neosea::doc
