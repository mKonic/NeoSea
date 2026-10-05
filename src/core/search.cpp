#include "core/search.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace neosea::search {

QList<Match> findInText(const QString &text, const QString &q)
{
    QList<Match> out;
    if (q.isEmpty()) return out;
    for (qsizetype at = text.indexOf(q, 0, Qt::CaseInsensitive); at >= 0; at = text.indexOf(q, at + q.size(), Qt::CaseInsensitive))
        out << Match{int(at), int(q.size())};
    return out;
}

QList<Match> find(const QTextDocument &d, const QString &q)
{
    QList<Match> out;
    if (q.isEmpty()) return out;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            for (const Match &m : findInText(f.text(), q)) out << Match{f.position() + m.pos, m.len};
        }
    return out;
}

namespace {

void put(QTextCursor &c, const Match &m, const QString &with)
{
    c.setPosition(m.pos);
    c.setPosition(m.pos + 1, QTextCursor::KeepAnchor);
    const QTextCharFormat fmt = c.charFormat(); // the type of the match's own first letter
    c.setPosition(m.pos);
    c.setPosition(m.pos + m.len, QTextCursor::KeepAnchor);
    c.insertText(with, fmt);
}

} // namespace

int replace(QTextDocument &d, const Match &m, const QString &with)
{
    if (m.len <= 0 || m.pos + m.len > d.characterCount()) return 0;
    QTextCursor c(&d);
    c.beginEditBlock();
    put(c, m, with);
    c.endEditBlock();
    return 1;
}

int replaceAll(QTextDocument &d, const QString &q, const QString &with)
{
    const QList<Match> all = find(d, q);
    if (all.isEmpty()) return 0;
    QTextCursor c(&d);
    c.beginEditBlock();
    // back to front, so the places ahead stay where they were
    for (auto it = all.rbegin(); it != all.rend(); ++it) put(c, *it, with);
    c.endEditBlock();
    return int(all.size());
}

} // namespace neosea::search
