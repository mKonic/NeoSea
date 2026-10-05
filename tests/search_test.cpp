#include "core/search.h"
#include "core/textdoc.h"

#include "print.h"

#include <QTextBlock>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::search;

TEST(Search, AnyCaseInReadingOrder)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>Holston climbed.</p><p>holston HOLSTON</p>");
    const auto m = find(d, "holston");
    ASSERT_EQ(m.size(), 3);
    EXPECT_EQ(m[0], (Match{0, 7}));
    EXPECT_EQ(m[1].pos, d.findBlockByNumber(1).position());
    EXPECT_EQ(m[2].pos, d.findBlockByNumber(1).position() + 8);
    EXPECT_TRUE(find(d, "").isEmpty());
}

TEST(Search, AWordHalfInItalicsIsntAMatch)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>tir<i>ed</i> and tired</p>");
    const auto m = find(d, "tired");
    ASSERT_EQ(m.size(), 1);
    EXPECT_EQ(m[0].pos, 10);
}

TEST(Search, ReplaceKeepsTheTypeAndIsOneUndo)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>He felt <i>tired</i>, tired.</p>");
    EXPECT_EQ(replaceAll(d, "TIRED", "weary"), 2);
    EXPECT_EQ(doc::html(d), "<p>He felt <i>weary</i>, weary.</p>");
    d.undo();
    EXPECT_EQ(doc::html(d), "<p>He felt <i>tired</i>, tired.</p>");
}

TEST(Search, ReplaceOne)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>a cat, a cat</p>");
    const auto m = find(d, "cat");
    EXPECT_EQ(replace(d, m[1], "dog"), 1);
    EXPECT_EQ(doc::html(d), "<p>a cat, a dog</p>");
    EXPECT_EQ(replace(d, Match{100, 3}, "x"), 0); // a match an edit left behind
}
