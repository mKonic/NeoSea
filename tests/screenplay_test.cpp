// Cases from NEO's scripts/screenplay.test.js, against our functions
#include "core/screenplay.h"

#include "print.h"

#include <QFile>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::sp;

namespace {

Line L(const char *type, const char *text) { return Line{type, QString::fromUtf8(text)}; }

QString slurp(const QString &f)
{
    QFile file(f);
    (void)file.open(QIODevice::ReadOnly);
    return QString::fromUtf8(file.readAll());
}

QString joined(const QList<Run> &runs)
{
    QString s;
    for (const Run &r : runs) s += r.text;
    return s;
}

QStringList typed(const QList<FdxLine> &lines)
{
    QStringList out;
    for (const auto &l : lines) out << l.type + ":" + joined(l.runs);
    return out;
}

} // namespace

TEST(Screenplay, HeadingsAndNotWordsThatStartTheSame)
{
    for (const char *s : {"INT. HOUSE", "ext. beach - day", "I/E CAR", "INT./EXT. CAR", "EST. CITY", "int house"})
        EXPECT_TRUE(isHeading(s)) << s;
    for (const char *s : {"Interior lights flicker.", "Internal memo", "Extra! Extra!", "Estelle walks in."})
        EXPECT_FALSE(isHeading(s)) << s;
}

TEST(Screenplay, CharactersAndTransitions)
{
    for (const char *s : {"KIM", "VERNON (V.O.)", "MRS. HANLON", "DEPUTY #2", "KIM (CONT'D)"})
        EXPECT_TRUE(looksLikeCharacter(s)) << s;
    for (const char *s : {"BOOM!", "FADE IN:", "Kim walks in.", "THE WHOLE BUILDING SHAKES AND THEN STOPS", "", "1984"})
        EXPECT_FALSE(looksLikeCharacter(s)) << s;
    EXPECT_TRUE(looksLikeTransition("CUT TO:"));
    EXPECT_TRUE(looksLikeTransition("FADE OUT."));
    EXPECT_TRUE(looksLikeTransition("Cut to:"));
    EXPECT_FALSE(looksLikeTransition("He points to:"));
    EXPECT_FALSE(looksLikeTransition("FADE IN:"));
}

TEST(Screenplay, EnterRhythm)
{
    EXPECT_EQ(after("character"), "dialogue");
    EXPECT_EQ(after("dialogue"), "action");
    EXPECT_EQ(after("heading"), "action");
    EXPECT_EQ(after("transition"), "heading");
    EXPECT_EQ(onEmpty("action"), "character");
    EXPECT_EQ(onEmpty("character"), "action");
}

TEST(Screenplay, GhostOffersOnlyWhatTheScriptHas)
{
    QList<Line> s{L("heading", "EXT. LEEVILLE MARINA - NIGHT"), L("action", "Fog."),
                  L("heading", "INT. HARBOR OFFICE - CONTINUOUS"), L("character", "KIM"), L("dialogue", "Third night."),
                  L("character", "VERNON"), L("dialogue", "Fourth."), L("character", "KIM"),
                  L("dialogue", "Not on the chart."), L("action", "He looks at her."), L("character", "")};
    EXPECT_EQ(ghost(s, 10), "VERNON");
    s[10].text = "k";
    EXPECT_EQ(ghost(s, 10), "IM");
    s[10].text = "KIM (V";
    EXPECT_EQ(ghost(s, 10), ".O.)");
    s << L("heading", "INT. H");
    EXPECT_EQ(ghost(s, 11), "ARBOR OFFICE");
    s[11].text = "int. harbor office - n";
    EXPECT_EQ(ghost(s, 11), "IGHT");
    s[11].text = "INT. HARBOR OFFICE - C";
    EXPECT_EQ(ghost(s, 11), "ONTINUOUS");
    s[11].text = "INT. Q";
    EXPECT_EQ(ghost(s, 11), "");
    s << L("transition", "SM");
    EXPECT_EQ(ghost(s, 12), "ASH CUT TO:");
    EXPECT_EQ(parseHeading("INT. HARBOR OFFICE - NIGHT"), (Heading{"INT.", "HARBOR OFFICE", QString("NIGHT")}));
}

