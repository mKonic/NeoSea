#pragma once
// The typing rules, as plain functions of the text before the caret and the
// key just pressed, so the editor stays thin and the rules are testable:
//
//   --        an em dash             ...   an ellipsis
//   " '       quotes that curl the language's way (« » in French, „ “ in German)
//   - (alone) a dialogue or spaced dash, spaced the way the language sets it
//   *word*    italic, **word** bold, ~~word~~ struck (Markdown as you type)
//   a.        a sentence's first letter capitalized; English "i" -> "I"
//   ; : ! ?   French: a narrow no-break space before them

#include "core/chapter.h"

#include <QList>
#include <QString>

#include <optional>

namespace neosea::typing {

struct QuoteStyle {
    QString open, close;
    bool singles = false; // ' also opens single quotes (English, Dutch, Brazil)
};
QuoteStyle quoteStyle(const QString &lang);
// …unless the book has settled on guillemets its language doesn't use
// (»…« in German novels): whichever mark opens the most quotes wins, in this
// chapter, or in the book when the chapter has none yet.
QuoteStyle bookQuotes(const QString &lang, const QString &chapterText, const QString &bookText);
// Is a quotation open in the text so far? Straight marks pair up in turn.
bool quoteOpenIn(const QString &text, const QuoteStyle &q, const QString &straight = {});
// The character a typed " or ' becomes. before: the paragraph's text up to
// the caret; q: bookQuotes for ", the plain style for '.
QString curlQuote(const QString &before, QChar key, const QuoteStyle &q);

struct DashStyle {
    QString open;
    std::optional<QString> space; // the space after an opening dash; unset = as typed
    QString mid;
};
DashStyle dashStyle(const QString &lang);
struct Edit {
    qsizetype at;
    QString from, to;
    bool operator==(const Edit &) const = default;
};
struct Edges {
    bool start = true;  // the text begins its paragraph
    bool end = true;    // the text ends it
    bool spaced = false; // a space comes before it
};
QList<Edit> dialogueDashEdits(const QString &text, const DashStyle &style, Edges edges = {});
QString dialogueDashes(const QString &text, const DashStyle &style, Edges edges = {});
// the same across a pasted paragraph's runs: each change lands in its run
void dashRuns(QList<Run> &runs, const DashStyle &style, Edges edges);
// The edit a key makes to the hyphen just behind the caret, if any.
// before: the paragraph text up to the caret; key: the typed character, or
// empty for Enter. manuscript: the opening dash only applies there.
std::optional<Edit> dashForKey(const QString &before, const QString &key, const DashStyle &style, bool manuscript);

struct Emphasis {
    int open = 0, part = 0; // how many marks open it; how many of the closing were already typed
    bool bold = false, italic = false;
    qsizetype start = 0;    // where the opening marks sit
    QString inner;
};
// The emphasis a typed * or _ closes, if any: ***word***, **word**, *word*.
std::optional<Emphasis> mdEmphasisMatch(const QString &before, QChar mark);
// ~~struck~~: the second closing ~ strikes it; returns where the opening ~~ sit
std::optional<qsizetype> mdStrikeMatch(const QString &before);
// A pasted line of Markdown as runs, or nothing when it has no emphasis
std::optional<QList<Run>> markdownInline(const QString &line);

// French rules when the book is written in French: "fr", "ca" (Quebec: the
// space before the colon only) or empty.
QString frenchTypography(const QString &writingLanguage, const QString &uiLocale);

bool isCapsAbbrev(const QString &word);
// The capital a typed letter becomes at a sentence's start, or nothing
std::optional<QString> autoCapital(const QString &before, const QString &key, const QString &lang);
// English: "i" standing alone before this key becomes "I"; the index of the i
std::optional<qsizetype> englishI(const QString &before, const QString &key, const QString &lang);
// Capitals the dictionary can't see, for a spellcheck pass: indexes of the
// letters in a paragraph that should be capitals
QList<qsizetype> capitalSlips(const QString &text, bool english);

// A line that opens with a dash says who said the lines above it
bool isAttribution(const QString &text);
// A chapter that opens on a line of dialogue sets no drop cap
bool opensWithDash(const QString &text);

} // namespace neosea::typing
