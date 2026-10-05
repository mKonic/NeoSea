#include "core/typing.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::typing;

// Cases from NEO's scripts/dashes.test.js, against our functions

TEST(Dashes, SpeechOpensWithTheLanguagesDash)
{
    const auto pt = dashStyle("pt"), es = dashStyle("es"), en = dashStyle("en");
    EXPECT_EQ(dialogueDashes("- Capitão! Planeta à vista.", pt), "— Capitão! Planeta à vista.");
    EXPECT_EQ(dialogueDashes("-Capitão!", pt), "— Capitão!");
    EXPECT_EQ(dialogueDashes("- Привет", dashStyle("ru")), "— Привет");
    EXPECT_EQ(dialogueDashes("- Hola", es), "—Hola");
    EXPECT_EQ(dialogueDashes("-¿Qué?", es), "—¿Qué?");
    // elsewhere the spacing stays as typed
    EXPECT_EQ(dialogueDashes("- Hello", en), "— Hello");
    EXPECT_EQ(dialogueDashes("-Hello", en), "—Hello");
}

TEST(Dashes, SpacedHyphenMidSentence)
{
    EXPECT_EQ(dialogueDashes("- Entendido - diz Gini.", dashStyle("pt")), "— Entendido — diz Gini.");
    EXPECT_EQ(dialogueDashes("- Hola - dijo él.", dashStyle("es")), "—Hola — dijo él.");
    EXPECT_EQ(dialogueDashes("It was late - too late.", dashStyle("en")), "It was late – too late.");
}

TEST(Dashes, CutOffAtTheEndOfSpeech)
{
    const auto pt = dashStyle("pt"), en = dashStyle("en");
    EXPECT_EQ(dialogueDashes("- Espere -", pt), "— Espere —");
    EXPECT_EQ(dialogueDashes("\"I was just -\"", en), "\"I was just –\"");
    EXPECT_EQ(dialogueDashes("“I was just -” she said.", en), "“I was just –” she said.");
    EXPECT_EQ(dialogueDashes("Mas eu -, disse ele.", pt), "Mas eu —, disse ele.");
}

TEST(Dashes, HyphensInWordsStay)
{
    const auto pt = dashStyle("pt");
    for (const char *s : {"Levou o guarda-chuva.", "Enoch-17 respondeu.", "Pôr-se-ia a caminho.",
                          "O pré- e o pós-operatório.", "Faz -5 °C lá fora.", "O sufixo -mente é comum.",
                          "-5 graus, disse o rádio.", "Pre- and post-war Europe.", "Wait--what?"})
        EXPECT_EQ(dialogueDashes(QString::fromUtf8(s), pt), QString::fromUtf8(s)) << s;
}

TEST(Dashes, BreaksAndBareDashesAlone)
{
    for (const char *s : {"-", "- - -", "---", "* * *", "-  -"})
        EXPECT_EQ(dialogueDashes(s, dashStyle("pt")), s) << s;
}

TEST(Dashes, FragmentsKnowOnlyTheirEdges)
{
    const auto pt = dashStyle("pt"), en = dashStyle("en");
    EXPECT_EQ(dialogueDashes("- olá", pt, {false, true, false}), "- olá");
    EXPECT_EQ(dialogueDashes("espere -", pt, {true, false, false}), "espere -");
    EXPECT_EQ(dialogueDashes("- sim - e saiu", pt, {false, true, true}), "— sim — e saiu");
    EXPECT_EQ(dialogueDashEdits(" - ", pt, {false, false, false}), (QList<Edit>{{1, "-", "—"}}));
    EXPECT_EQ(dialogueDashEdits(" -", en, {false, true, false}), (QList<Edit>{{1, "-", "–"}}));
    EXPECT_EQ(dialogueDashEdits(" -\"", en, {false, false, false}), (QList<Edit>{{1, "-", "–"}}));
    EXPECT_TRUE(dialogueDashEdits(" -m", pt, {false, false, false}).isEmpty());
    EXPECT_TRUE(dialogueDashEdits("- ", pt, {true, false, false}).isEmpty());
    EXPECT_EQ(dialogueDashEdits("- O", pt, {true, false, false}), (QList<Edit>{{0, "- ", "— "}}));
    EXPECT_EQ(dialogueDashEdits("-O", pt, {true, false, false}), (QList<Edit>{{0, "-", "— "}}));
    EXPECT_TRUE(dialogueDashEdits("-5", pt, {true, false, false}).isEmpty());
}

