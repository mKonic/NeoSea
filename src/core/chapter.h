#pragma once
// A chapter as NEO stores it: a run of <p> elements, each with a class that
// says what it is, and inline bold, italic, underline, strikethrough and the
// ⚑ marks of placeholders.
//
//   <p>prose</p>                         an ordinary paragraph
//   <p class="scene-break">***</p>       a section break
//   <p class="poetry"><i>…</i></p>       pulled in from the margins, italic
//   <p class="flush">…</p>               prose with no first-line indent
//   <p class="ghost" data-sec-id="…">    an outline note not yet written over
//   <p class="sp-heading">…</p>          a script's elements (sp-character, …)
//   <span class="ph-mark" data-sid="…" contenteditable="false">⚑</span>
//
// Paragraph attributes are kept in order, so a chapter nobody edited writes
// back as it was read.

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace neosea {

struct Run {
    QString text;           // U+2028 stands for a <br> inside a paragraph
    bool b = false, i = false, u = false, s = false;
    bool mark = false;      // a placeholder's ⚑
    QString sid;            // the placeholder's sticky id

    bool sameStyle(const Run &o) const { return !mark && !o.mark && b == o.b && i == o.i && u == o.u && s == o.s; }
    bool operator==(const Run &o) const = default;
};

struct Para {
    QList<QPair<QString, QString>> attrs;
    QList<Run> runs;

    QString attr(const QString &name) const;
    bool hasAttr(const QString &name) const;
    void setAttr(const QString &name, const QString &value);
    void removeAttr(const QString &name);
    bool hasClass(const QString &cls) const;
    void addClass(const QString &cls);
    void removeClass(const QString &cls);
    QStringList classes() const;
    QString align() const; // left/center/right/justify, empty = the page's own
    void setAlign(const QString &align);

    QString text() const;  // marks left out
    bool isBlank() const { return text().trimmed().isEmpty(); }
    bool hasMark() const;
    // merges neighbouring runs of the same style, drops empty ones
    void normalize();
    bool operator==(const Para &o) const = default;
};

// Reads a chapter body. Loose text and other blocks become paragraphs, junk
// spans unwrap, legacy darling anchors go, and the marks NEO sets for the
// screen only (data-first, data-pg, …) are never carried.
QList<Para> parseChapter(const QString &html);
QString serializeChapter(const QList<Para> &paras);
QString paraInnerHtml(const Para &p);
QString runHtml(const Run &r);

// A chapter's plain text, a line per paragraph, without ghosts or marks: what
// the word count reads.
QString chapterPlainText(const QString &html);

// NEO's escHtml (& < > only), for text set into exported HTML
QString escHtml(const QString &s);

// Pasted HTML reduced to a manuscript: paragraphs, bold, italic, underline,
// strikethrough and placeholder marks. Every block edge and <br> is a paragraph
// break; styling that lives only in a style attribute still counts.
QList<Para> cleanPaste(const QString &html);

} // namespace neosea
