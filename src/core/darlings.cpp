#include "core/darlings.h"

#include "core/chapter.h"
#include "core/storage.h"
#include "core/textdoc.h"

#include <QDateTime>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>

namespace neosea::darlings {

QString bodyPlain(const QTextDocument &d)
{
    QString out;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) out += b.text();
    return out;
}

int toDocPos(const QTextDocument &d, int plainPos)
{
    int acc = 0;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        const int len = b.length() - 1;
        if (acc + len >= plainPos) return b.position() + (plainPos - acc);
        acc += len;
    }
    return d.characterCount() - 1;
}

int toPlainPos(const QTextDocument &d, int docPos)
{
    const QTextBlock at = d.findBlock(docPos);
    return docPos - at.blockNumber(); // each block before it ends in a separator the plain text doesn't have
}

int findPosition(const QString &plain, const QString &prefix, const QString &suffix)
{
    if (prefix.isNull() && suffix.isNull()) return -1;
    qsizetype i = (prefix + suffix).isEmpty() ? -1 : plain.indexOf(prefix + suffix);
    if (i >= 0) return int(i + prefix.size());
    if (!prefix.isEmpty() && (i = plain.indexOf(prefix)) >= 0) return int(i + prefix.size());
    if (!suffix.isEmpty() && (i = plain.indexOf(suffix)) >= 0) return int(i);
    return -1;
}

QJsonObject cut(QTextCursor &c, const QString &chId, const QString &chapterLabel)
{
    QTextDocument &d = *c.document();
    const int start = c.selectionStart(), end = c.selectionEnd();
    const bool oneBlock = d.findBlock(start) == d.findBlock(end);
    // the words, and their type: a passage within a paragraph stays inline
    QTextDocument tmp;
    QTextCursor(&tmp).insertFragment(c.selection());
    const QList<Para> cutParas = doc::paragraphs(tmp);
    QString html = oneBlock && !cutParas.isEmpty() ? paraInnerHtml(cutParas.first()) : serializeChapter(cutParas);
    QString text = c.selection().toPlainText();
    text.replace(QChar::ParagraphSeparator, '\n').replace(QChar::LineSeparator, '\n');

    c.beginEditBlock();
    c.removeSelectedText();
    // a paragraph taken whole leaves its empty shell: that goes too, and the
    // caret waits at the end of the paragraph before (or the start of the next)
    QTextBlock b = c.block();
    if (d.blockCount() > 1 && b.text().trimmed().isEmpty() && !b.text().contains(QStringLiteral("⚑"))) {
        const bool hasPrev = b.previous().isValid();
        QTextCursor k(b);
        if (hasPrev) {
            k.setPosition(b.position() - 1);
            k.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        } else {
            k.setPosition(b.position());
            k.setPosition(b.position() + b.length(), QTextCursor::KeepAnchor);
        }
        k.removeSelectedText();
        c.setPosition(hasPrev ? k.position() : 0);
    }
    c.endEditBlock();

    const QString plain = bodyPlain(d);
    const int at = toPlainPos(d, c.position());
    return QJsonObject{{"id", "d-" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36)},
                       {"html", html},
                       {"text", text},
                       {"chapterId", chId.isEmpty() ? QJsonValue() : QJsonValue(chId)},
                       {"chapterLabel", chapterLabel},
                       {"anchorPrefix", plain.left(at).right(60)},
                       {"anchorSuffix", plain.mid(at, 60)},
                       {"date", isoNow()}};
}

} // namespace neosea::darlings