TEST(Screenplay, ContdAfterActionInTheSameScene)
{
    QList<Line> s{L("character", "KIM"), L("dialogue", "One."), L("action", "She waits."), L("character", "Kim"),
                  L("dialogue", "Two.")};
    EXPECT_TRUE(contd(s, 3));
    s[2] = L("character", "VERNON");
    EXPECT_FALSE(contd(s, 3));
    EXPECT_FALSE(contd({L("character", "KIM"), L("dialogue", "One."), L("character", "KIM")}, 2));
    EXPECT_FALSE(contd({L("character", "KIM"), L("heading", "INT. X"), L("action", "a"), L("character", "KIM")}, 3));
    EXPECT_FALSE(contd({L("character", "KIM"), L("action", "a"), L("character", "KIM (V.O.)")}, 2));
}

TEST(Screenplay, Pages)
{
    auto many = [](int n, const char *type = "action") { return QList<Item>(n, Item{type, 1}); };
    const auto pg = paginate(many(40));
    EXPECT_EQ(pg.pages, 2);
    qsizetype firstBreak = -1;
    for (qsizetype k = 0; k < pg.at.size(); ++k)
        if (pg.at[k].brk) { firstBreak = k; break; }
    EXPECT_EQ(firstBreak, 27);
    EXPECT_EQ(pg.at[27].fill, 1);
    EXPECT_EQ(pg.at[27].before, 0);
    auto h = many(26);
    h << Item{"heading", 1} << Item{"action", 1};
    EXPECT_TRUE(paginate(h).at[26].brk) << "the heading goes over with its scene";
    auto d = many(26);
    d << Item{"character", 1} << Item{"dialogue", 1} << Item{"dialogue", 1};
    EXPECT_TRUE(paginate(d).at[26].brk) << "the speech goes over with its speaker";
    EXPECT_EQ(paginate({Item{"action", 120}, Item{"action", 1}}).pages, 3);
    const auto two = paginate({Item{"heading", 1}, Item{"action", 1}, Item{"heading", 1}, Item{"action", 1}});
    QList<int> before;
    for (const auto &a : two.at) before << a.before;
    EXPECT_EQ(before, (QList<int>{0, 1, 2, 1}));
    EXPECT_EQ(eighths(54), 8);
    EXPECT_EQ(eighths(7), 1);
}

TEST(Screenplay, FountainOutForcesMisreads)
{
    const QString out = toFountain({L("heading", "int. kitchen - day"), L("action", "KIM ENTERS"), L("character", "kim"),
                                    L("paren", "(quietly)"), L("dialogue", "Hi."), L("transition", "cut to:"),
                                    L("heading", "FLASHBACK"), L("transition", "BACK TO PRESENT"),
                                    L("shot", "close on the bell")},
                                   {"No Wind", "Written by", "Hugh Howey", "", "A\nB"});
    EXPECT_EQ(out, QStringList({"Title: No Wind", "Credit: Written by", "Author: Hugh Howey", "Contact:", "    A", "    B", "",
                                "INT. KITCHEN - DAY", "", "!KIM ENTERS", "", "KIM", "(quietly)", "Hi.", "", "CUT TO:", "",
                                ".FLASHBACK", "", ">BACK TO PRESENT", "", "!CLOSE ON THE BELL", ""})
                       .join('\n'));
}

