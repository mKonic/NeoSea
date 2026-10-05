#include "core/spell.h"

#include "print.h"

#include <QSet>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::spell;

namespace {

QStringList words(const QString &text)
{
    QStringList out;
    for (const auto &o : occurrences(text)) {
        if (!o.whole.isEmpty()) out << "[" + o.whole + "]";
        for (const auto &p : o.parts) out << p.word;
    }
    return out;
}

QStringList underlined(const QString &text, const QSet<QString> &dictionary)
{
    QStringList out;
    for (const auto &[a, b] : wrong(occurrences(text), [&](const QString &w) { return dictionary.contains(w.toLower()); })) out << text.mid(a, b - a);
    return out;
}

} // namespace

TEST(Spell, WordsOfAnyAlphabetWithTheirAccents)
{
    EXPECT_EQ(words("Café déjà vu, Москва"), (QStringList{"Café", "déjà", "vu", "Москва"}));
    EXPECT_EQ(words("don’t ‘quote’"), (QStringList{"don't", "quote"})); // curly apostrophes count; quotes at the ends don't
}

TEST(Spell, ShoutingAndSingleLettersAreLegal)
{
    EXPECT_EQ(words("NASA said I was OK"), (QStringList{"said", "was"}));
}

TEST(Spell, HyphenatedWordsAreJudgedWholeFirst)
{
    EXPECT_EQ(words("well-known"), (QStringList{"[well-known]", "well", "known"}));
    const QSet<QString> dict{"well", "known", "e-mail"};
    EXPECT_EQ(underlined("e-mail", {"e-mail"}), QStringList{});          // the whole is a word
    EXPECT_EQ(underlined("well-knwn", dict), QStringList{"knwn"});       // a wrong piece is underlined alone
    EXPECT_EQ(underlined("well-known", dict), QStringList{"well-known"}); // every piece fine, the whole not
}

TEST(Spell, AStammerIsJudgedByTheWordItLandsOn)
{
    EXPECT_EQ(words("Wh-what"), QStringList{"what"});
    EXPECT_EQ(words("N-não"), QStringList{"não"});
    EXPECT_EQ(words("up-to"), (QStringList{"[up-to]", "up", "to"})); // not a stammer: "up" doesn't start "to"
}

TEST(Spell, TheDefaultFollowsTheInterface)
{
    EXPECT_EQ(defaultLanguage("fr-CA"), "fr");
    EXPECT_EQ(defaultLanguage("pt"), "pt-BR");
    EXPECT_EQ(defaultLanguage("en-GB"), "en-GB");
    EXPECT_EQ(defaultLanguage("ja"), "en-US");
    EXPECT_EQ(defaultLanguage(""), "en-US");
}

TEST(Spell, HunspellWithTheBundledDictionary)
{
    const QString base = dictionaryBase(QStringLiteral(NEOSEA_SOURCE_RESOURCES), "en-US");
    if (base.isEmpty()) GTEST_SKIP() << "no en-US dictionary (scripts/fetch-dictionaries.sh)";
    Speller s;
    EXPECT_TRUE(s.check("anything")); // nothing loaded: nothing is wrong
    ASSERT_TRUE(s.load(base, "en-US", {"Holston"}));
    EXPECT_TRUE(s.check("climbed"));
    EXPECT_FALSE(s.check("climbd"));
    EXPECT_TRUE(s.check("Holston")); // the writer's own words
    EXPECT_TRUE(s.suggest("climbd").contains("climbed"));
    EXPECT_FALSE(s.check("Zorblax"));
    s.add("Zorblax");
    EXPECT_TRUE(s.check("Zorblax"));
}