TEST(Dashes, PastedRunsKeepTheirStyle)
{
    neosea::Run a, b, m, c;
    a.text = "- ";
    b.text = "Não";
    c.text = " - disse ela -";
    b.i = true;
    m.mark = true;
    m.sid = "x";
    QList<neosea::Run> runs{a, b, m, c};
    dashRuns(runs, dashStyle("pt"), {});
    EXPECT_EQ(runs[0].text, "— ");
    EXPECT_EQ(runs[1].text, "Não");
    EXPECT_TRUE(runs[1].i);
    EXPECT_EQ(runs[3].text, " — disse ela —");
}

TEST(Dashes, AsTheKeysArrive)
{
    const auto pt = dashStyle("pt");
    // "- " then a letter at a paragraph's start
    EXPECT_EQ(dashForKey("- ", "O", pt, true), (Edit{0, "- ", "— "}));
    // not in Notes: the opening dash is the manuscript's only
    EXPECT_FALSE(dashForKey("- ", "O", pt, false));
    // a spaced hyphen, then a space
    EXPECT_EQ(dashForKey("Sim -", " ", pt, false), (Edit{4, "-", "—"}));
    // Enter after "Espere -"
    EXPECT_EQ(dashForKey("Espere -", "", pt, true), (Edit{7, "-", "—"}));
    EXPECT_FALSE(dashForKey("guarda-", "c", pt, true));
}

TEST(Quotes, OpenAfterADashOrNot)
{
    const QuoteStyle en{"“", "”"}, de{"„", "“"}, single{"‘", "’"};
    EXPECT_TRUE(quoteOpenIn("“I was just—", en));
    EXPECT_TRUE(quoteOpenIn("He said, “I was just—", en));
    EXPECT_TRUE(quoteOpenIn("“Wait—” he said. “And then—", en));
    EXPECT_TRUE(quoteOpenIn("„Ich war—", de));
    EXPECT_TRUE(quoteOpenIn("‘I wasn’t going to—", single));
    EXPECT_FALSE(quoteOpenIn("He stopped—", en));
    EXPECT_FALSE(quoteOpenIn("“Hello,” she said—", en));
    EXPECT_FALSE(quoteOpenIn("“Wait—” he said—", en));
    EXPECT_FALSE(quoteOpenIn("She wasn’t sure—", single));
}

TEST(Quotes, StraightOnesPairUpInTurn)
{
    const QuoteStyle en{"“", "”"};
    EXPECT_TRUE(quoteOpenIn("\"I was just—", en, "\""));
    EXPECT_TRUE(quoteOpenIn("\"Wait—\" he said. \"And then—", en, "\""));
    EXPECT_FALSE(quoteOpenIn("\"Wait—\" he said—", en, "\""));
    EXPECT_TRUE(quoteOpenIn("\"Hello,\" she said. “I was just—", en, "\""));
    EXPECT_TRUE(quoteOpenIn("“Hello,” she said, \"I was just—", en, "\""));
    EXPECT_FALSE(quoteOpenIn("He didn't know—", QuoteStyle{"‘", "’"}));
}

TEST(Quotes, CurlTheLanguagesWay)
{
    const auto en = quoteStyle("en-US"), fr = quoteStyle("fr"), de = quoteStyle("de");
    EXPECT_EQ(curlQuote("", '"', en), "“");
    EXPECT_EQ(curlQuote("Hello", '"', en), "”");
    EXPECT_EQ(curlQuote("", '"', fr), QString("« "));
    EXPECT_EQ(curlQuote("Hallo", '"', de), "“");
    EXPECT_EQ(curlQuote("", '\'', en), "‘");
    EXPECT_EQ(curlQuote("", '\'', de), "’"); // German ' is an apostrophe
    EXPECT_EQ(curlQuote("don", '\'', en), "’");
    EXPECT_EQ(curlQuote("“I was just—", '"', en), "”");
    EXPECT_EQ(curlQuote("He stopped—", '"', en), "“");
}

TEST(Quotes, BookSettledOnGuillemets)
{
    const auto q = bookQuotes("de", "»Hallo«, sagte sie. »Wie geht's?«", "");
    EXPECT_EQ(q.open, "»");
    EXPECT_EQ(bookQuotes("de", "", "»Ja« »Nein«").open, "»");
    EXPECT_EQ(bookQuotes("de", "„Hallo“", "»Ja«").open, "„");
}