TEST(Screenplay, FountainInSortsElements)
{
    const QString src = QStringList({"Title: No Wind", "Author: Hugh Howey", "", "EXT. MARINA - NIGHT", "",
                                     "Fog. A bell rings.", "", "KIM", "(quietly)", "Three nights.", "", "VERNON ^", "Four.",
                                     "", "CUT TO:", "", ".FLASHBACK", "", "!LOUD NOISE", "", "[[a note]]", "/* gone */",
                                     "# Act One", "= synopsis", "===", "@McCLANE", "Yippee."})
                            .join('\n');
    EXPECT_EQ(fromFountain(src),
              (QList<Line>{L("heading", "EXT. MARINA - NIGHT"), L("action", "Fog. A bell rings."), L("character", "KIM"),
                           L("paren", "(quietly)"), L("dialogue", "Three nights."), L("character", "VERNON"),
                           L("dialogue", "Four."), L("transition", "CUT TO:"), L("heading", "FLASHBACK"),
                           L("action", "LOUD NOISE"), L("character", "McCLANE"), L("dialogue", "Yippee.")}));
}

TEST(Screenplay, FountainInJoinsBlocksAndDropsPdfFurniture)
{
    const QString src = QStringList({"INT. HARBOR OFFICE - DAWN", "", "Gray light. Vernon asleep in his",
                                     "chair. The chain still on the desk.", "", "2.", "", "KIM (CONT'D)",
                                     "We should call somebody. The", "Coast Guard.", "(MORE)", "", "CONTINUED:"})
                            .join('\n');
    EXPECT_EQ(fromFountain(src),
              (QList<Line>{L("heading", "INT. HARBOR OFFICE - DAWN"),
                           L("action", "Gray light. Vernon asleep in his chair. The chain still on the desk."),
                           L("character", "KIM"), L("dialogue", "We should call somebody. The Coast Guard.")}));
}

TEST(Screenplay, FountainTitleAndEmphasis)
{
    const QString src = "Title:\n    _**NO WIND**_\nCredit: Written by\nAuthor: Hugh Howey\nDraft date: First Draft\n"
                        "Contact:\n    Kristin Nelson\n    Nelson Literary Agency\n\nEXT. A - DAY";
    EXPECT_EQ(fountainTitle(src),
              (TitlePage{"NO WIND", "Written by", "Hugh Howey", "First Draft", "Kristin Nelson\nNelson Literary Agency"}));
    EXPECT_EQ(fountainTitle("EXT. A - DAY"), TitlePage{});
    auto r = [](const char *t) {
        QStringList out;
        for (const neosea::Run &x : runsFromFountain(t))
            out << QString(x.b ? "B" : "") + (x.i ? "I" : "") + (x.u ? "U" : "") + ":" + x.text;
        return out;
    };
    EXPECT_EQ(r("He *really* means it."), (QStringList{":He ", "I:really", ": means it."}));
    EXPECT_EQ(r("**Bold** and ***both*** and _under_"), (QStringList{"B:Bold", ": and ", "BI:both", ": and ", "U:under"}));
    EXPECT_EQ(r("2 * 3 = 6 and snake_case"), (QStringList{":2 * 3 = 6 and snake_case"}));
    EXPECT_EQ(r("\\*not italic\\*"), (QStringList{":*not italic*"}));
}

TEST(Screenplay, FinalDraftFromScreenplainMatchesItsFountain)
{
    const Fdx fdx = fromFdx(slurp(NEOSEA_TEST_FIXTURES "/screenplain.fdx"));
    QList<Line> fromFdxLines;
    for (const auto &l : fdx.lines) fromFdxLines << Line{l.type, joined(l.runs)};
    EXPECT_EQ(fromFdxLines, fromFountain(slurp(NEOSEA_TEST_FIXTURES "/screenplain.fountain")));
    EXPECT_GT(fromFdxLines.size(), 10);
}

