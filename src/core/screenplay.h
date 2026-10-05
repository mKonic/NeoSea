#pragma once
// Screenplay rules: plain functions of a script's lines, no page. A script is
// one chapter whose paragraphs are its elements, each with the class
// sp-<element> (action has none). Nobody has to pick an element: INT. or EXT.
// makes a scene heading, a short line in capitals followed by Enter makes a
// character, and Enter on an empty line changes what that line is (the rules
// of Fountain, the plain-text screenplay format).

#include "core/chapter.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace neosea::sp {

inline const QStringList kTypes{"heading", "action", "character", "paren", "dialogue", "transition", "shot"};
inline constexpr int kLinesPerPage = 54;

// blank lines above each element (two above a scene heading)
int linesBefore(const QString &type);
// Enter at the end of a line with words: what the next line is
QString after(const QString &type);
// Enter on an empty line: what that line becomes
QString onEmpty(const QString &type);
// Tab steps through these (in a speech, Tab trades dialogue and parenthetical)
inline const QStringList kCycle{"action", "character", "transition", "heading", "shot"};

struct Line {
    QString type;
    QString text;
    bool operator==(const Line &) const = default;
};

bool isHeading(const QString &text);  // INT. / EXT. / I/E / EST.
QString bareName(const QString &text); // without (V.O.) and the like
bool looksLikeTransition(const QString &text);
bool looksLikeCharacter(const QString &text);
struct Heading {
    QString prefix, loc;
    std::optional<QString> time;
    bool operator==(const Heading &) const = default;
};
std::optional<Heading> parseHeading(const QString &text);
// the gray suggestion for line i, the caret at its end
QString ghost(const QList<Line> &lines, qsizetype i);
// (CONT'D): the same voice again, after action, in the same scene
bool contd(const QList<Line> &lines, qsizetype i);

struct Item {
    QString type;
    int lines;
};
struct Placed {
    int page = 1, before = 0;
    bool brk = false;
    int fill = 0;
};
struct Pages {
    QList<Placed> at;
    int pages = 1, used = 0;
};
// The pages as they print: 54 lines, a speech kept with its speaker, a scene
// heading never alone at the foot of a page.
Pages paginate(const QList<Item> &items, int perPage = kLinesPerPage);
int eighths(int lines, int perPage = kLinesPerPage);

struct TitlePage {
    QString title, credit, author, draft, contact;
    bool operator==(const TitlePage &) const = default;
};

QString toFountain(const QList<Line> &lines, const TitlePage &title = {});
QList<Line> fromFountain(const QString &src);
TitlePage fountainTitle(const QString &src);
QList<Run> runsFromFountain(const QString &text);
// the inverse, for export: a line's runs in Fountain's emphasis
QString fountainOfRuns(const QList<Run> &runs);

struct FdxLine {
    QString type;
    QList<Run> runs;
};
struct Fdx {
    QList<FdxLine> lines;
    TitlePage title;
};
Fdx fromFdx(const QString &xml);
QString toFdx(const QList<FdxLine> &lines, const TitlePage &title = {});

// a script's paragraph <-> its element
QString typeOf(const Para &p);
void setType(Para &p, const QString &type);

} // namespace neosea::sp
