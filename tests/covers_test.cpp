#include "core/covers.h"

#include "print.h"

#include <QJsonObject>
#include <QPainter>

#include <gtest/gtest.h>

using namespace neosea;

// the values NEO's covers.js gives for the same seeds (run under node)
TEST(Covers, SameRandomnessAsNeo)
{
    EXPECT_EQ(covers::hash("book-abc"), 1666910689u);
    EXPECT_EQ(covers::hash("type:Wool"), 406354400u);
    EXPECT_EQ(covers::hash("Война"), 1234950397u);
    covers::Rng r(covers::hash("book-abc"));
    EXPECT_DOUBLE_EQ(r(), 0.5492332358844578);
    EXPECT_DOUBLE_EQ(r(), 0.994353876914829);
    EXPECT_DOUBLE_EQ(r(), 0.7465614578686655);
}

TEST(Covers, SameStyleAsNeo)
{
    EXPECT_EQ(covers::styleOf("book-abc"), "horizon");
    EXPECT_EQ(covers::styleOf("seed-2"), "horizon");
    EXPECT_EQ(covers::styleOf("x"), "solitary");
}

TEST(Covers, TitlesBreakLikeDisplayType)
{
    auto texts = [](const QList<covers::Line> &lines) {
        QStringList out;
        for (const auto &l : lines) out << (l.small ? "~" : "") + l.text;
        return out;
    };
    EXPECT_EQ(texts(covers::breakLines("The Road", "stack")), (QStringList{"The", "Road"})); // a lone leading connector stays big
    EXPECT_EQ(texts(covers::breakLines("Lord of Flies", "stack")), (QStringList{"Lord", "~of", "Flies"}));
    EXPECT_EQ(texts(covers::breakLines("Lord of Flies", "band")), (QStringList{"Lord", "of", "Flies"}));
    // more words than lines: balanced, connectors glued to the next word
    const auto many = covers::breakLines("The Girl with the Dragon Tattoo and Other Stories", "black");
    // NEO's own result for this title (covers.js under node)
    EXPECT_EQ(texts(many), (QStringList{"The Girl with the", "Dragon Tattoo", "and Other Stories"}));
}

TEST(Covers, AbstractsAreStableAndDistinct)
{
    const QImage a = covers::paintAbstract("book-abc");
    const QImage b = covers::paintAbstract("book-abc");
    const QImage c = covers::paintAbstract("book-xyz");
    EXPECT_EQ(a.size(), QSize(208, 300));
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Covers, PlanPicksInkFromTheArt)
{
    QImage white(208, 300, QImage::Format_RGB32);
    white.fill(Qt::white);
    QImage black(208, 300, QImage::Format_RGB32);
    black.fill(Qt::black);
    const QJsonObject meta{{"id", "b1"}, {"title", "Wool"}, {"author", "Hugh Howey"}};
    EXPECT_FALSE(covers::plan(meta, white).ink.light); // dark ink on pale art
    EXPECT_TRUE(covers::plan(meta, black).ink.light);
    EXPECT_FALSE(covers::plan(meta, black).ink.scrim);
    // every big line is sized to run the cover's width
    for (const auto &l : covers::plan(meta, black).lines) EXPECT_GT(l.size, 7);
}

TEST(Covers, FullCoverRenders)
{
    const QImage img = covers::renderFull(QJsonObject{{"id", "b1"}, {"title", "Wool"}, {"author", "Hugh Howey"}}, {}, QSize(320, 512));
    EXPECT_EQ(img.size(), QSize(320, 512));
    // the type changed pixels beyond the art: not one flat colour
    EXPECT_NE(img.pixel(160, 100), img.pixel(5, 5));
}