TEST(Screenplay, FinalDraftStylesDualDialogueTitlePage)
{
    const QString xml = R"(<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<FinalDraft DocumentType="Script" Template="No" Version="5">
  <Content>
    <Paragraph Type="Scene Heading" Number="1"><SceneProperties Length="1/8"/><Text>INT. GALLEY - NIGHT</Text></Paragraph>
    <Paragraph Type="Action"><Text>The bell </Text><Text Style="Bold+Italic">rings</Text><Text> &amp; stops.</Text></Paragraph>
    <Paragraph Type="Action"><Text></Text></Paragraph>
    <Paragraph><DualDialogue>
      <Paragraph Type="Character"><Text>KIM (CONT'D)</Text></Paragraph>
      <Paragraph Type="Dialogue"><Text>Now.</Text></Paragraph>
      <Paragraph Type="Character"><Text>VERNON</Text></Paragraph>
      <Paragraph Type="Parenthetical"><Text>quietly</Text></Paragraph>
      <Paragraph Type="Dialogue"><Text>Now.</Text></Paragraph>
    </DualDialogue></Paragraph>
    <Paragraph Type="General"><Text>THE END</Text></Paragraph>
  </Content>
  <TitlePage><Content>
    <Paragraph Alignment="Center"><Text>NO WIND</Text></Paragraph>
    <Paragraph Alignment="Center"><Text>Written by</Text></Paragraph>
    <Paragraph Alignment="Center"><Text>Hugh Howey</Text></Paragraph>
    <Paragraph Alignment="Left"><Text>Nelson Literary</Text></Paragraph>
    <Paragraph Alignment="Right"><Text>Draft 2</Text></Paragraph>
  </Content></TitlePage>
</FinalDraft>)";
    const Fdx f = fromFdx(xml);
    EXPECT_EQ(typed(f.lines), (QStringList{"heading:INT. GALLEY - NIGHT", "action:The bell rings & stops.", "character:KIM",
                                           "dialogue:Now.", "character:VERNON", "paren:(quietly)", "dialogue:Now.",
                                           "action:THE END"}));
    EXPECT_TRUE(f.lines[1].runs[1].b && f.lines[1].runs[1].i);
    EXPECT_EQ(f.title, (TitlePage{"NO WIND", "Written by", "Hugh Howey", "Draft 2", "Nelson Literary"}));
}

TEST(Screenplay, FinalDraftOutAndInAgain)
{
    auto run = [](const char *text, bool i = false) {
        neosea::Run r;
        r.text = QString::fromUtf8(text);
        r.i = i;
        return r;
    };
    const QList<FdxLine> lines{{"heading", {run("int. galley - night")}},
                               {"action", {run("The bell "), run("rings", true), run(" <loud> & \"clear\".")}},
                               {"character", {run("Kim")}},
                               {"paren", {run("(quietly)")}},
                               {"dialogue", {run("Now.")}},
                               {"transition", {run("cut to:")}},
                               {"shot", {run("close on the bell")}}};
    const TitlePage tp{"No Wind", "Written by", "Hugh Howey", "First Draft", "Nelson Literary\nDenver"};
    const QString xml = toFdx(lines, tp);
    EXPECT_TRUE(xml.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n<FinalDraft DocumentType=\"Script\""));
    EXPECT_TRUE(xml.contains("<Text Style=\"Italic\">rings</Text>"));
    EXPECT_TRUE(xml.contains("&lt;loud&gt; &amp; &quot;clear&quot;"));
    const Fdx back = fromFdx(xml);
    EXPECT_EQ(typed(back.lines), (QStringList{"heading:INT. GALLEY - NIGHT", "action:The bell rings <loud> & \"clear\".",
                                              "character:KIM", "paren:(quietly)", "dialogue:Now.", "transition:CUT TO:",
                                              "shot:CLOSE ON THE BELL"}));
    TitlePage want = tp;
    want.title = "NO WIND";
    EXPECT_EQ(back.title, want);
}

TEST(Screenplay, RunsBackToFountainEmphasis)
{
    neosea::Run a, b, c;
    a.text = "He ";
    b.text = "really ";
    b.i = true;
    c.text = "*means* it";
    EXPECT_EQ(fountainOfRuns({a, b, c}), "He *really* \\*means\\* it");
    const auto again = runsFromFountain(fountainOfRuns({a, b, c}));
    EXPECT_EQ(joined(again), "He really *means* it");
}

TEST(Screenplay, ParagraphElement)
{
    Para p;
    EXPECT_EQ(typeOf(p), "action");
    setType(p, "character");
    EXPECT_EQ(p.attr("class"), "sp-character");
    setType(p, "action");
    EXPECT_FALSE(p.hasAttr("class"));
}
