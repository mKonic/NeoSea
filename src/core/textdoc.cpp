#include "core/textdoc.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace neosea::doc {

namespace {

Qt::Alignment alignmentOf(const QString &a)
{
    if (a == "center") return Qt::AlignHCenter;
    if (a == "right") return Qt::AlignRight | Qt::AlignAbsolute;
    if (a == "justify") return Qt::AlignJustify;
    return Qt::AlignLeft | Qt::AlignAbsolute;
}

QString alignName(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter) return "center";
    if (a & Qt::AlignJustify) return "justify";
    if ((a & Qt::AlignRight) && !(a & Qt::AlignLeft)) return "right";
    return {};
}

QTextBlockFormat blockFormatOf(const Para &p)
{
    QTextBlockFormat f;
    f.setAlignment(alignmentOf(p.align()));
    f.setProperty(ParaClass, p.attr("class"));
    QStringList flat;
    for (const auto &[k, v] : p.attrs) flat << k << v;
    f.setProperty(ParaAttrs, flat);
    return f;
}

QTextCharFormat charFormatOf(const Run &r)
{
    if (r.mark) return markFormat(r.sid);
    QTextCharFormat f;
    if (r.b) f.setFontWeight(QFont::Bold);
    if (r.i) f.setFontItalic(true);
    if (r.u) f.setFontUnderline(true);
    if (r.s) f.setFontStrikeOut(true);
    return f;
}

QList<QPair<QString, QString>> attrsOf(const QTextBlockFormat &f)
{
    QList<QPair<QString, QString>> out;
    const QStringList flat = f.property(ParaAttrs).toStringList();
    for (qsizetype i = 0; i + 1 < flat.size(); i += 2) out.append({flat[i], flat[i + 1]});
    return out;
}

} // namespace

QTextCharFormat markFormat(const QString &sid)
{
    QTextCharFormat f;
    f.setProperty(MarkSid, sid);
    f.setBackground(QColor("#f6e3b8"));
    f.setForeground(QColor("#1c1c1c"));
    f.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    f.setUnderlineColor(QColor("#c0392b"));
    return f;
}

void load(QTextDocument &d, const QList<Para> &paras)
{
    d.clear();
    QTextCursor c(&d);
    c.beginEditBlock();
    bool first = true;
    const QList<Para> list = paras.isEmpty() ? QList<Para>{Para{}} : paras;
    for (const Para &p : list) {
        const QTextBlockFormat bf = blockFormatOf(p);
        if (first) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(QTextCharFormat());
            first = false;
        } else {
            c.insertBlock(bf, QTextCharFormat());
        }
        for (const Run &r : p.runs) {
            QString text = r.text;
            text.replace(QChar(0x2028), QChar::LineSeparator);
            c.insertText(text, charFormatOf(r));
        }
    }
    c.endEditBlock();
    d.clearUndoRedoStacks();
    d.setModified(false);
}

QList<Para> paragraphs(const QTextDocument &d)
{
    QList<Para> out;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        Para p;
        const QTextBlockFormat bf = b.blockFormat();
        p.attrs = attrsOf(bf);
        const QString cls = bf.property(ParaClass).toString().trimmed();
        if (cls.isEmpty()) p.removeAttr("class");
        else p.setAttr("class", cls);
        p.setAlign(alignName(bf.alignment()));
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            const QTextCharFormat cf = frag.charFormat();
            const QString sid = cf.property(MarkSid).toString();
            if (cf.hasProperty(MarkSid)) {
                // each ⚑ in the fragment is one mark
                for (QChar ch : frag.text()) {
                    Run m;
                    m.mark = true;
                    m.sid = sid;
                    m.text = QString(ch);
                    p.runs << m;
                }
                continue;
            }
            Run r;
            r.text = frag.text();
            r.text.replace(QChar::LineSeparator, QChar(0x2028));
            r.b = cf.fontWeight() >= QFont::Bold;
            r.i = cf.fontItalic();
            r.u = cf.fontUnderline();
            r.s = cf.fontStrikeOut();
            p.runs << r;
        }
        p.normalize();
        out << p;
    }
    return out;
}

QStringList classes(const QTextBlock &b)
{
    return b.blockFormat().property(ParaClass).toString().split(' ', Qt::SkipEmptyParts);
}

bool hasClass(const QTextBlock &b, const QString &cls) { return classes(b).contains(cls); }

void setClasses(QTextBlock b, const QStringList &cls)
{
    QTextCursor c(b);
    QTextBlockFormat f = b.blockFormat();
    f.setProperty(ParaClass, cls.join(' '));
    c.setBlockFormat(f);
}

QString attr(const QTextBlock &b, const QString &name)
{
    for (const auto &[k, v] : attrsOf(b.blockFormat()))
        if (k == name) return v;
    return {};
}

void setAttr(QTextBlock b, const QString &name, const QString &value)
{
    QTextBlockFormat f = b.blockFormat();
    auto attrs = attrsOf(f);
    bool found = false;
    for (qsizetype i = 0; i < attrs.size(); ++i) {
        if (attrs[i].first != name) continue;
        found = true;
        if (value.isEmpty()) attrs.removeAt(i);
        else attrs[i].second = value;
        break;
    }
    if (!found && !value.isEmpty()) attrs.append({name, value});
    QStringList flat;
    for (const auto &[k, v] : attrs) flat << k << v;
    f.setProperty(ParaAttrs, flat);
    QTextCursor(b).setBlockFormat(f);
}

QString blockText(const QTextBlock &b)
{
    QString out;
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (frag.isValid() && !frag.charFormat().hasProperty(MarkSid)) out += frag.text();
    }
    return out;
}

} // namespace neosea::doc
