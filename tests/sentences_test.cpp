#include "core/sentences.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;

namespace {
QString at(const QString &text, int offset, const QString &lang = "en")
{
    const auto [a, b] = sentenceAt(text, offset, lang);
    return text.mid(a, b - a);
}
} // namespace

TEST(Sentences, TheOneTheCaretIsIn)
{
    const QString t = "He climbed. She waited by the door. Then rain.";
    EXPECT_EQ(at(t, 2), "He climbed.");
    EXPECT_EQ(at(t, 15), "She waited by the door.");
    EXPECT_EQ(at(t, int(t.size())), "Then rain.");
}

TEST(Sentences, TheCaretAtAnEndBelongsToTheNextWhenItCan)
{
    const QString t = "One. Two.";
    EXPECT_EQ(at(t, 4), "One.");  // just after the full stop, before the space: still the first
    EXPECT_EQ(at(t, 5), "Two.");   // past the space: the next
}

TEST(Sentences, TrailingSpaceIsLeftOff)
{
    EXPECT_EQ(at("Alone.   ", 1), "Alone.");
    EXPECT_EQ(sentenceAt("   ", 1, "en"), qMakePair(0, 0));
}

TEST(Sentences, AbbreviationsDontEndASentence)
{
    EXPECT_EQ(at("Mr. Smith left. He was late.", 5), "Mr. Smith left.");
}

TEST(Sentences, EverySentenceFromAnOffset)
{
    const QString t = "One. Two two. Three.";
    EXPECT_EQ(sentenceSpans(t, 0, "en"), (QList<QPair<int, int>>{{0, 5}, {5, 14}, {14, 20}}));
    EXPECT_EQ(sentenceSpans(t, 7, "en"), (QList<QPair<int, int>>{{7, 14}, {14, 20}})); // from the middle of the second
    EXPECT_TRUE(sentenceSpans("   ", 0, "en").isEmpty());
}