TEST(Markdown, EmphasisClosesAsTyped)
{
    auto i = mdEmphasisMatch("a *word", '*');
    ASSERT_TRUE(i);
    EXPECT_TRUE(i->italic);
    EXPECT_FALSE(i->bold);
    EXPECT_EQ(i->start, 2);
    auto b = mdEmphasisMatch("a **word*", '*');
    ASSERT_TRUE(b);
    EXPECT_TRUE(b->bold);
    EXPECT_EQ(b->part, 1);
    // not emphasis: arithmetic, a word with stars, a lone footnote mark
    EXPECT_FALSE(mdEmphasisMatch("2 * 3", '*'));
    EXPECT_FALSE(mdEmphasisMatch("f**", '*'));
    EXPECT_FALSE(mdEmphasisMatch("a footnote", '*'));
    EXPECT_FALSE(mdEmphasisMatch("snake_case", '_'));
    // an emphasis opened inside but not closed keeps the outer one open
    EXPECT_FALSE(mdEmphasisMatch("*a **b c", '*'));
    // in *a **b the next * closes **b, not *a
    auto nested = mdEmphasisMatch("*a **b*", '*');
    ASSERT_TRUE(nested);
    EXPECT_TRUE(nested->bold);
}

TEST(Markdown, StrikeAndPaste)
{
    EXPECT_EQ(mdStrikeMatch("it was ~~gone~"), 7);
    EXPECT_FALSE(mdStrikeMatch("~~ gone~"));
    auto runs = markdownInline("plain *it* and **bold** and ~~gone~~");
    ASSERT_TRUE(runs);
    Para p;
    p.runs = *runs;
    EXPECT_EQ(serializeChapter({p}), "<p>plain <i>it</i> and <b>bold</b> and <strike>gone</strike></p>");
    EXPECT_FALSE(markdownInline("2 * 3 * 4"));
    EXPECT_FALSE(markdownInline("snake_case_name"));
}

TEST(Capitals, SentenceStarts)
{
    EXPECT_EQ(autoCapital("", "t", "en"), "T");
    EXPECT_EQ(autoCapital("“", "h", "en"), "H");
    EXPECT_EQ(autoCapital("It ended. ", "t", "en"), "T");
    EXPECT_FALSE(autoCapital("It ended… ", "t", "en"));  // an ellipsis is the writer's
    EXPECT_FALSE(autoCapital("Mr. ", "s", "en"));        // an abbreviation
    EXPECT_FALSE(autoCapital("J. ", "r", "en"));         // an initial
    EXPECT_FALSE(autoCapital("And ", "t", "en"));
    EXPECT_FALSE(autoCapital("", "T", "en"));
    EXPECT_EQ(autoCapital("", "i", "tr"), "İ"); // Turkish dotted capital
}

TEST(Capitals, EnglishI)
{
    EXPECT_EQ(englishI("and i", " ", "en-GB"), 4);
    EXPECT_EQ(englishI("i", "'", "en"), 0);
    EXPECT_FALSE(englishI("and i", " ", "fr"));
    EXPECT_FALSE(englishI("taxi", " ", "en"));
    EXPECT_FALSE(englishI("(i", ")", "en"));
}

TEST(Capitals, SlipsForTheSpellcheckPass)
{
    EXPECT_EQ(capitalSlips("the end. then i left. Mr. smith i'm sure", true), (QList<qsizetype>{0, 9, 14, 32}));
    EXPECT_TRUE(capitalSlips("Oh! how lovely… and then", true).isEmpty());
    EXPECT_EQ(capitalSlips("then i left", false), (QList<qsizetype>{0}));
}

TEST(Typography, FrenchAndAttribution)
{
    EXPECT_EQ(frenchTypography("fr", "fr-CA"), "ca");
    EXPECT_EQ(frenchTypography("fr-FR", "en"), "fr");
    EXPECT_EQ(frenchTypography("en", "fr"), "");
    EXPECT_TRUE(isAttribution("— Shakespeare"));
    EXPECT_TRUE(isAttribution("- Shakespeare"));
    EXPECT_FALSE(isAttribution("-Shakespeare"));
    EXPECT_TRUE(opensWithDash("  —Hola"));
}
